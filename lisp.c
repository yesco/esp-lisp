/* A mini "lisp machine" */

// Lua time 100,000 => 2s
// ----------------------
//   s=0; for i=1,100000 do s=s+i end; print(s);
//   function tail(n, s) if n == 0 then return s else return tail(n-1, s+1); end end print(tail(100000, 0))

// DEF(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
// princ(evalGC(read("(tail 100000 0)"), env));
// -----------------------------------------------------------------
// lisp.c (tail 1,000 0)
//   all alloc/evalGC gives 5240ms with print
//   no print gc => 5040ms
//   ==> painful slow!
//   NO INT alloc => 4500ms
//   needGC() function, block mark => 1070ms
//
// lisp.c (tail 10,000 0)
//   => 9380, 9920, 10500
//   reuse() looped always from 0, made it round-robin => 3000... 4200... 4000ms!
//   mark_deep() + p->index=i ===> 1500-2000ms
//   car/cdr macro, on desktop 6.50 -> 4.5s for for 1M loop for esp => 1400-1800
//   hardcode primapply 2 parameters => 1100ms
//   hardcode primapply 1,2,3,-3 (if),-16 => 860-1100ms
//   slot alloc => 590,600 ms
//   eq => ==, remove one eval, tag test => 540,550ms
//
// lisp.c (tail 10,000 0)
//   now takes 5300ms more or less exactly every time
//   2.5x slower than lua, didn't actually measure actual lua time
//   evalGC now lookup vars too => 4780
//   bindEvalList (combined evallist + bindlist) => 4040ms!
//   ... => 4010ms
//
//   slight increase if change the MAX_ALLOC to 512 but it keeps 17K free! => 4180ms

//
// RAW C esp8266 - printf("10,000,000 LOOP (100x lua) TIME=%d\r\n", tm); ===> 50ms
//

#ifndef TEST
  #include "espressif/esp_common.h"
  //#include "FreeRTOS.h" // just for MEM FREE QUESTION
  #define LOOP 99999
  #define LOOPTAIL "(tail 99999 0)"
#endif

#ifdef TEST
  #include <stdlib.h>
  #include <stdio.h>
  #define LOOP 2999999
  #define LOOPTAIL "(tail 2999999 0)"
#endif

#include <string.h>
#include <stdarg.h>

// TODO: use pointer to store some tag data, use this for exteded types
// last bits (3 as it allocates in at least 8 bytes boundaries):
// ----------
// 000 = normal pointer, generic extended lisp data/struct - DONE
// 001 = integer << 1 - DONE
// -- byte[8] lispheap[MAX_HEAP], then still need byte[8] lisptag[MAX_HEAP]
// 010 = lispheap, cons == 8 bytes, 2 cells
// 100 = lispheap, atom == name + primptr, not same as value
// 110 = 

// inline atoms? 32 bits = 6 * 5 bits = 30 bits = 6 "chars" x10, but then atom cannot have ptr

// TODO: move all defs into this:
#include "lisp.h"

// use for list(mkint(1), symbol("foo"), mkint(3), END);
lisp END = (lisp) -1; 

// global symbol variables, set in lispinit()
lisp nil = NULL;
lisp t = NULL;
lisp LAMBDA = NULL;

// if non nil enables continous GC
int dogc = 0;

#define string_TAG 1
typedef struct {
    char tag;
    char xx;
    short index;

    char* p; // TODO: make it inline, not second allocation
} string;

// conss name in order to be able to have a function named 'cons()'
#define conss_TAG 2
typedef struct {
    char tag;
    char xx;
    short index;

    lisp car;
    lisp cdr;
} conss;

// TODO: store inline in pointer
#define intint_TAG 3
typedef struct {
    char tag;
    char xx;
    short index;

    int v;
} intint; // TODO: handle overflow...

#define prim_TAG 4
typedef struct {
    char tag;
    char xx;
    short index;

    signed char n; // TODO: maybe could use xx tag above?
    void* f;
    char* name; // TODO should be char name[1]; // inline allocation!, or actually should point to an ATOM
} prim;

// TODO: somehow an atom is very similar to a conss cell.
// it has two pointers, next/cdr, diff is first pointer points a naked string/not lisp string. Maybe it should?
// TODO: if we make this a 2-cell or 1-cell lisp? or maybe atoms should have no property list or value, just use ENV for that
#define atom_TAG 5
typedef struct atom {
    char tag;
    char xx;
    short index;

    struct atom* next;
    char* name; // TODO should be char name[1]; // inline allocation!
} atom;

// Pseudo closure that is returned by if/progn and other construct that takes code, should handle tail recursion
#define thunk_TAG 6
typedef struct thunk {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    // This needs be same as immediate
} thunk;

#define immediate_TAG 7
typedef struct immediate {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    // This needs be same as thunk
} immediate;

#define func_TAG 8

typedef struct func {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    // This needs be same as thunk
} func;

