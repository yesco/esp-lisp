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
lisp t = NULL;

int gettag(lisp x) { return x->tag; }


#define string_TAG 1
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

void* myMalloc(int bytes, int tag) {
    tag_count[tag]++;
    tag_bytes[tag] += bytes;
    tag_count[0]++;
    tag_bytes[0] += bytes;

    void* p = malloc(bytes);
    allocs[allocs_count] = p;
    allocs_count++;
    if (allocs_count >= MAX_ALLOCS) {
        printf("Exhaused myMalloc array!\n");
        #ifdef TEST
          exit(1);
        #endif
    }
    return p;
}

void gc() {
    printf("\nGC...\n");
    int i ;
    for(i = 0; i < allocs_count; i++) {
        lisp p = allocs[i];
        if (!p) continue;
        
        int u = (used[i/32] >> i%32) & 1;
        if (u) {
            printf("%d used=%d  ::  ", i, u); princ(p); terpri();
        } else {
            printf("%d FREE! ", i);
            free(p);
            allocs[i] = NULL;
        }
    }
}

void reportAllocs() {
    gc();
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

#define TAG(x) (x ? ((lisp)x)->tag : 0 )
#define ALLOC(type) ({type* x = myMalloc(sizeof(type), type ## _TAG); x->tag = type ## _TAG; x;})
#define ATTR(type, x, field) ((type*)x)->field
#define IS(x, type) (x && TAG(x) == type ## _TAG)

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
#define conss_TAG 2
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

lisp primapply(lisp ff, lisp args, lisp env) {
    //printf("PRIMAPPLY "); princ(ff); princ(args); terpri();
    int n = ATTR(prim, ff, n);
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
        lisp (*fp)(lisp, lisp) = ATTR(prim, ff, f);
        r = fp(env, args);
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
} thunk;

lisp mkthunk(lisp e, lisp env) {
    thunk* r = ALLOC(thunk);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

#define immediate_TAG 7

// an immediate is a continuation returned that will be called by eval directly to yield another value
// this implements continuation based evaluation thus maybe alllowing tail recursion...
lisp mkimmediate(lisp e, lisp env) {
    thunk* r = (thunk*)mkthunk(e, env); // inherit from func_TAG
    r->tag = immediate_TAG;
    return (lisp)r;
}

#define func_TAG 8
lisp mkfunc(lisp e, lisp env) {
    thunk* r = (thunk*)mkthunk(e, env); // inherit from func_TAG
    r->tag = func_TAG;
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
                if (IS_USED(i)) return; // no need mark again or follow pointers
                SET_USED(i); printf("Marked %i deep %i :: ", i, deep); princ(p); terpri();

                // only atom and conss contains pointers...
                if (IS(p, atom)) {
                    x = ATTR(atom, p, next);
                    i = -1;
                } else if (IS(p, conss)) {
                    mark_deep(car(p), deep+1);
                    // don't recurse on rest, just loop
                    x = cdr(p);
                    i = -1;
                } else if (IS(p, thunk) || IS(p, immediate) || IS(p, func)) {
                    // should we switch? which one is most likely to become deep?
                    mark_deep(ATTR(thunk, p, e), deep+1);
                    x = ATTR(thunk, p, env);
                } else return; // found pointer but nothing more to do
            }
        }

        i++;
    }
}

void mark(void* x) {
    mark_deep(x, 1);
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
    // only integer is 'atom' needs to be eq that follow pointer
    // TODO: string???
    if (ta != intint_TAG) return nil;
    if (getint(a) == getint(b)) return t;
    return nil;
}
lisp equal(lisp a, lisp b) { return eq(a, b) ? t : symbol("*EQUAL-NOT-DEFINED*"); }

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
        //free(input);
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
    // TODO: lookup atom, use linked list first...
    return mklenstring(start, len);
}

