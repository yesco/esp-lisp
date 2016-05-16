#ifndef LISP_H
#define LISP_H 1

// for PRIMP !
//#define PRIM lisp
#define PRIM __attribute__ ((aligned (16))) lisp

typedef struct {
    char tag;
    char xx;
    short index; // index into allocs_ptr array, -1 if not in index
} *lisp;

#define string_TAG 1
#define conss_TAG 2
#define intint_TAG 3
#define prim_TAG 4
#define symboll_TAG 5 // be aware, we currently have two symbol implemenations, test SYMP() first (internal), then use TAG()
#define thunk_TAG 6
#define immediate_TAG 7
#define func_TAG 8
#define MAX_TAGS 16

#define TAG(x) ({ lisp _x = (x); !_x ? 0 : INTP(_x) ? intint_TAG : CONSP(_x) ? conss_TAG : SYMP(_x) ? symboll_TAG : HSYMP(_x) ? symboll_TAG : PRIMP(_x) ? prim_TAG : ((lisp)_x)->tag; })
#define ALLOC(type) ({type* x = myMalloc(sizeof(type), type ## _TAG); x->tag = type ## _TAG; x;})
#define ATTR(type, x, field) ((type*)x)->field
#define IS(x, type) (x && TAG(x) == type ## _TAG)

#define INTP(x) ((((unsigned int)x) & 3) == 1)
#define GETINT(x) (((signed int)x) >> 2)
#define MKINT(i) ((lisp)((((unsigned int)(i)) << 2) | 1))

#define CONSP(x) ((((unsigned int)x) & 7) == 2)
#define GETCONS(x) ((conss*)(((unsigned int)x) & ~2))
#define MKCONS(x) ((lisp)(((unsigned int)x) | 2))

#define SYMP(x) ((((unsigned int)x) & 3) == 3)
#define HSYMP(x) ((((unsigned int)x) & 0xff) == 0xff)

lisp mkprim(char* name, int n, void *f);

#define PRIMP(x) ((((unsigned int)x) & 7) == 6)
#define GETPRIM(x) ((conss*)(((unsigned int)x) & ~6))
#define MKPRIM(x) ((lisp)(((unsigned int)x) | 6))
#define GETPRIMFUNC(x) (getprimfunc(x))
#define GETPRIMNUM(x) (getprimnum(x))

lisp mkint(int v);
int getint(lisp x);
lisp mkprim(char* name, int n, void *f);
lisp symbol(char* s);
lisp quote(lisp x);

// symbols
lisp nil;
lisp t;
lisp LAMBDA;
lisp _FREE_;
lisp ATSYMBOL;

// misc mgt
void report_allocs(int verbose);
lisp lisp_init();
void lisp_run(lisp* envp);

void maybeGC();

// lisp entry functions
lisp apply(lisp f, lisp args);
lisp eval(lisp e, lisp* envp);
lisp progn(lisp* envp, lisp all);

// lisp functions
PRIM prin1(lisp x);
PRIM princ(lisp x);
PRIM printf_(lisp *envp, lisp all);
PRIM terpri();

lisp car(lisp x);
lisp cdr(lisp x);

PRIM setcar(lisp x, lisp v);
PRIM setcdr(lisp x, lisp v);

// list(mkint(1), mkint(2), END);
lisp list(lisp first, ...);
#define END ((lisp) -1)

PRIM _define(lisp* envp, lisp args);
PRIM de(lisp* envp, lisp namebody);
PRIM reads(char *s);

// User, macros, assume a "globaL" env variable implicitly, and updates it
#define SET(sname, val) _setbang(envp, sname, val)
#define SETQc(sname, val) _setbang(envp, symbol(#sname), val)
#define SETQ(sname, val) _setbang(envp, symbol(#sname), reads(#val))
#define SETQQ(sname, val) _setbang(envp, symbol(#sname), quote(reads(#val)))
#define DEFINE(fname, sbody) _define(envp, cons(symbol(#fname), cons(reads(#sbody), nil)))
#define DE(all) de(envp, reads(#all))
#define EVAL(what) eval(reads(#what), envp)
#define PRINT(what) ({ princ(EVAL(what)); terpri(); })
#define SHOW(what) ({ printf(#what " => "); princ(EVAL(what)); terpri(); })
#define TEST(what, expect) testss(envp, #what, #expect)
#define DEFPRIM(fname, argn, fun) _setbang(envp, symbol(#fname), mkprim(#fname, argn, fun))

// symbol (internalish) functions
void init_symbols();
lisp hashsym(lisp sym, char* optionalString, int len, int create_binding);
lisp symbol_len(char* start, int len);
void syms_mark();
PRIM syms(lisp f);

void sym2str(lisp s, char name[7]); // be aware this only fork for SYMP(s)
char* symbol_getString(lisp s); // be aware this only works for !SYMP(s) && IS(s, symboll)

// TODO: inline or macro
int getprimnum(lisp p);
void* getprimfunc(lisp p);

// memory mgt
void error(char* msg);
PRIM print_detailed_stack();
void print_stack();

lisp mem_usage(int count);
void* myMalloc(int bytes, int tag);
char* my_strndup(char* s, int len); // calls myMalloc

lisp evalGC(lisp e, lisp *envp); // maybe not call directly... not safe if you've done a cons unless you mark it first...
void mark(lisp x);

#endif