#define MAX_TAGS 16
int tag_count[MAX_TAGS] = {0};
int tag_bytes[MAX_TAGS] = {0};
char* tag_name[MAX_TAGS] = { "TOTAL", "string", "cons", "int", "prim", "atom", "thunk", "immediate", "func", 0 };
int tag_size[MAX_TAGS] = { 0, sizeof(string), sizeof(conss), sizeof(intint), sizeof(prim), sizeof(thunk), sizeof(immediate), sizeof(func) };

// essentially total number of cons+atom+prim
// TODO: remove ATOM since they are never GC:ed! (thunk are special too, not tracked)
//#define MAX_ALLOCS 1024
#define MAX_ALLOCS 512 // keeps 17K free
//#define MAX_ALLOCS 256 // keeps 21K free

int allocs_count = 0;
void* allocs[MAX_ALLOCS] = { 0 };
unsigned int used[MAX_ALLOCS/32 + 1] = { 0 };
    
#define SET_USED(i) ({int _i = (i); used[_i/32] |= 1 << _i%32;})
#define IS_USED(i) ({int _i = (i); (used[_i/32] >> _i%32) & 1;})

#define INTP(x) (((unsigned int)x) & 1)
#define GETINT(x) (((unsigned int)x) >> 1)
#define MKINT(i) ((lisp)(((i) << 1) + 1))

//#define TAG(x) (x ? ((lisp)x)->tag : 0 )
#define TAG(x) (INTP(x) ? intint_TAG : (x ? ((lisp)x)->tag : 0 ))

#define ALLOC(type) ({type* x = myMalloc(sizeof(type), type ## _TAG); x->tag = type ## _TAG; x;})
#define ATTR(type, x, field) ((type*)x)->field
#define IS(x, type) (x && TAG(x) == type ## _TAG)

void reportAllocs();

int gettag(lisp x) {
    return TAG(x);
    return x->tag;
}

// any slot with no value/nil can be reused
int reuse_pos = 0;
int reuse() {
    int n = allocs_count;
    while(n--) {
        if (!allocs[reuse_pos]) return reuse_pos;
        reuse_pos++;
        if (reuse_pos >= allocs_count) reuse_pos = 0;
    }
    return -1;
}

// total number of things in use
int used_count = 0;

// SLOT salloc/sfree, reuse mallocs of same size instead of free, saved 20% speed
#define SALLOC_MAX_SIZE 32
void* alloc_slot[SALLOC_MAX_SIZE] = {0}; // TODO: probably too many sizes...

void* salloc(int bytes) {
    if (bytes > SALLOC_MAX_SIZE) return malloc(bytes);
    void** p = alloc_slot[bytes];
    if (!p) return malloc(bytes);
    alloc_slot[bytes] = *p;
    return p;
}

void sfree(void** p, int bytes) {
    if (bytes > SALLOC_MAX_SIZE) return free(p);
    void* n = alloc_slot[bytes];
    *p = n;
    alloc_slot[bytes] = p;
}

// call this malloc using ALLOC(typename) macro
void* myMalloc(int bytes, int tag) {
    used_count++;
    tag_count[tag]++;
    tag_bytes[tag] += bytes;
    tag_count[0]++;
    tag_bytes[0] += bytes;

    // use for heap debugging, put in offending addresses
    //if (allocs_count == 269) { printf("\n==============ALLOC: %d bytes of tag %s ========================\n", bytes, tag_name[tag]); }
    //if ((int)p == 0x08050208) { printf("\n============================== ALLOC trouble pointer %d bytes of tag %d %s ===========\n", bytes, ag, tag_name[tag]); }

    void* p = salloc(bytes);

    // thunk optimization, they are given back shortly after created so need to be kept track of
    if (tag == immediate_TAG) {
        ((lisp)p)->index = -1;
        return p;
    }

    int pos = reuse();
    if (pos < 0) {
        pos = allocs_count;
        allocs_count++;
    }

    allocs[pos] = p;
    ((lisp)p)->index = pos;

    if (allocs_count >= MAX_ALLOCS) {
        printf("Exhaused myMalloc array!\n");
        reportAllocs();
        #ifdef TEST
          exit(1);
        #endif
    }
    return p;
}

void mark_clean() {
    memset(used, 0, sizeof(used));
}

// set to 1 to get GC tracing messages
int traceGC = 0;

int needGC() {
    return (used_count < MAX_ALLOCS * 0.8) ? 0 : 1;
}

void gc() {
    if (!needGC()) return; // not yet

    int count = 0;
    if (traceGC) printf(" [GC...");
    int i ;
    for(i = 0; i < allocs_count; i++) {
        lisp p = allocs[i];
        if (!p) continue;
        
        // USE FOR DEBUGGING SPECIFIC PTR
        //if ((int)p == 0x0804e528) { printf("\nGC----------------------%d ERROR! p=0x%x  ", i, p); princ(p); terpri(); }

        if (TAG(p) > 8 || TAG(p) == 0) {
            printf("\nGC----------------------%d ILLEGAL TAG! %d p=0x%x  ", i, TAG(p), (unsigned int)p); princ(p); terpri();
        }
        int u = (used[i/32] >> i%32) & 1;
        if (u) {
            // printf("%d used=%d  ::  ", i, u); princ(p); terpri();
        } else {
            count++;
            if (1) {
                sfree((void*)p, tag_size[TAG(p)]);;
            } else {
                printf("FREE: %d ", i); princ(p); terpri();
                // simulate free
                p->tag = 66;
            }
            allocs[i] = NULL;
            used_count--;
        }
    }
    mark_clean();
    if (traceGC) printf("freed %d used=%d] ", count, used_count);
}