lisp readx() {
    skipSpace();
    char c = next();
    if (!c) return NULL;

    if (c == '(') {
        // TODO: don't recurse on tail, long lists will kill the stack!
        lisp a = readx();
        skipSpace();
        c = next(); if (c == '.') next(); else nextChar = c;
        skipSpace();
        lisp d = readx();
        return cons(a, d);
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

lisp eval_hlp(lisp e, lisp env) {
    //if (e == nil) return e;
    if (IS(e, atom)) return cdr(assoc(e, env)); // look up variable
    //if (!IS(e, conss)) return e; // all others are literal (for now)

    lisp f = eval(car(e), env);
    if (IS(f, prim)) return primapply(f, cdr(e), env);
    if (IS(f, thunk)) return eval(ATTR(thunk, f, e), ATTR(thunk, f, env)); // ignore arguments
    if (IS(f, func)) return funcapply(f, cdr(e), env);
    printf("%%ERROR.lisp - don't know how to evaluate: "); princ(e); terpri();
    return nil;
}

lisp eval(lisp e, lisp env) {
    if (e == nil) return e;
    if (!IS(e, atom) && !IS(e, conss)) return e;

    indent(level++); printf("---> "); princ(e); terpri();
    lisp r = eval_hlp(e, env);
    while (r && TAG(r) == immediate_TAG) {
        r = eval(ATTR(thunk, r, e), ATTR(thunk, r, env));
    }
    indent(--level); princ(r); printf(" <--- "); princ(e); terpri();
    return r;
}

void lispF(char* name, int n, void* f) {
    // TODO: do something...
}

lisp iff(lisp env, lisp exp, lisp thn, lisp els) {
    // non-tail if
    if (0) {
        //printf("ENV="); princ(env); terpri();
        return eval(exp, env) ? eval(thn, env) : eval(els, env);
    } else {
        return eval(exp, env) ? mkimmediate(thn, env) : mkimmediate(els, env);
    }
}

lisp lambda(lisp env, lisp rest) {
    // TODO: hmm, we just removed #lambda and now we're adding it again..., move up???
    return mkfunc(cons(mkprim("lambda", -16, lambda), rest), env);
}

lisp bindlist(lisp fargs, lisp args, lisp env) {
    // TODO: not recurse!
    if (!fargs) return env;
    lisp b = cons(car(fargs), car(args));
    return bindlist(cdr(fargs), cdr(args), cons(b, env));
}

// TODO: nlambda?
lisp funcapply(lisp f, lisp args, lisp env) {
    //printf("FUNCAPPLY:"); princ(f); terpri();
    lisp lenv = ATTR(thunk, f, env);
    lisp l = ATTR(thunk, f, e);
    lisp fargs = car(cdr(l)); // skip #lambda
    args = evallist(args, env);
    lenv = bindlist(fargs, args, lenv);
    lisp prog = car(cdr(cdr(l))); // skip #lambda (...) GET IGNORE
    // TODO: implicit progn? loop over cdr...
    return eval(prog, lenv);
}

void lispinit() {
    t = symbol("t");
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

void lisptest() {
    printf("------------------------------------------------------\n");
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
    printf("\nreadA---123: "); princ(read("123"));
    printf("\nread-a---1: "); princ(read("(A)"));
    printf("\nread-ab--12: "); princ(read("(A B)"));
    printf("\nread-abc-123: "); princ(read("(A B C)"));
    printf("\nread-3=3: "); princ(eq(mkint(3), mkint(3)));
    printf("\nread-3=4: "); princ(eq(mkint(3), mkint(4)));
    printf("\nread-a=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\nread-a=b: "); princ(eq(symbol("a"), symbol("b")));
    printf("\nread-a=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\n");

    lisp plu = mkprim("plus", 2, plus);
    lisp tim = mkprim("times", 2, times);
    lisp pp = cons(plu, cons(mkint(3), cons(mkint(4), nil)));
    lisp tt = cons(tim, cons(mkint(3), cons(mkint(4), nil)));
    lisp xx = cons(plu, cons(pp, cons(tt, nil)));
    mark(xx);
    eval(xx, nil);

    printf("\neval-a: ");
    lisp a = symbol("a");
    lisp env = cons(cons(a, mkint(5)), nil);;
    princ(eval(a, env));

    printf("\nchanged neval-a: ");
    mark(a);
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
    eval(list(LA, mkint(7), -1), nil);
    lisp la = symbol("lambda");
    lisp lenv = list(cons(la, LA), -1);
    lisp l = list(LA, list(symbol("n"), -1),
                  list(plu, mkint(39), symbol("n"), -1),
                  -1);
    l = eval(l, lenv);
    eval(list(l, mkint(3), -1), lenv); // looking up la giving LA doesn't work?

    lisp n = symbol("n");
    lisp EQ = mkprim("eq", 2, eq);
    lisp minuus = mkprim("minus", 2, minus);
    lisp facexp = list(EQ, n, mkint(0), -1);
    lisp facthn = mkint(1);
    lisp fc = symbol("fac");
    lisp facrec = list(fc, list(minuus, n, mkint(1), -1), -1);
    lisp facels = list(tim, n, facrec, -1);
    printf("\nfacels="); princ(facels); terpri();
    lisp facif = list(IF, facexp, facthn, facels, -1);
    lisp fac = list(LA, list(n, -1), facif, -1);
    lisp fenv = cons( cons(symbol("fac"), mkint(99)),
                      lenv);
    lisp FAC = eval(fac, fenv);
    lisp facbind = assoc(fc, fenv);
    setcdr(facbind, FAC); // create circular dependency on it's own defininition symbol by redefining
    eval(list(FAC, mkint(6), -1), fenv);

    princ(list(nil, mkstring("fihs"), mkint(1), symbol("fish"), mkint(2), mkint(3), mkint(4), nil, nil, nil, -1));
    //eval(read("(lambda (n) (if (eq n 0) 1 (fac (- n 1))))"), lenv);

//    reportAllocs();
//    printf("SIZEOF int = %d\nSIZEOF ptr = %d\n", sizeof(int), sizeof(int*));
}


