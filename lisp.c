/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* A mini "lisp machine" */

#ifndef TEST
  #include "espressif/esp_common.h"
  //#include "FreeRTOS.h" // just for MEM FREE QUESTION
#endif

#ifdef TEST
  #include <stdlib.h>
  #include <stdio.h>
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


int gettag(lisp x) { return x->tag; }


#define string_TAG 1
#define conss_TAG 2

#define immediate_TAG 7

typedef struct {
    char tag;
    char* p; // TODO: make it inline, not second allocation
} string;

#define MAX_TAGS 16
int tag_count[MAX_TAGS] = {0};
int tag_bytes[MAX_TAGS] = {0};
char* tag_name[MAX_TAGS] = { "TOTAL", "string", "cons", "int", "prim", "atom", "thunk", "immediate", "func", 0 };

#define MAX_ALLOCS 1024
int allocs_count = 0;
void* allocs[MAX_ALLOCS] = { 0 };
unsigned int used[MAX_ALLOCS/32 + 1] = { 0 };
    
#define SET_USED(i) ({int _i = (i); used[_i/32] |= 1 << _i%32;})
#define IS_USED(i) ({int _i = (i); (used[_i/32] >> _i%32) & 1;})

#define TAG(x) (x ? ((lisp)x)->tag : 0 )
#define ALLOC(type) ({type* x = myMalloc(sizeof(type), type ## _TAG); x->tag = type ## _TAG; x;})
#define ATTR(type, x, field) ((type*)x)->field
#define IS(x, type) (x && TAG(x) == type ## _TAG)

void reportAllocs();

// any slot with no value/nil can be reused
int reuse() {
    int i ;
    for(i = 0; i < allocs_count; i++) {
        if (!allocs[i]) return i;
    }
    return -1;
}