void reportAllocs() {
    // gc();
    terpri();
    printf("--- Allocation stats ---\n");
    int i;
    for(i = 0; i<16; i++) {
        if (tag_count[i] > 0)
            printf("%7s: %3d allocations of %5d bytes\n", tag_name[i], tag_count[i], tag_bytes[i]);
        tag_count[i] = 0;
        tag_bytes[i] = 0;
    }
}

// make a string from POINTER (inside other string) by copying LEN bytes
lisp mklenstring(char *s, int len) {
    string* r = ALLOC(string);
    r->p = strndup(s, len);
    return (lisp)r;
}

lisp mkstring(char *s) {
    string* r = ALLOC(string);
    r->p = s;
    return (lisp)r;
}

// macros car/cdr save 5%
lisp carr(lisp x) { return x && IS(x, conss) ? ATTR(conss, x, car) : nil; }
#define car(x) ({ lisp _x = (x); _x && IS(_x, conss) ? ATTR(conss, _x, car) : nil; })

lisp cdrr(lisp x) { return x && IS(x, conss) ? ATTR(conss, x, cdr) : nil; }
#define cdr(x) ({ lisp _x = (x); _x && IS(_x, conss) ? ATTR(conss, _x, cdr) : nil; })

lisp cons(lisp a, lisp b) {
    conss* r = ALLOC(conss);
    r->car = a;
    r->cdr = b;
    return (lisp)r;
}

lisp setcar(lisp x, lisp v) { return IS(x, conss) ? ATTR(conss, x, car) = v : nil; return v; }
lisp setcdr(lisp x, lisp v) { return IS(x, conss) ? ATTR(conss, x, cdr) = v : nil; return v; }

lisp list(lisp first, ...) {
    va_list ap;
    lisp r = nil;
    // points to cell where cdr is next pos
    lisp last = r;
    lisp x;

    va_start(ap, first);
    for (x = first; x != (lisp)-1; x = va_arg(ap, lisp)) {
        putchar('['); princ(x); putchar(']');
        lisp nw = cons(x, nil);
        if (!r) r = nw;
        setcdr(last, nw);
        last = nw;
    }
    va_end(ap);

    return r;
}

lisp mkint(int v) {
    return MKINT(v);

    // TODO: add "bigint" or minusint...
    intint* r = ALLOC(intint);
    r->v = v;
    return (lisp)r;
}

int getint(lisp x) {
    return INTP(x) ? GETINT(x) : 0;

    // TODO: add "bigint" or minusint...
    return IS(x, intint) ? ATTR(intint, x, v) : 0;
}

lisp mkprim(char* name, int n, void *f) {
    prim* r = ALLOC(prim);
    r->name = name;
    r->n = n;
    r->f = f;
    return (lisp)r;
}

lisp eval(lisp e, lisp env);
lisp eq(lisp a, lisp b);

// lookup binding of atom variable name (not work for int names)
lisp assoc(lisp name, lisp env) {
    while (env) {
        lisp bind = car(env);
        // only works for atom
        if (car(bind)==name) return bind; 
        // TODO: this is required for for example integer if BIGINT
        // if (eq(car(bind), name)) return bind;
        env = cdr(env);
    }
    return nil;
}

lisp evallist(lisp e, lisp env) {
    if (!e) return e;
    // TODO: don't recurse!
    return cons(eval(car(e), env), evallist(cdr(e), env));
}

lisp primapply(lisp ff, lisp args, lisp env, lisp all) {
    //printf("PRIMAPPLY "); princ(ff); princ(args); terpri();
    int n = ATTR(prim, ff, n);
    int an = abs(n);

    if (n == 2) { // eq/plus etc
        lisp (*fp)(lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env), eval(car(cdr(args)), env));
    }
    if (n == -3) { // if...
        lisp (*fp)(lisp,lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(env, car(args), car(cdr(args)), car(cdr(cdr(args))));
    }
    if (n == 1) {
        lisp (*fp)(lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env));
    }
    if (n == 3) {
        lisp (*fp)(lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env), eval(car(cdr(args)), env), eval(car(cdr(cdr(args))),env));
    }
    if (n == -16) { // lambda, quite uncommon
        lisp (*fp)(lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(env, args, all);
    }
    // don't do evalist, but allocate array, better for GC
    if (an > 0 && an <= 4) {
        if (n < 0) an++; // add one for neval and initial env
        lisp argv[an];
        int i;
        for(i = 0; i < n; i++) {
            // if noeval, put env first
            if (i == 0 && n < 0) { 
                argv[0] = env;
                continue;
            }
            lisp a = car(args);
            if (a && n > 0) a = eval(a, env);
            argv[i] = a;
            args = cdr(args);
        }
        lisp (*fp)() = ATTR(prim, ff, f);
        switch (an) {
        case 1: return fp(argv[0]);
        case 2: return fp(argv[0], argv[1]);
        case 3: return fp(argv[0], argv[1], argv[2]);
        case 4: return fp(argv[0], argv[1], argv[2], argv[3]);
        }
    }
    // above is all optimiziations

    // prepare arguments
    if (n >= 0) {
        args = evallist(args, env);
    } else if (n > -16) { // -1 .. -15 no-eval lambda, put env first
        args = cons(env, args);
    }

    lisp r;
    if (abs(n) == 16) {
        lisp (*fp)(lisp, lisp, lisp) = ATTR(prim, ff, f);
        r = fp(env, args, all);
    } else {
        lisp a = args, b = cdr(a), c = cdr(b), d = cdr(c), e = cdr(d), f = cdr(e), g = cdr(f), h = cdr(g), i = cdr(h), j = cdr(i);
        // with C calling convention it's ok, but maybe not most efficient...
        lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = ATTR(prim, ff, f);
        r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    }

    return r;
}

