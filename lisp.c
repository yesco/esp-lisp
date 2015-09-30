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

lisp symbol(char*);

lisp t = NULL;

int gettag(lisp x) {
    return *(char*)x;
}

#define string_TAG 1
typedef struct {
    char tag;
    char* p; // TODO: make it inline, not second allocation
} string;

// make a string from POINTER (inside other string) by copying LEN bytes
lisp mklenstring(char *s, int len) {
    string* r = malloc(sizeof(string));
    r->tag = string_TAG;
    r->p = malloc(len+1);
    strncpy(r->p, s, len);
    r->p[len] = 0; // make sure!
    return (lisp)r;
}

lisp mkstring(char *s) {
    string* r = malloc(sizeof(string));
    r->tag = string_TAG;
    r->p = s;
    return (lisp)r;
}

lisp mkatom(char *s) {
    // TODO: wrong, same atoms not EQ!
    return (lisp)mkstring(s);
}

lisp symbol(char *s) {
    return mkatom(s);
}

#define conss_TAG 2
typedef struct {
    char tag;
    lisp car;
    lisp cdr;
} conss;

lisp cons(lisp a, lisp b) {
    conss* r = malloc(sizeof(conss));
    r->tag = conss_TAG;
    r->car = a;
    r->cdr = b;
    return (lisp)r;
}

lisp car(lisp x) {
    if (!x) return NIL;
    return (lisp)((conss*)x)->car;
}

lisp cdr(lisp x) {
    if (!x) return NIL;
    return (lisp)((conss*)x)->cdr;
}

lisp setcar(lisp x, lisp v) {
    if (gettag(x) == conss_TAG) ((conss*)x)->car = v;
    return v;
}

lisp setcdr(lisp x, lisp v) {
    if (gettag(x) == conss_TAG) ((conss*)x)->cdr = v;
    return v;
}

#define intint_TAG 3
// TODO: store inline in pointer
typedef struct {
    char tag;
    int v;
} intint;

lisp mkint(int v) {
    intint* r = malloc(sizeof(intint));
    r->tag = intint_TAG;
    r->v = v;
    return (lisp)r;
}

int getint(lisp x) {
    if (gettag(x) == intint_TAG) return ((intint*)x)->v;
    return 0;
}

#define prim_TAG 4

typedef struct {
    char tag;
    char n;
    char* name;
    void* f;
} prim;

lisp mkprim(char* name, int n, void *f) {
    prim* r = malloc(sizeof(prim));
    r->tag = prim_TAG;
    r->name = name;
    r->n = n;
    r->f = f;
    return (lisp)r;
}

char* primname(lisp x) {
    return ((prim*)x)->name;
}

///--------------------------------------------------------------------------------

lisp plus(lisp a, lisp b) {
    return mkint(getint(a) + getint(b));
}

lisp minus(lisp a, lisp b) {
    return mkint(getint(a) - getint(b));
}
 
lisp times(lisp a, lisp b) {
    return mkint(getint(a) * getint(b));
}

lisp divide(lisp a, lisp b) {
    return mkint(getint(a) / getint(b));
}

lisp lessthan(lisp a, lisp b) {
    return getint(a) < getint(b) ?  t : NIL;
}
    
lisp eq(lisp a, lisp b) {
    return a == b ? t : NIL;
}

lisp equal(lisp a, lisp b) {
    if (eq(a, b)) return t;
    return symbol("*EQUAL-NOT-DEFINED*");
}

lisp princ(lisp x);
lisp terpri();

///--------------------------------------------------------------------------------
lisp primapply(lisp ff, lisp args) {
    lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = ((prim*)ff)->f;

    lisp a = args;
    lisp b = cdr(a);
    lisp c = cdr(b);
    lisp d = cdr(c);
    lisp e = cdr(d);
    lisp f = cdr(e);
    lisp g = cdr(f);
    lisp h = cdr(g);
    lisp i = cdr(h);
    lisp j = cdr(i);

    // actually this works because of C calling convention, however, it's pretty expensive
    // and wasteful, replace by standard switch?
    lisp r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    princ(cons(ff, args));
    printf(" -> "); princ(r); printf("\n");

    return NIL;
}

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

lisp terpri() {
    printf("\n");
    return NIL;
}

lisp princ(lisp x) {
    if (x == NIL) {
        printf("NIL");
        return NIL;
    }

    char tag = gettag(x);
    // simple one liners
    if (tag == string_TAG) printf("%s", ((string*)x)->p);
    else if (tag == intint_TAG) printf("%d", ((intint*)x)->v);
    else if (tag == prim_TAG) printf("#prim:%s", primname(x));
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

void eval(lisp e) {
    lisp f = car(e);
    //lisp a = car(cdr(e));
    //lisp b = car(cdr(cdr(e)));
    //lisp args[2] = {a, b};
    //args = e;

    primapply(f, cdr(e));
}

void seval(char *e) {
    princ(read("foo"));
    //char* x = malloc(100);
    //char *fgets(char *s, int size, FILE *stream);
    //x = fgets(x, 99, stdin);
//    int c = getchar();
//    printf("input: %d\n", c);
    printf("eval: %s\n", e);

    char* r = malloc(100);
    r[0] = 'f';
    r[1] = 'o';
    r[2] = 'o';
    r[3] = 0;
    printf("--> %s\n", r);
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

    lispF("read", 1, read); // extra
    lispF("symbol", 0, symbol); // extra
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
    printf("\n");

    seval("(+ 3 4)");
    
    eval(cons(mkprim("plus", 2, plus), cons(mkint(3), cons(mkint(4), NIL))));
    eval(cons(mkprim("times", 2, times), cons(mkint(3), cons(mkint(4), NIL))));
}
