/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* A mini "lisp machine" */

#ifndef TEST
  #include "espressif/esp_common.h"
#endif

#ifdef TEST
  #include <stdlib.h>
  #include <stdio.h>
#endif

#include <string.h>

// 'pretty"
// all lispy symbols are required to start with tag

// TODO: use pointer to store some tag data, use this for exteded types
// last bits:
// ----------
// 00 = integer << 2
// 01 = cons
// 10 = 
// 11 = extended type, remove bits to get pointer assumes 4 byte boundary

typedef struct {
    char tag;
} *lisp;

lisp NIL = NULL;
lisp t = NULL;

int gettag(lisp x) { return x->tag; }


#define string_TAG 1
typedef struct {
    char tag;
    char* p; // TODO: make it inline, not second allocation
} string;

#define TAG(x) (x ? ((lisp)x)->tag : 0 )
#define ALLOC(type) ({type* x = malloc(sizeof(type)); x->tag = type ## _TAG; x;})
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

lisp car(lisp x) { return x ? ATTR(conss, x, car) : NIL; }
lisp cdr(lisp x) { return x ? ATTR(conss, x, cdr) : NIL; }
lisp setcar(lisp x, lisp v) { return IS(x, conss) ? ATTR(conss, x, car) = v : NIL; return v; }
lisp setcdr(lisp x, lisp v) { return IS(x, conss) ? ATTR(conss, x, cdr) = v : NIL; return v; }

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
    char n;
    char* name;
    void* f;
} prim;

lisp mkprim(char* name, int n, void *f) {
    prim* r = ALLOC(prim);
    r->name = name;
    r->n = n;
    r->f = f;
    return (lisp)r;
}

// TODO: remove
lisp princ(lisp x);
lisp terpri();

lisp eval(lisp e, lisp env);
lisp eq(lisp a, lisp b);

// returns binding, so you can change the cdr == value
lisp assoc(lisp name, lisp env) {
    while (env) {
        lisp bind = car(env);
        if (eq(car(bind), name)) return bind;
        env = cdr(env);
    }
    return NIL;
}

lisp evallist(lisp e, lisp env) {
    if (!e) return e;
    // TODO: don't recurse!
    return cons(eval(car(e), env), evallist(cdr(e), env));
}

lisp primapply(lisp ff, lisp args, lisp env) {
    int n = ATTR(prim, ff, n);
    // normal apply = eval list
    if (n >= 0) {
        args = evallist(args, env);
    }
    lisp a = args, b = cdr(a), c = cdr(b), d = cdr(c), e = cdr(d), f = cdr(e), g = cdr(f), h = cdr(g), i = cdr(h), j = cdr(i);
    lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = ATTR(prim, ff, f);
    // with C calling convention it's ok, but maybe not most efficient...
    lisp r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    princ(cons(ff, args));
    printf(" -> "); princ(r); printf("\n");

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

#define atom_TAG 5
typedef struct atom {
    char tag;
    char* name;
    struct atom* next;
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

///--------------------------------------------------------------------------------
// Primitives

lisp plus(lisp a, lisp b) { return mkint(getint(a) + getint(b)); }
lisp minus(lisp a, lisp b) { return mkint(getint(a) - getint(b)); }
lisp times(lisp a, lisp b) { return mkint(getint(a) * getint(b)); }
lisp divide(lisp a, lisp b) { return mkint(getint(a) / getint(b)); }
lisp lessthan(lisp a, lisp b) { return getint(a) < getint(b) ?  t : NIL; }
lisp terpri() { printf("\n"); return NIL; }
lisp eq(lisp a, lisp b) {
    if (a == b) return t;
    char ta = TAG(a);
    char tb = TAG(b);
    if (ta != tb) return NIL;
    // only integer is 'atom' needs to be eq that follow pointer
    // TODO: string???
    if (ta != intint_TAG) return NIL;
    if (getint(a) == getint(b)) return t;
    return NIL;
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
    if (!c) return NIL;
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
        return NIL;
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
    if (x == NIL) {
        printf("NIL");
        return NIL;
    }

    char tag = TAG(x);
    // simple one liners
    if (tag == string_TAG) printf("%s", ATTR(string, x, p));
    else if (tag == intint_TAG) printf("%d", ATTR(intint, x, v));
    else if (tag == prim_TAG) printf("#prim:%s", ATTR(prim, x, name));
    else if (tag == atom_TAG) printf("%s", ATTR(atom, x, name));
    // longer blocks
    else if (tag == conss_TAG) {
        putchar('(');
        princ(car(x));
        lisp d = cdr(x);
        while (d && gettag(d) == conss_TAG) {
            putchar(' ');
            princ(car(d));
            // TODO: remove recursion, otherwise blow up stack
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
    return NIL;
}

lisp eval(lisp e, lisp env) {
    if (e == NIL) return e;
    if (IS(e, atom)) return cdr(assoc(e, env));
    if (!IS(e, conss)) return e; // all others are literal (for now)
    lisp f = eval(car(e), env);
    return primapply(f, cdr(e), env);
}

void lispF(char* name, int n, void* f) {
    // TODO: do something...
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
    lisp pp = cons(plu, cons(mkint(3), cons(mkint(4), NIL)));
    lisp tt = cons(tim, cons(mkint(3), cons(mkint(4), NIL)));
    eval(cons(plu, cons(pp, cons(tt, NIL))), NIL);

    printf("\neval-a: ");
    lisp a = symbol("a");
    lisp env = cons(cons(a, mkint(5)), NIL);;
    princ(eval(a, env));

    printf("\nchanged neval-a: ");
    princ(eval(a, cons(cons(a, mkint(77)), env)));

    terpri();
}
