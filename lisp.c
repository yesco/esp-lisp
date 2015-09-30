/* A mini "lisp machine" */

// Lua time 100,000 => 2s
// ----------------------
//   s=0; for i=1,100000 do s=s+i end; print(s);
//   function tail(n, s) if n == 0 then return s else return tail(n-1, s+1); end end print(tail(100000, 0))

// lisp.c (tail 1000 0)
//   all alloc/evalGC gives 5240ms with print
//   no print gc => 5040ms
//   ==> painful slow!
//   NO INT alloc => 4500ms
//   needGC() function, block mark => 1070ms
//
// lisp.c (tail 10000 0)
//   => 9380, 9920, 10500
//   reuse() looped always from 0, made it round-robin => 3000... 4200... 4000ms!
//   mark_deep() + p->index=i ===> 1500-2000ms
//   car/cdr macro, on desktop 6.50 -> 4.5s for for 1M loop for esp => 1400-1800
//   hardcode primapply 2 parameters => 1100ms
//   hardcode primapply 1,2,3,-3 (if),-16 => 860-1100ms
//   slot alloc => 590,600 ms
//
// RAW C esp8266 - printf("10,000,000 LOOP (100x lua) TIME=%d\r\n", tm); ===> 50ms
//

#ifndef TEST
  #include "espressif/esp_common.h"
  //#include "FreeRTOS.h" // just for MEM FREE QUESTION
  #define LOOP 9999
  #define LOOPTAIL "(tail 9999 0)"
#endif

#ifdef TEST
  #include <stdlib.h>
  #include <stdio.h>
  #define LOOP 2999999
  #define LOOPTAIL "(tail 2999999 0)"
#endif

#include <string.h>
#include <stdarg.h>

// 'pretty"
// all lispy symbols are required to start with tag

// TODO: use pointer to store some tag data, use this for exteded types
// last bits:
// ----------
// 00 = integer << 2
// 01 = cons
// 10 = 
// 11 = extended type, remove bits to get pointer assumes 4 byte boundary

// TODO: move all defs into this:
#include "lisp.h"

lisp nil = NULL;
lisp END = (lisp) -1;
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
    signed char n;
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

#define MAX_ALLOCS 1024
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

int used_count = 0;

#define SALLOC_MAX_SIZE 32
void* alloc_slot[SALLOC_MAX_SIZE] = {0};

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

void* myMalloc(int bytes, int tag) {
    used_count++;
    tag_count[tag]++;
    tag_bytes[tag] += bytes;
    tag_count[0]++;
    tag_bytes[0] += bytes;

    //if (allocs_count == 269) { printf("\n==============ALLOC: %d bytes of tag %s ========================\n", bytes, tag_name[tag]); }
    //if (allocs_count == 270) { printf("\n==============ALLOC: %d bytes of tag %d %s ========================\n", bytes, tag, tag_name[tag]); }
    //if ((int)p == 0x08050208) { printf("\n============================== ALLOC trouble pointer %d bytes of tag %d %s ===========\n", bytes, ag, tag_name[tag]); }

    void* p = salloc(bytes);

    // dangerous optimization
    if (tag == immediate_TAG) {
        // do not record, do not GC, they'll be GC:ed automatically as invoked once!
        ((lisp)p)->index = -1;
        return p;
    }
    int pos = reuse();
    if (pos < 0) {
        pos = allocs_count;
        allocs_count++;
    }
    //if ((int)p == 0x0804e528) { printf("\n=POS=%d pointer=0x%x tag %d %s\n", pos, (unsigned int)p, tag, tag_name[tag]); }
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

// you better mark stuff you want to keep first...
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
//            printf("%d used=%d  ::  ", i, u); princ(p); terpri();
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
//    gc();
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
    r->p = malloc(len+1);
    strncpy(r->p, s, len);
    r->p[len] = 0; // make sure!
    return (lisp)r;
}