lisp symbol_list = NULL;

// don't call this directly, call symbol
lisp secretMkAtom(char* s) {
    atom* r = ALLOC(atom);
    r->name = s;
    // link it in first
    r->next = (atom*)symbol_list;
    symbol_list = (lisp)r;
    return (lisp)r;
}

lisp find_symbol(char *s, int len) {
    atom* cur = (atom*)symbol_list;
    while (cur) {
        if (strncmp(s, cur->name, len) == 0) return (lisp)cur;
        cur = cur->next;
    }
    return NULL;
}

// linear search to intern the string
// will always return same atom
lisp symbol(char* s) {
    lisp sym = find_symbol(s, strlen(s));
    if (sym) return sym;
    return secretMkAtom(s);
}

// create a copy of partial string if not found
lisp symbolCopy(char* start, int len) {
    lisp sym = find_symbol(start, len);
    if (sym) return sym;
    return secretMkAtom(strndup(start, len));
}

// TODO: not used??? this can be used to implement generators
lisp mkthunk(lisp e, lisp env) {
    thunk* r = ALLOC(thunk);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

// an immediate is a continuation returned that will be called by eval directly to yield a value
// this implements continuation based evaluation thus maybe allowing tail recursion...
// these are used to avoid stack growth on self/mutal recursion functions
// if, lambda, progn etc return these instead of calling eval on the tail
lisp mkimmediate(lisp e, lisp env) {
    immediate* r = ALLOC(immediate); //(thunk*)mkthunk(e, env); // inherit from func_TAG
    //r->tag = immediate_TAG;
    r->e = e;
    r->env = env;
    return (lisp)r;
}


// these are formed by evaluating a lambda
lisp mkfunc(lisp e, lisp env) {
    func* r = ALLOC(func);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

////////////////////////////// GC

void mark_deep(lisp next, int deep) {
    while (next) {
        if (INTP(next)) return;

        // optimization, we store index position in element
        int index = next->index;
        if (index < 0) return; // no tracked here

        lisp p = allocs[index];
        if (!p || p != next) {
            printf("mark_deep.ERROR: index %d doesn't contain pointer.\n", index);
            //printf("pppp = 0x%u and next = 0x%u \n", p, next);
            /* princ(next); */
            /* return; */
        } 
        
        if (IS_USED(index)) return;

        SET_USED(index);
        //printf("Marked %i deep %i :: ", index, deep); princ(p); terpri();
            
        // follow pointers, recurse on one end, or set next for continue
        if (IS(p, atom)) {
            next = (lisp)ATTR(atom, (void*)p, next);
        } else if (IS(p, conss)) {
            mark_deep(car(p), deep+1);
            next = cdr(p);
        } else if (IS(p, thunk) || IS(p, immediate) || IS(p, func)) {
            mark_deep(ATTR(thunk, p, e), deep+1);
            next = ATTR(thunk, p, env);
        }
    }
}

void mark(lisp x) {
    mark_deep(x, 1);

    mark_deep(nil, 1);
    mark_deep(LAMBDA, 1);

    // TODO: remove these from array?
    mark_deep(symbol_list, 0); // never deallocate atoms!!! 
}

///--------------------------------------------------------------------------------
// Primitives

lisp plus(lisp a, lisp b) { return mkint(getint(a) + getint(b)); }
lisp minus(lisp a, lisp b) { return mkint(getint(a) - getint(b)); }
lisp times(lisp a, lisp b) { return mkint(getint(a) * getint(b)); }
lisp divide(lisp a, lisp b) { return mkint(getint(a) / getint(b)); }
lisp lessthan(lisp a, lisp b) { return getint(a) < getint(b) ?  t : nil; }
lisp terpri() { printf("\n"); return nil; }
lisp eq(lisp a, lisp b) {
    if (a == b) return t;
    char ta = TAG(a);
    char tb = TAG(b);
    if (ta != tb) return nil;
    // only int needs to be eq with other int even if on heap...
    if (ta != intint_TAG) return nil;
    if (getint(a) == getint(b)) return t;
    return nil;
}
lisp equal(lisp a, lisp b) { return eq(a, b) ? t : symbol("*EQUAL-NOT-DEFINED*"); }
//lisp setQQ(lisp name, lisp v, lisp env) {
//    lisp nenv = cons( cons(name, v), env );
//    setcdr(nenv, FAC); // create circular dependency on it's own defininition symbol by redefining
//    return nenv;
//}
lisp setq(lisp name, lisp v, lisp env) {
    lisp bind = assoc(name, env);
    if (!bind) {
        bind = cons(name, nil);
        env = cons(bind, env);
    }
    v = eval(v, env); // evaluate using new env containing right env with binding to self
    setcdr(bind, v); // create circular dependency on it's own defininition symbol by redefining
    return env;
}

///--------------------------------------------------------------------------------
// lisp reader

static char *input = NULL;
static char nextChar = 0;

static char nextx() {
    if (nextChar != 0) {
        char c = nextChar;
        nextChar = 0;
        return c;
    }
    if (!input) return 0;
    if (!*input) {
        // TODO: leak? ownership of pointers
        input = NULL;
        return 0;
    }
    return *(input++);
}

static char next() {
    if (0) {
        char c = nextx();
        printf(" [next == %d] ", c);
        return c;
    } else {
        return nextx();
    }
}

static void skipSpace() {
    char c = next();
    while (c && c == ' ') c = next();
    nextChar = c;
}

// TODO: negative number
static lisp readInt() {
    char c = next();
    int v = 0;
    if (!c) return nil;
    while (c && c >= '0' && c <= '9') {
        v = v*10 + c-'0';
        c = next();
    }
    nextChar = c;
//    printf("MKINT=%d\n", v); 
    return mkint(v);
}

static lisp readString() {
    char* start = input;
    char c = next();
    while (c && c != '"') c = next();
    
    int len = input - start - 1;
    return mklenstring(start, len);
}

static lisp readAtom() {
    // TODO: broken if ungetc used before...?
    char* start = input - 1;
    int len = 0;
    char c = next();
    while (c && c!='(' && c!=')' && c!=' ' && c!='.') {
        c = next();
        len++;
    }
    nextChar = c;
    return symbolCopy(start, len);
}

static lisp readx();

static lisp readList() {
    skipSpace();

    char c = next();
    if (c == ')') return nil;
    if (c == '(') {
        lisp ax = readList();
        lisp dx = readList();
        return cons(ax, dx);
    }
    nextChar = c;

    lisp a = readx();
    skipSpace();
    c = next(); if (c == '.') next(); else nextChar = c;
    skipSpace();
    return cons(a, readList());
}

static lisp readx() {
    skipSpace();
    char c = next();
    if (!c) return NULL;

    if (c == '(') {
        return readList();
    } else if (c == ')') {
        return nil;
    } else if (c >= '0' && c <= '9') {
        nextChar = c;
        return readInt();
    } else if (c == '"') {
        return readString();
    } else {
        nextChar = c;
        return readAtom();
    }
}

lisp read(char *s) {
    input = s;
    nextChar = 0;
    return readx();
}

// TODO: prin1 that quotes so it/strings can be read back again
// print that prin1 with newline
lisp princ(lisp x) {
    if (x == nil) {
        printf("nil");
        return x;
    }
    int tag = TAG(x);
    // simple one liners
    if (tag == string_TAG) printf("%s", ATTR(string, x, p));
    else if (tag == intint_TAG) printf("%d", getint(x));
    else if (tag == prim_TAG) printf("#%s", ATTR(prim, x, name));
    else if (tag == atom_TAG) printf("%s", ATTR(atom, x, name));
    else if (tag == thunk_TAG) { printf("#thunk["); princ(ATTR(thunk, x, e)); putchar(']'); }
    else if (tag == immediate_TAG) { printf("#immediate["); princ(ATTR(thunk, x, e)); putchar(']'); }
    else if (tag == func_TAG) { printf("#func["); princ(ATTR(thunk, x, e)); putchar(']'); }
    // longer blocks
    else if (tag == conss_TAG) {
        putchar('(');
        princ(car(x));
        lisp d = cdr(x);
        while (d && gettag(d) == conss_TAG) {
            putchar(' ');
            princ(car(d));
            d = cdr(d);
        }
        if (d) {
            putchar(' '); putchar('.'); putchar(' ');
            princ(d);
        }
        putchar(')');
    } else {
        printf("*UnknownTag:%d*", tag);
    }
    // is need on esp, otherwise it's buffered and comes some at a time...
    // TODO: check performance implications
    fflush(stdout); 
    return x;
}

static void indent(int n) {
    n *= 2;
    while (n-- > 0) putchar(' ');
}

static int level = 0;

static lisp funcapply(lisp f, lisp args, lisp env);

lisp evalGC(lisp e, lisp env);

static lisp eval_hlp(lisp e, lisp env) {
    if (!e) return e;
    char tag = TAG(e);
    if (tag == atom_TAG) {
        lisp v = assoc(e, env); // look up variable
        if (v) return cdr(v);
        printf("Undefined symbol: "); princ(e); terpri();
        return nil;
    }
    if (tag != conss_TAG) return e;

    // find function
    lisp orig = car(e);
    lisp f = orig;
    tag = TAG(f);
    while (f && tag!=prim_TAG && tag!=thunk_TAG && tag!=func_TAG && tag!=immediate_TAG) {
        f = evalGC(f, env);
        tag = TAG(f);
    }
    if (f != orig) {
        // "macro expansion" lol (replace with implementation)
        // TODO: not safe if found through variable (like all!)
        // TODO: keep on atom ptr to primitive function/global, also not good?
        // DEF(F,...) will then break many local passed variables
        // maybe must search all list till find null, then can look on symbol :-(
        // but that's everytime? actually, not it's a lexical scope!
        // TODO: only replace if not found in ENV and is on an ATOM!
        setcar(e, f);
    }

    if (tag == prim_TAG) return primapply(f, cdr(e), env, e);
    if (tag == func_TAG) return funcapply(f, cdr(e), env);
    if (tag == thunk_TAG) return f; // ignore args

    printf("%%ERROR.lisp - don't know how to evaluate f="); princ(f); printf("  ");
    princ(e); printf(" ENV="); princ(env); terpri();
    return nil;
}

static void mymark(lisp x) {
    if (dogc) mark(x);
}

static void mygc() {
    if (dogc) gc();
}

static int blockGC = 0;

// this is a safe eval to call from anywhere, it will not GC
lisp eval(lisp e, lisp env) {
    blockGC++;
    lisp r = evalGC(e, env);
    blockGC--;
    return r;
}

static lisp stack[64];

static int trace = 0;

lisp evalGC(lisp e, lisp env) {
    if (!e) return e;
    char tag = TAG(e);
    // look up variable
    if (tag == atom_TAG) {
        lisp v = assoc(e, env); 
        if (v) return cdr(v);
        printf("Undefined symbol: "); princ(e); terpri();
        return nil;
    }
    if (tag != atom_TAG && tag != conss_TAG && tag != thunk_TAG) return e;

    if (level >= 64) {
        printf("You're royally screwed! why does it still work?\n");
        #ifdef TEST
          exit(1);
        #endif
        return nil;
    }

    stack[level] = e;

    // TODO: move this to function
    if (!blockGC && needGC()) {
        mymark(env);
        if (trace) printf("%d STACK: ", level);
        int i;
        for(i=0; i<64; i++) {
            if (!stack[i]) break;
            mymark(stack[i]);
            if (trace) {
                printf(" %d: ", i);
                princ(stack[i]);
            }
        }
        if (trace) terpri();
        mygc(); 
    }

    if (trace) { indent(level); printf("---> "); princ(e); terpri(); }
    level++;
    if (trace) { indent(level+1); printf(" ENV= "); princ(env); terpri(); }

    lisp r = eval_hlp(e, env);
    while (r && TAG(r) == immediate_TAG) {
        lisp tofree = r;
        r = eval_hlp(ATTR(thunk, r, e), ATTR(thunk, r, env));
        // immediates are immediately consumed after evaluation, so they can be free:d directly
        tofree->tag = 0;
        sfree((void*)tofree, sizeof(thunk));
        used_count--; // TODO: move to sfree?
    }

    --level;
    if (trace) { indent(level); princ(r); printf(" <--- "); princ(e); terpri(); }
    stack[level] = nil;
    return r;
}

lisp iff(lisp env, lisp exp, lisp thn, lisp els) {
    // evalGC is safe here as we don't construct any structes, yet
    // TODO: how did we get here? primapply does call evallist thus created something...
    // but we pass ENV on so it should be safe..., it'll mark it!
    return evalGC(exp, env) ? mkimmediate(thn, env) : mkimmediate(els, env);
}

// essentially this is a quote but it stores the environment so it's a closure!
lisp lambda(lisp env, lisp all) {
    return mkfunc(all, env);
}

// use bindEvalList unless NLAMBDA
lisp bindlist(lisp fargs, lisp args, lisp env) {
    // TODO: not recurse!
    if (!fargs) return env;
    lisp b = cons(car(fargs), car(args));
    return bindlist(cdr(fargs), cdr(args), cons(b, env));
}

lisp bindEvalList(lisp fargs, lisp args, lisp env, lisp extend) {
    while (fargs) {
        lisp b = cons(car(fargs), eval(car(args), env));
        extend = cons(b, extend);
        fargs = cdr(fargs);
        args = cdr(args);
    }
    return extend;
}

// TODO: nlambda?
lisp funcapply(lisp f, lisp args, lisp env) {
    lisp lenv = ATTR(thunk, f, env);
    lisp l = ATTR(thunk, f, e);
    //printf("FUNCAPPLY:"); princ(f); printf(" body="); princ(l); printf(" args="); princ(args); printf(" env="); princ(lenv); terpri();
    lisp fargs = car(l); // skip #lambda // TODO: check if NLAMBDA!
    lenv = bindEvalList(fargs, args, env, lenv);
    lisp prog = car(cdr(l)); // skip #lambda (...) TODO: implicit PROGN? how to do?
    return mkimmediate(prog, lenv);
}

// User, macros, assume a "globaL" env variable implicitly, and updates it
#define SETQ(sname, val) env = setq(symbol(#sname), val, env)
#define DEF(fname, sbody) env = setq(symbol(#fname), read(#sbody), env)
//#define EVAL(what) ({ eval(read(#what), env); terpri(); }) // TODO: no good!
#define PRIM(fname, argn, fun) env = setq(symbol(#fname), mkprim(#fname, argn, fun), env)

// returns an env with functions
lisp lispinit() {
    lisp env = nil;

    // free up and start over...
    dogc = 0;

    // TODO: this is a leak!!!
    allocs_count = 0;

    mark_clean();
    mark(nil);
    gc();

    t = symbol("t");
    // nil = symbol("nil"); // LOL? TODO:? that wouldn't make sense? then it would be taken as true!
    LAMBDA = mkprim("lambda", -16, lambda);

    SETQ(lambda, LAMBDA);
    PRIM(+, 2, plus);
    PRIM(-, 2, minus);
    PRIM(*, 2, times);
    // PRIM("/", 2, divide);
    PRIM(eq, 2, eq);
    PRIM(=, 2, eq);
    PRIM(<, 2, lessthan);
    PRIM(if, -3, iff);
    PRIM(terpri, 0, terpri);
    PRIM(princ, 1, princ);

    PRIM(cons, 2, cons);
    PRIM(car, 1, carr);
    PRIM(cdr, 1, cdrr);
    PRIM(setcar, 2, setcar);
    PRIM(setcdr, 2, setcdr);
    PRIM(assoc, 2, assoc);
    // PRIM(quote, -16, quote);
    // PRIM(list, 16, listlist);

    PRIM(eval, -1, eval);
    PRIM(evallist, 2, evallist);

    PRIM(read, 1, read);

    // setq
    // define
    // defun
    // defmacro
    // while
    // gensym

    // another small lisp in 1K lines
    // - https://github.com/rui314/minilisp/blob/master/minilisp.c

    return env;
}

 void tread(char* s) {
    printf("\nread-%s: ", s);
    princ(read(s));
    terpri();
}
    
void newLispTest(lisp env) {
    dogc = 1;

    printf("ENV= "); princ(env); terpri();
    princ(read("(foo bar 42)")); terpri();
    princ(mkint(3)); terpri();
    princ(read("4711")); terpri();
    princ(read("303")); terpri();
    princ(eval(read("3"), env)); terpri();
    princ(evalGC(read("33"), env)); terpri();
    //princ(eval(read("(333)"), env)); terpri(); // TODO: crashes!!!???

    // TODO: should nil be a symbol? grrr, eval to nil, no better not because (x) will be true!
    // TODO: means reader should be special...
    princ(evalGC(read("nil"), env)); terpri();
    // TODO: should t be a symbol? grrr, eval to t?
    princ(evalGC(read("t"), env)); terpri();
    princ(read("(+ 333 444)")); terpri();
    princ(eval(read("(+ 33 44)"), env)); terpri();

    printf("\n\n----------------------TAIL OPT AA BB!\n");
    DEF(bb, (lambda (b) (+ b 3)));
    DEF(aa, (lambda (a) (bb a)));
    printf("\nTEST 10="); princ(eval(read("(aa 7)"), env)); terpri();

    printf("\n\n----------------------TAIL RECURSION!\n");
    printf("1====\n");

    DEF(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
    printf("2====\n");
    printf("\nTEST %d=", LOOP); princ(evalGC(read(LOOPTAIL), env)); terpri();

    mark(env); // TODO: move into GC()
    gc();
}

void lisptest(lisp env) {
    printf("------------------------------------------------------\n");
    newLispTest(env);
    return;

    printf("\n---string: "); princ(mkstring("foo"));
    printf("\n---int: "); princ(mkint(42));
    printf("\n---cons: "); princ(cons(mkstring("bar"), mkint(99)));
    printf("\n");
    printf("\nread1---string: "); princ(mklenstring("bar", 3));
    printf("\nread2---string: "); princ(mklenstring("bar", 2));
    printf("\nread3---string: "); princ(mklenstring("bar", 1));
    printf("\nread4---read.string: "); princ(read("\"bar\""));
    printf("\nread5---atom: "); princ(read("bar"));
    printf("\nread6---int: "); princ(read("99"));
    printf("\nread7---cons: "); princ(read("(bar . 99)"));
    printf("\nread8---1: "); princ(read("1"));
    printf("\nread9---12: "); princ(read("12"));
    tread("123");
    tread("()");
    tread("(1)");
    tread("(1 2)");
    tread("(1 2 3)");
    tread("((1) (2) (3))");
    tread("(lambda (n) if (eq n 0) (* n (fac (- n 1))))");
    tread("(A)");
    tread("(A B)");
    tread("(A B C)");
    printf("\n3=3: "); princ(eq(mkint(3), mkint(3)));
    printf("\n3=4: "); princ(eq(mkint(3), mkint(4)));
    printf("\na=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\na=b: "); princ(eq(symbol("a"), symbol("b")));
    printf("\na=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\n");

    lisp plu = mkprim("plus", 2, plus);
    lisp tim = mkprim("times", 2, times);
    lisp pp = cons(plu, cons(mkint(3), cons(mkint(4), nil)));
    lisp tt = cons(tim, cons(mkint(3), cons(mkint(4), nil)));

    printf("\neval-a: ");
    lisp a = symbol("a");
    env = cons(cons(a, mkint(5)), nil);;
    princ(eval(a, env));

    printf("\nchanged neval-a: ");
    princ(eval(a, cons(cons(a, mkint(77)), env)));

    printf("\nTHUNK-----");
    princ(mkthunk(mkint(14), nil));
    printf(" ----> ");
    princ(eval(cons(mkthunk(pp, nil), nil), nil));
    // it has "lexical binding", lol
    princ(eval(cons(mkthunk(a, env), nil), env));

    lisp IF = mkprim("if", -3, iff);
    printf("\n\n--------------IF\n");
    eval(IF, nil); terpri();
    eval(cons(IF, cons(mkint(7), cons(pp, cons(tt, nil)))), nil);
    eval(cons(IF, cons(nil, cons(pp, cons(tt, nil)))), nil);

    lisp LA = mkprim("lambda", -16, lambda);
    printf("\n\n--------------LAMBDA\n");
    eval(LA, nil); terpri();
    eval(list(LA, mkint(7), END), nil);
    lisp la = symbol("lambda");
    lisp lenv = list(cons(la, LA), END);
    lisp l = list(LA, list(symbol("n"), END),
                  list(plu, mkint(39), symbol("n"), END),
                  END);
    l = eval(l, lenv);
    eval(list(l, mkint(3), END), lenv); // looking up la giving LA doesn't work?

    lisp n = symbol("n");
    lisp EQ = mkprim("eq", 2, eq);
    lisp minuus = mkprim("minus", 2, minus);
    lisp facexp = list(EQ, n, mkint(0), END);
    lisp facthn = mkint(1);
    lisp fc = symbol("fac");
    lisp facrec = list(fc, list(minuus, n, mkint(1), END), END);
    lisp facels = list(tim, n, facrec, END);
    printf("\nfacels="); princ(facels); terpri();
    lisp facif = list(IF, facexp, facthn, facels, END);
    lisp fac = list(LA, list(n, END), facif, END);
    printf("FACCC="); princ(fac); terpri();
    lisp fenv = setq(fc, fac, lenv);
    printf("ENVENVENV=="); princ(fenv); terpri();
//    lisp fenv = cons( cons(symbol("fac"), mkint(99)),
//                      lenv);
//    lisp FAC = eval(fac, fenv);
//    lisp facbind = assoc(fc, fenv);
//    setcdr(facbind, FAC); // create circular dependency on it's own defininition symbol by redefining

    eval(list(fc, mkint(6), END), fenv);

    princ(list(nil, mkstring("fihs"), mkint(1), symbol("fish"), mkint(2), mkint(3), mkint(4), nil, nil, nil, END));

    mark(nil); gc();
    mark(nil); gc();
    printf("AFTER GC!\n");

    if (0) {
        dogc = 1;
        lisp tl = symbol("tail");
        lisp s = symbol("s");

        LA = mkprim("lambda", -16, lambda);
        EQ = mkprim("eq", 2, eq);
        IF = mkprim("if", -3, iff);
        minuus = mkprim("minus", 2, minus);
        plu = mkprim("plus", 2, plus);
        tim = mkprim("times", 2, times);

        lenv = list(cons(la, LA), END);

        fenv = lenv;

        lisp tail = list(LA, list(n, s, END),
                         list(IF, list(EQ, n, mkint(0), END),
                              s,
                              list(tl, list(minuus, n, mkint(1), END), list(plu, s, mkint(1), END)), END),
                         END);
        lisp tailenv = setq(tl, tail, fenv);
        printf("\n\n----------------------TAIL RECURSION!\n");
        eval(list(tail, mkint(900), mkint(0), END), tailenv);
    
        printf("\n\n----------------------TAIL RECURSION!\n");
        lisp aa = symbol("aa");
        lisp bb = symbol("bb");
        lisp BB = list(LA, list(n, END), list(plu, n, mkint(3), END), END);
        lisp aenv = setq(bb, BB, fenv);
        lisp AA = list(LA, list(n, END), list(bb, n, END), END);
        aenv = setq(aa, AA, aenv);
        eval(list(aa, mkint(7), END), aenv);
    
        //eval(read("(lambda (n) (if (eq n 0) 1 (fac (- n 1))))"), lenv);

//    reportAllocs();
//    printf("SIZEOF int = %d\nSIZEOF ptr = %d\n", sizeof(int), sizeof(int*));
    } else {
        newLispTest(env);
    }
    printf("\n========================================================END=====================================================\n");
}