void* myMalloc(int bytes, int tag) {
    tag_count[tag]++;
    tag_bytes[tag] += bytes;
    tag_count[0]++;
    tag_bytes[0] += bytes;

    if (allocs_count == 269) { printf("\n==============ALLOC: %d bytes of tag %s ========================\n", bytes, tag_name[tag]); }
    if (allocs_count == 270) { printf("\n==============ALLOC: %d bytes of tag %d %s ========================\n", bytes, tag, tag_name[tag]); }
    void* p = malloc(bytes);
    // dangerous optimization
    if ((int)p == 0x08050208) {
        printf("\n============================== ALLOC trouble pointer %d bytes of tag %d %s ===========\n", bytes, tag, tag_name[tag]);
    }
    if (tag == immediate_TAG) {
        // do not record, do not GC, they'll be GC:ed automatically as invoked once!
        return p;
    }
    int pos = reuse();
    if (pos < 0) {
        pos = allocs_count;
        allocs_count++;
    }
    if ((int)p == 0x0804e528) {
        printf("\n=POS=%d pointer=0x%x tag %d %s\n", pos, p, tag, tag_name[tag]);
    }
    allocs[pos] = p;
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
void gc() {
    int count = 0;
    printf(" [GC...");
    int i ;
    for(i = 0; i < allocs_count; i++) {
        lisp p = allocs[i];
        if (!p) continue;
        
        if ((int)p == 0x0804e528) {
            printf("\nGC----------------------%d ERROR! p=0x%x  ", i, p); princ(p); terpri();
        }

        if (TAG(p) > 8 || TAG(p) == 0) {
            printf("\nGC----------------------%d ILLEGAL TAG! %d p=0x%x  ", i, TAG(p), p); princ(p); terpri();
        }
        int u = (used[i/32] >> i%32) & 1;
        if (u) {
//            printf("%d used=%d  ::  ", i, u); princ(p); terpri();
        } else {
//            printf("%d FREE! ", i);
            count++;
            if (1) {
                free(p);
            } else {
                printf("FREE: "); princ(p); terpri();
                // simulate free
                p->tag = 66;
            }
            allocs[i] = NULL;
        }
    }
    mark_clean();
    printf("%d] ", count);
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

// conss name in order to be able to have a function named 'cons()'
typedef struct {
    char tag;
    lisp car;
    lisp cdr;
} conss;

lisp cons(lisp a, lisp b) {
    conss* r = ALLOC(conss);
    r->car = a;
    r->cdr = b;
    return (lisp)r;
}

lisp car(lisp x) { return x && IS(x, conss) ? ATTR(conss, x, car) : nil; }
lisp cdr(lisp x) { return x && IS(x, conss) ? ATTR(conss, x, cdr) : nil; }
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

#define intint_TAG 3
// TODO: store inline in pointer
typedef struct {
    char tag;
    int v;
} intint;

lisp mkint(int v) {
    intint* r = ALLOC(intint);
    r->v = v;
    return (lisp)r;
}

int getint(lisp x) { return IS(x, intint) ? ATTR(intint, x, v) : 0; }

#define prim_TAG 4

typedef struct {
    char tag;
    signed char n;
    void* f;
    char* name; // TODO should be char name[1]; // inline allocation!, or actually should point to an ATOM
} prim;

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
    printf("PRIMAPPLY "); princ(ff); princ(args); terpri();
    int n = ATTR(prim, ff, n);
    // normal apply = eval list
    if (n >= 0) {
        args = evallist(args, env);
        printf("eval ARGS=> "); princ(args); terpri();
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

// TODO: somehow an atom is very similar to a conss cell.
// it has two pointers, next/cdr, diff is first pointer points a naked string/not lisp string. Maybe it should?
// TODO: if we make this a 2-cell or 1-cell lisp? or maybe atoms should have no property list or value, just use ENV for that
#define atom_TAG 5
typedef struct atom {
    char tag;
    struct atom* next;
    char* name; // TODO should be char name[1]; // inline allocation!
} atom;

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

// Pseudo closure that is returned by if/progn and other construct that takes code, should handle tail recursion
#define thunk_TAG 6
typedef struct thunk {
    char tag;
    lisp e;
    lisp env;
    // This needs be same as immediate
} thunk;

lisp mkthunk(lisp e, lisp env) {
    thunk* r = ALLOC(thunk);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

typedef struct immediate {
    char tag;
    lisp e;
    lisp env;
    // This needs be same as thunk
} immediate;

// an immediate is a continuation returned that will be called by eval directly to yield another value
// this implements continuation based evaluation thus maybe alllowing tail recursion...
lisp mkimmediate(lisp e, lisp env) {
    immediate* r = ALLOC(immediate); //(thunk*)mkthunk(e, env); // inherit from func_TAG
    //r->tag = immediate_TAG;
    r->e = e;
    r->env = env;
    return (lisp)r;
}

#define func_TAG 8

typedef struct func {
    char tag;
    lisp e;
    lisp env;
    // This needs be same as thunk
} func;

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
void mark_deep(void* x, int deep) {
    int i =  0;
    while (i < allocs_count) {
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
                    x = ATTR(atom, p, next);
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

void mark(void* x) {
    mark_deep(x, 1);
    mark_deep(symbol_list, 0); // never deallocate atoms!!!
    mark_deep(nil, 1);
    mark_deep(LAMBDA, 1);
}

///--------------------------------------------------------------------------------
// Primitives

lisp plus(lisp a, lisp b) { printf("PLUS!"); princ(a); princ(b); terpri(); return mkint(getint(a) + getint(b)); }
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
#define DEF(fname, sbody) printf("DEFINE:%s %s", #fname, #sbody); env = setq(symbol(#fname), read(#sbody), env)
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
        return nil;
    }

    char tag = TAG(x);
    // simple one liners
    if (tag == string_TAG) printf("%s", ATTR(string, x, p));
    else if (tag == intint_TAG) printf("%d", ATTR(intint, x, v));
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
    return nil;
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
    lisp f = evalGC(car(e), env);
    //printf("NOT FUNC: "); princ(f); terpri();
    while (f && !IS(f, prim) && !IS(f, thunk) && !IS(f, func) && !IS(f, immediate)) {
        f = evalGC(f, env);
        //printf("GOT--: "); princ(f); terpri();
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

lisp evalGC(lisp e, lisp env) {
    if (!e) return e;
    if (!IS(e, atom) && !IS(e, conss)) return e;

    if (level >= 64) {
        printf("You're royally screwed! why does it still work?\n");
        #ifdef TEST
          exit(1);
        #endif
    }

    if (!blockGC) {
        mymark(env); // TODO: important

        stack[level] = e;
        // print stack
        printf("%d STACK: ", level); int i;
        for(i=0; i<64; i++) {
            if (!stack[i]) break;
            printf(" %d: ", i);
            princ(stack[i]);
            mymark(stack[i]); // TODO: important
        }
        terpri();

        // TODO: better mark all shit first!
        mygc(); // not safe here, setq for example calls eval, it should not do gc as it builds cons at same time...
        // evallist the same issue, TODO: maybe an eval that doesn't GC? and only gc here and "up"
    }

    indent(level++); printf("---> "); princ(e); terpri();
    indent(level+1); printf(" ENV= "); princ(env); terpri();
    lisp r = eval_hlp(e, env);
    while (r && TAG(r) == immediate_TAG) {
        lisp tofree = r;
        r = eval_hlp(ATTR(thunk, r, e), ATTR(thunk, r, env));
        //mark(ATTR(thunk, r, e)); mark(ATTR(thunk, r, env));
        //r = eval(ATTR(thunk, r, e), ATTR(thunk, r, env));
        // immediates are immediately consumed after evaluation, so they can be free:d directly
        // TODO: move into eval_hlp?
        tofree->tag = 0;
        free(tofree);
    }
    indent(--level); princ(r); printf(" <--- "); princ(e); terpri();
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
    printf("FUNCAPPLY:"); princ(f); printf(" body="); princ(l); printf(" args="); princ(args); printf(" env="); princ(lenv); terpri();
    lisp fargs = car(l); // skip #lambda
    args = evallist(args, env);
    lenv = bindlist(fargs, args, lenv);
    lisp prog = car(cdr(l)); // skip #lambda (...) GET IGNORE
    printf("HERE! prog="); princ(prog); terpri();
    printf("HERE! fargs="); princ(fargs); terpri();
    printf("HERE! args="); princ(args); terpri();
    printf("HERE! env="); princ(lenv); terpri();
    // TODO: implicit progn? loop over cdr...
    //printf("MKIMMEDIATE: "); princ(prog); terpri();    
    return mkimmediate(prog, lenv);
    // this doesn't handle tail recursion, but may be faster?
    return eval(prog, lenv);
}

void lispinit() {
    t = symbol("t");
    // nil = symbol("nil"); // LOL? TODO:? that wouldn't make sense?
    LAMBDA = mkprim("lambda", -16, lambda);

    // 1K lines lisp - https://github.com/rui314/minilisp/blob/master/minilisp.c
    // quote
    lispF("cons", 2, cons);
    lispF("car", 1, car);
    lispF("cdr", 1, cdr);
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
    lispF("if", -4, iff);
    lispF("lambda", -16, lambda);
}

 void tread(char* s) {
    printf("\nread-%s: ", s);
    princ(read(s));
    terpri();
}
    
void newLispTest() {
    dogc = 1;

    lisp env = nil;
    SETQ(lambda, LAMBDA);
    PRIM(+, 2, plus);
    PRIM(-, 2, minus);
    PRIM(*, 2, times);
    PRIM(eq, 2, eq);
    PRIM(=, 2, eq);
    PRIM(if, -4, iff);

    printf("\n\n----------------------TAIL OPT AA BB!\n");
    princ(read("(+ 333 444)"));
    princ(evalGC(read("(+ 33 44)"), env));
    DEF(bb, (lambda (b) (+ b 3)));
    DEF(aa, (lambda (a) (bb a)));
    eval(read("(aa 7)"), env);

    printf("\n\n----------------------TAIL RECURSION!\n");
    printf("1====\n");
    DEF(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
    printf("2====\n");
    //evalGC(read("(tail 900 0)"), env); // OK, can tail recurses
    evalGC(read("(if (tail 900 0) 999 666)"), env);

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
    lisp xx = cons(plu, cons(pp, cons(tt, nil)));

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

    lisp IF = mkprim("if", -4, iff);
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
        IF = mkprim("if", -4, iff);
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