lisp mkstring(char *s) {
    string* r = ALLOC(string);
    r->p = s;
    return (lisp)r;
}

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
    lisp cur = r;
    lisp x;

    va_start(ap, first);
    for (x = first; x != (lisp)-1; x = va_arg(ap, lisp)) {
        putchar('['); princ(x); putchar(']');
        lisp cx = cons(x, nil);
        if (!r) r = cx;
        setcdr(cur, cx);
        cur = cx;
    }
    va_end(ap);

    return r;
}

lisp mkint(int v) {
    return MKINT(v);
    intint* r = ALLOC(intint);
    r->v = v;
    return (lisp)r;
}

int getint(lisp x) {
    return INTP(x) ? GETINT(x) : 0;
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

// returns binding, so you can change the cdr == value
lisp assoc(lisp name, lisp env) {
    while (env) {
        lisp bind = car(env);
        if (eq(car(bind), name)) return bind;
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

    if (n == 1) {
        lisp (*fp)(lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env));
    }
    if (n == 2) {
        lisp (*fp)(lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env), eval(car(cdr(args)), env));
    }
    if (n == 3) {
        lisp (*fp)(lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(eval(car(args), env), eval(car(cdr(args)), env), eval(car(cdr(cdr(args))),env));
    }
    if (n == -3) {
        lisp (*fp)(lisp,lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(env, car(args), car(cdr(args)), car(cdr(cdr(args))));
    }
    if (n == -16) {
        lisp (*fp)(lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(env, args, all);
    }
    if (an > 0 && an <= 4) {
        lisp argv[n];
        int i;
        for(i = 0; i < n; i++) {
            lisp a = car(args);
            if (a && n > 0) a = eval(a, env);
            argv[i] = a;
            args = cdr(args);
        }
        lisp (*fp)() = ATTR(prim, ff, f);
        switch (n) {
        case 1: return fp(argv[0]);
        case 2: return fp(argv[0], argv[1]);
        case 3: return fp(argv[0], argv[1], argv[2]);
        case 4: return fp(argv[0], argv[1], argv[2], argv[3]);
        }
    }
    
    // normal apply = eval list
    if (n >= 0) {
        args = evallist(args, env);
    } else if (n > -16) { // -1 .. -15
        // no eval/autoquote arguemnts/"macro"
        // calling convention is first arg is current environment
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

lisp oldprimapply(lisp ff, lisp args) {
    lisp a = args, b = cdr(a), c = cdr(b), d = cdr(c), e = cdr(d), f = cdr(e), g = cdr(f), h = cdr(g), i = cdr(h), j = cdr(i);
    lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = ATTR(prim, ff, f);
    // with C calling convention it's ok, but maybe not most efficient...
    lisp r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    princ(cons(ff, args));
    printf(" -> "); princ(r); printf("\n");

    return r;
}

lisp symbol_list = NULL;

lisp mkmkatom(char* s, lisp list) {
    atom* r = ALLOC(atom);
    r->name = s;
    r->next = (atom*)list;
    return (lisp)r;
}

// linear search during 'read'
// TODO: fix if problem...
lisp symbol(char *s) {
    atom* cur = (atom*)symbol_list;
    while (cur) {
        if (strcmp(s, cur->name) == 0) return (lisp)cur;
        cur = cur->next;
    }
    symbol_list = mkmkatom(s, symbol_list);
    return symbol_list;
}

lisp mkthunk(lisp e, lisp env) {
    thunk* r = ALLOC(thunk);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

// an immediate is a continuation returned that will be called by eval directly to yield another value
// this implements continuation based evaluation thus maybe alllowing tail recursion...
lisp mkimmediate(lisp e, lisp env) {
    immediate* r = ALLOC(immediate); //(thunk*)mkthunk(e, env); // inherit from func_TAG
    //r->tag = immediate_TAG;
    r->e = e;
    r->env = env;
    return (lisp)r;
}


lisp mkfunc(lisp e, lisp env) {
    func* r = ALLOC(func); //(thunk*)mkthunk(e, env); // inherit from func_TAG
    //r->tag = func_TAG;
    r->e = e;
    r->env = env;
    return (lisp)r;
}

////////////////////////////// GC

// silly inefficient, as it loops to find pointer in array, lol
// better just store the bit in the tag field?
// or add extra char of data to allocated stuffs
void mark_deep(lisp x, int deep) {
    int i =  0;
    while (i < allocs_count) {
        if (!x) return;
        if (INTP(x)) return;

        int index = x->index;
        if (index > 0) i = index;

        lisp p = allocs[i];
        if (p) {
            if (p == x) {
                if (IS_USED(i)) {
                    //printf("already Marked %i deep %i :: ", i, deep); princ(p); terpri();
                    return; // no need mark again or follow pointers
                }
                SET_USED(i);
                //printf("Marked %i deep %i :: ", i, deep); princ(p); terpri();

                // only atom and conss contains pointers...
                if (IS(p, atom)) {
                    x = (lisp)ATTR(atom, (void*)p, next);
                    i = -1; // start over at 0
                } else if (IS(p, conss)) {
                    mark_deep(car(p), deep+1);
                    // don't recurse on rest, just loop
                    x = cdr(p);
                    i = -1; // start over at 0
                } else if (IS(p, thunk) || IS(p, immediate) || IS(p, func)) {
                    // should we switch? which one is most likely to become deep?
                    mark_deep(ATTR(thunk, p, e), deep+1);
                    x = ATTR(thunk, p, env);
                    i = -1; // start over at 0
                } else return; // found pointer but nothing more to do
            }
        }

        i++;
    }
}

void mark(lisp x) {
    mark_deep(x, 1);
    mark_deep(symbol_list, 0); // never deallocate atoms!!!
    mark_deep(nil, 1);
    mark_deep(LAMBDA, 1);
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
    // only integer is 'atom' needs to be eq that follow pointer
    // TODO: string???
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
    int old = dogc;
    dogc = 0;

    mark(name); mark(v); mark(env); // make sure next eval doesn't remove "v"
    lisp bind = assoc(name, env);
    if (!bind) {
        bind = cons(name, nil);
        env = cons(bind, env);
    }
    v = eval(v, env); // evaluate using new env containing right env with binding to self
    setcdr(bind, v); // create circular dependency on it's own defininition symbol by redefining

    dogc = old;

    return env;
}

#define SETQ(sname, val) env = setq(symbol(#sname), val, env)
//#define DEF(fname, sbody) env = setq(symbol(#fname), read(#sbody), env)
#define DEF(fname, sbody) env = setq(symbol(#fname), read(#sbody), env)
#define EVAL(what, env) eval(read(#what), env)
#define PRIM(fname, argn, fun) env = setq(symbol(#fname), mkprim(#fname, argn, fun), env)

///--------------------------------------------------------------------------------
// lisp reader
char *input = NULL;
char nextChar = 0;

char nextx() {
    if (nextChar != 0) {
        char c = nextChar;
        nextChar = 0;
        return c;
    }
    if (!input) return 0;
    if (!*input) {
        // TODO: leak? ownership of pointers
        free(input);
        input = NULL;
        return 0;
    }
    return *(input++);
}

char next() {
    if (0) {
        char c = nextx();
        printf(" [next == %d] ", c);
        return c;
    } else {
        return nextx();
    }
}

void skipSpace() {
    char c = next();
    while (c && c == ' ') c = next();
    nextChar = c;
}

// TODO: negative number
lisp readInt() {
    char c = next();
    int v = 0;
    if (!c) return nil;
    while (c && c >= '0' && c <= '9') {
        v = v*10 + c-'0';
        c = next();
    }
    nextChar = c;
    return mkint(v);
}

lisp readString() {
    char* start = input;
    char c = next();
    while (c && c != '"') c = next();
    
    int len = input - start - 1;
    return mklenstring(start, len);
}

lisp readAtom() {
    // TODO: broken if ungetc used before...?
    char* start = input - 1;
    int len = 0;
    char c = next();
    while (c && c!='(' && c!=')' && c!=' ' && c!='.') {
        c = next();
        len++;
    }
    nextChar = c;

    return symbol(strndup(start, len));
}

lisp readx();

lisp readList() {
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

lisp readx() {
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
    fflush(stdout);
    return x;
}

void indent(int n) {
    n *= 2;
    while (n-- > 0) putchar(' ');
}

int level = 0;

lisp lambdaapply(lisp f, lisp args, lisp env);
lisp funcapply(lisp f, lisp args, lisp env);

lisp evalGC(lisp e, lisp env);

lisp eval_hlp(lisp e, lisp env) {
    if (!e || (!IS(e, atom) && !IS(e, conss))) return e;
    if (IS(e, atom)) {
        lisp v = assoc(e, env); // look up variable
        if (v) return cdr(v);
        printf("Undefined symbol: "); princ(e); terpri();
        return nil;
    }

    //printf("EVAL FUNC:"); princ(env); terpri();
    lisp orig = car(e);
    lisp f = evalGC(orig, env);
    //printf("NOT FUNC: "); princ(f); terpri();
    while (f && !IS(f, prim) && !IS(f, thunk) && !IS(f, func) && !IS(f, immediate)) {
        f = evalGC(f, env);
        //printf("GOT--: "); princ(f); terpri();
    }
    if (f != orig) {
        // nasty self code modification optimzation (saves lookup)
        // TODO: not safe if found through variable (like all!)
        setcar(e, f);
    }
    if (IS(f, prim)) return primapply(f, cdr(e), env, e);
    if (IS(f, thunk)) return f; // ignore args, higher level can call (evalGC)
    if (IS(f, func)) return funcapply(f, cdr(e), env);
    printf("%%ERROR.lisp - don't know how to evaluate f="); princ(f); printf("  ");
    princ(e); printf(" ENV="); princ(env); terpri();
    return nil;
}

void mymark(lisp x) {
    if (dogc) mark(x);
}

void mygc() {
    if (dogc) gc();
}

int blockGC = 0;

lisp eval(lisp e, lisp env) {
    blockGC++;
    lisp r = evalGC(e, env);
    blockGC--;
    return r;
}

lisp stack[64];

int trace = 0;

lisp evalGC(lisp e, lisp env) {
    if (!e) return e;
    if (!IS(e, atom) && !IS(e, conss)) return e;

    if (level >= 64) {
        printf("You're royally screwed! why does it still work?\n");
        #ifdef TEST
          exit(1);
        #endif
    }

    stack[level] = e;

    if (!blockGC && needGC()) {
        mymark(env); // TODO: important

        // print stack
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
        // TODO: better mark all shit first!
        mygc(); // not safe here, setq for example calls eval, it should not do gc as it builds cons at same time...
        // evallist the same issue, TODO: maybe an eval that doesn't GC? and only gc here and "up"

    }

    if (trace) { indent(level); printf("---> "); princ(e); terpri(); }
    level++;
    if (trace) { indent(level+1); printf(" ENV= "); princ(env); terpri(); }

    lisp r = eval_hlp(e, env);
    while (r && TAG(r) == immediate_TAG) {
        lisp tofree = r;
        r = eval_hlp(ATTR(thunk, r, e), ATTR(thunk, r, env));
        //mark(ATTR(thunk, r, e)); mark(ATTR(thunk, r, env));
        //r = eval(ATTR(thunk, r, e), ATTR(thunk, r, env));
        // immediates are immediately consumed after evaluation, so they can be free:d directly
        // TODO: move into eval_hlp?
        tofree->tag = 0;
        sfree((void*)tofree, sizeof(thunk));
        used_count--;
    }
    --level;
    if (trace) { indent(level); princ(r); printf(" <--- "); princ(e); terpri(); }
    stack[level] = nil;
    return r;
}

void lispF(char* name, int n, void* f) {
    // TODO: do something...
}

lisp iff(lisp env, lisp exp, lisp thn, lisp els) {
    // evalGC is safe here as we don't construct any structes, yet
    return evalGC(exp, env) ? mkimmediate(thn, env) : mkimmediate(els, env);
}

lisp lambda(lisp env, lisp all) {
    // TODO: we just removed lambda and now adding it back,
    // do better calling convention? retain name/full form of no-eval function?
    return mkfunc(all, env);
}

lisp bindlist(lisp fargs, lisp args, lisp env) {
    // TODO: not recurse!
    if (!fargs) return env;
    lisp b = cons(car(fargs), car(args));
    return bindlist(cdr(fargs), cdr(args), cons(b, env));
}

// TODO: nlambda?
lisp funcapply(lisp f, lisp args, lisp env) {
    lisp lenv = ATTR(thunk, f, env);
    lisp l = ATTR(thunk, f, e);
    //printf("FUNCAPPLY:"); princ(f); printf(" body="); princ(l); printf(" args="); princ(args); printf(" env="); princ(lenv); terpri();
    lisp fargs = car(l); // skip #lambda
    args = evallist(args, env);
    lenv = bindlist(fargs, args, lenv);
    lisp prog = car(cdr(l)); // skip #lambda (...) GET IGNORE
    // TODO: implicit progn? loop over cdr...
    //printf("MKIMMEDIATE: "); princ(prog); terpri();    
    return mkimmediate(prog, lenv);
    // this doesn't handle tail recursion, but may be faster?
    return eval(prog, lenv);
}

void lispinit() {
    // free up and start over...
    dogc = 0;

    allocs_count = 0;
    mark_clean();
    int i;
    for(i = 0; i<MAX_ALLOCS; i++) {
        allocs[i] = nil;
    }



    t = symbol("t");
    // nil = symbol("nil"); // LOL? TODO:? that wouldn't make sense?
    LAMBDA = mkprim("lambda", -16, lambda);

    // 1K lines lisp - https://github.com/rui314/minilisp/blob/master/minilisp.c
    // quote
    lispF("cons", 2, cons);
    lispF("car", 1, carr);
    lispF("cdr", 1, cdrr);
// setq
// define
// defun
// defmacro
    lispF("setcar", 2, setcar);
    lispF("setcdr", 2, setcdr);
// while
// gensym
    lispF("+", 2, plus);
    lispF("-", 2, minus);
    lispF("*", 2, times); // extra
    lispF("*", 2, divide); // extra
    lispF("<", 2, lessthan); 
// macroexpand
    //lispF("lambda", -16, lambda);
    //lispF("if", 3, iff);
    lispF("=", 2, equal);
    lispF("eq", 2, eq);
    lispF("princ", 1, princ); // println
    lispF("terpri", 0, terpri); // extra

    // -- all extras
    lispF("read", 1, read);
    lispF("symbol", 0, symbol); 
    lispF("eval", 1, eval);
    //lispF("apply", 1, apply);
    //lispF("evallist", 2, evallist);

    // -- special
    lispF("if", -3, iff);
    lispF("lambda", -16, lambda);
}

 void tread(char* s) {
    printf("\nread-%s: ", s);
    princ(read(s));
    terpri();
}
    
void newLispTest() {
    dogc = 1;

    princ(read("(foo bar 42)"));
    lisp env = nil;
    SETQ(lambda, LAMBDA);
    PRIM(+, 2, plus);
    PRIM(-, 2, minus);
    PRIM(*, 2, times);
    PRIM(eq, 2, eq);
    PRIM(=, 2, eq);
    PRIM(if, -3, iff);
    PRIM(terpri, 0, terpri);
    PRIM(princ, 1, princ);

    printf("\n\n----------------------TAIL OPT AA BB!\n");
    princ(read("(+ 333 444)"));
    princ(evalGC(read("(+ 33 44)"), env));
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

void lisptest() {
    printf("------------------------------------------------------\n");
    newLispTest();
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
    lisp env = cons(cons(a, mkint(5)), nil);;
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
        newLispTest();
    }
    printf("\n========================================================END=====================================================\n");
}
