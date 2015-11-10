/* Distributed under Mozilla Public Licence 2.0   */
/* https://www.mozilla.org/en-US/MPL/2.0/         */
/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* A mini "lisp machine" */

// in speed it's comparable to compiled LUA
// - simple "readline"
// - maybe 2x as slow
// - handles tail-recursion optimization
// - full closures
// - lexical binding

// RAW C esp8266 - printf("10,000,000 LOOP (100x lua) TIME=%d\r\n", tm); ===> 50ms

// Lua time 100,000 => 2s
// ----------------------
//   s=0; for i=1,100000 do s=s+i end; print(s);
//   function tail(n, s) if n == 0 then return s else return tail(n-1, s+1); end end print(tail(100000, 0))

// DEF(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
// princ(evalGC(reads("(tail 100000 0)"), env));
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
//   bindEvalList (combined evallist + bindList) => 4040ms!
//   ... => 4010ms
//
//   slight increase if change the MAX_ALLOC to 512 but it keeps 17K free! => 4180ms

// (fibo 24)
//   lua: 3s
//   esp-lisp: 5s
//
// (fibo 30)
//   x61 lisp: 4.920s
//   opt: 2.67s
//
// time echo "function fibo(n) if n < 2 then return 1; else return fibo(n-1) + fibo(n-2); end end print(fibo(35)); " | lua
//   14930352, real 0m6.006s, user 0m3.653s

// lua fibo 32 
//   3524578, real 0m1.992s, user 0m0.903s
//
// time ./a.out
//   3524578, real 0m19.353s, user 0m12.353s
//
// time ./opt
//   3524578, real 0m3.549s, user 0m2.253s
//  
// 2.5x slower than lua interpreting instead of compiling to byte code!!!
//

#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef UNIX
  #include "FreeRTOS.h"

  #define LOOP 99999
  #define LOOPS "99999"
  #define LOOPTAIL "(tail 99999 0)"
#endif

#ifdef UNIX
  #define LOOP 2999999
  #define LOOPS "2999999"
  #define LOOPTAIL "(tail 2999999 0)"
#endif

// TODO: use pointer to store some tag data, use this for exteded types
// last bits (3 as it allocates in at least 8 bytes boundaries):
// ----------
// 000 = normal pointer, generic extended lisp data/struct - DONE
// 001 = integer << 1 - DONE
// -- byte[8] lispheap[MAX_HEAP], then still need byte[8] lisptag[MAX_HEAP]
// 010 = lispheap, cons == 8 bytes, 2 cells
// 100 = lispheap, symbol == name + primptr, not same as value
// 110 = 

// inline symbol? 32 bits = 6 * 5 bits = 30 bits = 6 "chars" x10, but then symbol cannot have ptr

// TODO: move all defs into this:
#include "lisp.h"

#include "compat.h"

// big value ok as it's used mostly no inside evaluation but outside at toplevel
#define READLINE_MAXLEN 1024

// set to 1 to get GC tracing messages
static int traceGC = 0;
static int trace = 0;

// use for list(mkint(1), symbol("foo"), mkint(3), END);
lisp END = (lisp) -1;

// global symbol variables, set in lisp_init()
lisp nil = NULL;
lisp t = NULL;
lisp LAMBDA = NULL;
lisp _FREE_ = NULL;

lisp evalGC(lisp e, lisp *envp);

// if non nil enables continous GC
// TODO: remove
int dogc = 0;

#define string_TAG 1
typedef struct {
    char tag;
    char xx;
    short index;

    char* p; // TODO: make it inline, not second allocation
} string;

// conss name in order to be able to have a function named 'cons()'
// these are special, not stored in the allocated array
#define conss_TAG 2
typedef struct {
    lisp car;
    lisp cdr;
} conss;

// lisp is a pointer with some semantics according to its lowest bits
// ==================================================================
// cons cells are 8 bytes, heap allocated objects are all 8 byte aligned too,
// thus, the three lowest bits aren't used so we use it for tagging/inline types.
//
// 00000000 nil
//      000 heap allocated objects
//       01 int stored inline in the pointer
//      010 cons pointer into conses array
//       11 symbol names stored inside the pointer
//
//      100 special pointer, see below...
//     0100 UNUSED: (IROM) symbol/string, n*16 bytes, zero terminated
//     1100 UNUSED: (IROM) longcons (array consequtive nil terminated list) n*16 bytes (n*8 cars)
//
//     x1yy cons style things? but with other type
//     0110 UNUSED: maybe we have func/thunk/immediate
//     1110 UNUSED: 

#define CONSP(x) ((((unsigned int)x) & 7) == 2)
#define GETCONS(x) ((conss*)(((unsigned int)x) & ~2))
#define MKCONS(x) ((lisp)(((unsigned int)x) | 2))

// stored using inline pointer
#define intint_TAG 3
typedef struct {
    char tag;
    char xx;
    short index;

    int v;
} intint; // TODO: handle overflow...

#define INTP(x) ((((unsigned int)x) & 3) == 1)
#define GETINT(x) (((signed int)x) >> 2)
#define MKINT(i) ((lisp)((((unsigned int)(i)) << 2) | 1))

#define prim_TAG 4
typedef struct {
    char tag;
    char xx;
    short index;

    signed char n; // TODO: maybe could use xx tag above?
    void* f;
    char* name; // TODO: should point to an SYMBOL! integrate SYMBOL and prims!
} prim;

// TODO: somehow a symbol is very similar to a conss cell.
// it has two pointers, next/cdr, diff is first pointer points a naked string/not lisp string. Maybe it should?
// TODO: if we make this a 2-cell or 1-cell lisp? or maybe symbols should have no property list or value, just use ENV for that
#define symboll_TAG 5
typedef struct symboll {
    char tag;
    char xx;
    short index;

    struct symboll* next;
    char* name; // TODO should be char name[1]; // inline allocation!
} symboll;

#define SYMP(x) ((((unsigned int)x) & 3) == 3)

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

#define TAG(x) ({ lisp _x = (x); INTP(_x) ? intint_TAG : CONSP(_x) ? conss_TAG : SYMP(_x) ? symboll_TAG : (_x ? ((lisp)_x)->tag : 0 ); })
//#define TAG(x) ({ lisp _x = (x); INTP(_x) ? intint_TAG : CONSP(_x) ? conss_TAG : (_x ? ((lisp)_x)->tag : 0 ); })

#define ALLOC(type) ({type* x = myMalloc(sizeof(type), type ## _TAG); x->tag = type ## _TAG; x;})
#define ATTR(type, x, field) ((type*)x)->field
#define IS(x, type) (x && TAG(x) == type ## _TAG)

int gettag(lisp x) {
    return TAG(x);
}

#define MAX_TAGS 16
int tag_count[MAX_TAGS] = {0};
int tag_bytes[MAX_TAGS] = {0};
int tag_freed_count[MAX_TAGS] = {0};
int tag_freed_bytes[MAX_TAGS] = {0};

char* tag_name[MAX_TAGS] = { "total", "string", "cons", "int", "prim", "symbol", "thunk", "immediate", "func", 0 };
int tag_size[MAX_TAGS] = { 0, sizeof(string), sizeof(conss), sizeof(intint), sizeof(prim), sizeof(thunk), sizeof(immediate), sizeof(func) };

// essentially total number of cons+symbol+prim
// TODO: remove SYMBOL since they are never GC:ed! (thunk are special too, not tracked)
//#define MAX_ALLOCS 819200 // (fibo 22)
//#define MAX_ALLOCS 8192
//#define MAX_ALLOCS 1024 // keesp 15K free
//#define MAX_ALLOCS 512 // keeps 17K free
//#define MAX_ALLOCS 256 // keeps 21K free
//#define MAX_ALLOCS 128 // keeps 21K free
#define MAX_ALLOCS 128

int allocs_count = 0;
void* allocs[MAX_ALLOCS] = { 0 };
unsigned int used[MAX_ALLOCS/32 + 1] = { 0 };
    
#define SET_USED(i) ({int _i = (i); used[_i/32] |= 1 << _i%32;})
#define IS_USED(i) ({int _i = (i); (used[_i/32] >> _i%32) & 1;})

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
int used_bytes = 0;

// SLOT salloc/sfree, reuse mallocs of same size instead of free, saved 20% speed
#define SALLOC_MAX_SIZE 32
void* alloc_slot[SALLOC_MAX_SIZE] = {0}; // TODO: probably too many sizes...

void* salloc(int bytes) {
    void** p = alloc_slot[bytes];
    if (!p || bytes >= SALLOC_MAX_SIZE) {
        used_bytes += bytes;
        return malloc(bytes);
    }
    alloc_slot[bytes] = *p;
    return p;
}

void sfree(void** p, int bytes, int tag) {
    if (CONSP((lisp)p)) {
        printf("sfree.CONS.error\n");
        exit(1);
    }
    if (bytes >= SALLOC_MAX_SIZE) {
        used_bytes -= bytes;
        return free(p);
    }
    // store for reuse
    void* n = alloc_slot[bytes];
    *p = n;
    alloc_slot[bytes] = p;
    // stats
    tag_freed_count[tag]++;
    tag_freed_bytes[tag] += bytes;
    tag_freed_count[0]++;
    tag_freed_bytes[0] += bytes;
}

// call this malloc using ALLOC(typename) macro
void* myMalloc(int bytes, int tag) {
    if (1) { // 830ms -> 770ms 5% faster if removed
    used_count++;
    tag_count[tag]++;
    tag_bytes[tag] += bytes;
    tag_count[0]++;
    tag_bytes[0] += bytes;
    }

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
        printf("Exhausted myMalloc array!\n");
        report_allocs(2);
        exit(1);
    }
    return p;
}

void mark_clean() {
    memset(used, 0, sizeof(used));
}

symboll* symbol_list = NULL;

void mark(lisp x);
void gc_conses();
lisp mem_usage(int count);

lisp gc(lisp* envp) {
    // mark
    mark(nil); mark((lisp)symbol_list); // TODO: remove?

    //if (envp) { printf("ENVP %u=", (unsigned int)*envp); princ(*envp); terpri();}
    if (envp) mark(*envp);

    // sweep
    gc_conses();
    
    int count = 0;
    int i ;
    for(i = 0; i < allocs_count; i++) {
        lisp p = allocs[i];
        if (!p) continue;
        if (INTP(p) || CONSP(p)) {
            printf("GC.erronious pointer stored: %u, tag=%d\n", (int)p, TAG(p));
            printf("VAL="); princ(p); terpri();
            exit(1);
        }
        
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
                sfree((void*)p, tag_size[TAG(p)], TAG(p));;
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

    return mem_usage(count);
}

void report_allocs(int verbose) {
    int i;

    terpri();
    if (verbose == 2)
        printf("--- Allocation stats ---\n");


    if (verbose == 1) {
        printf("\nAllocated: ");
        for(i = 0; i<16; i++)
            if (tag_count[i] > 0) printf("%d %s=%d bytes, ", tag_count[i], tag_name[i], tag_bytes[i]);
        
        printf("\n    Freed: ");
        for(i = 0; i<16; i++)
            if (tag_freed_count[i] > 0) printf("%d %s=%d bytes, ", tag_count[i], tag_name[i], tag_bytes[i]);

        printf("\n     Used: ");
    }

    for(i = 0; i<16; i++) {
        if (tag_count[i] > 0 || tag_freed_count[i] > 0) {
            int count = tag_count[i] - tag_freed_count[i];
            int bytes = tag_bytes[i] - tag_freed_bytes[i];
            if (verbose == 2) 
                printf("%12s: %3d allocations of %5d bytes, and still use %3d total %5d bytes\n",
                       tag_name[i], tag_count[i], tag_bytes[i], count, bytes);
            else if (verbose == 1 && (count > 0 || bytes > 0))
                printf("%d %s=%d bytes, ", count, tag_name[i], bytes);
        }
    }

    if (verbose == 2) {
        for(i = 0; i<16; i++) {
            if (tag_count[i] > 0 || tag_freed_count[i] > 0) {
                int count = tag_count[i] - tag_freed_count[i];
                int bytes = tag_bytes[i] - tag_freed_bytes[i];

                if (verbose == 1 && (tag_count[i] != count || tag_bytes[i] != bytes) && (tag_count[i] || tag_bytes[i]))
                    printf("churn %d %s=%d bytes, ", tag_count[i], tag_name[i], tag_bytes[i]);
            }
        }
    }
    
    for(i = 0; i<16; i++) {
        tag_count[i] = 0;
        tag_bytes[i] = 0;
        tag_freed_count[i] = 0;
        tag_freed_bytes[i] = 0;
    }

    // TODO: this one doesn't make sense?
    if (verbose) {
        printf("\nused_count=%d ", used_count);
        fflush(stdout);
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

char* getstring(lisp s) {
    return IS(s, string) ? ATTR(string, s, p) : NULL;
}

//#define MAX_CONS 137 // allows allocation of (list 1 2)
//#define MAX_CONS 1024
#define MAX_CONS 512
#define CONSES_BYTES (MAX_CONS * sizeof(conss))

conss* conses = NULL;
unsigned int cons_used[MAX_CONS/32 + 1] = { 0 };
lisp free_cons = 0;
int cons_count = 0; // TODO: remove, used for GC indication

#define CONS_SET_USED(i) ({int _i = (i); cons_used[_i/32] |= 1 << _i%32;})
#define CONS_IS_USED(i) ({int _i = (i); (cons_used[_i/32] >> _i%32) & 1;})

// TODO: as alternative to free list we could just use the bitmap
// this would allow us to allocate adjacent elements!
void gc_conses() {
    // make first pointer point to first position
    free_cons = nil;
    cons_count = 0;
    int i;
    for(i = MAX_CONS - 1; i >= 0; i--) {
        if (!CONS_IS_USED(i)) {
            conss* c = &conses[i];
            //if (c->car && c->car == _FREE_) continue; // already in free list...
            c->car = _FREE_;
            c->cdr = free_cons;
            free_cons = MKCONS(c);
            //printf("%u CONS=%u CONSP=%d\n", (int)c, (int)free_cons, CONSP(free_cons));
            cons_count++;
        }
    }
    memset(cons_used, 0, sizeof(cons_used));
}

void gc_cons_init() {
    conses = malloc(CONSES_BYTES); // we malloc, so it's pointer starts at xxx000
    memset(conses, 0, CONSES_BYTES);
    memset(cons_used, 0, sizeof(cons_used));
    gc_conses();
}

lisp cons(lisp a, lisp b) {
    conss* c = GETCONS(free_cons);
    cons_count--;
    if (!c) {
        printf("Run out of conses\n");
        exit(1);
    }
    if (cons_count < 0) {
        printf("Really ran out of conses\n");
        // TODO: shouldn't get here, should have been caught above...
        exit(1);
    }
    if (c->car != _FREE_) {
        printf("Conses corruption error %u ... %u CONSP=%d\n", (int)c, (int)free_cons, CONSP(free_cons));
        printf("CONS="); princ((lisp)c); terpri();
        exit(1);
    }
    // TODO: this is updating counter in myMalloc stats, maybe refactor...
    if (0) { // TOOD: enable this and it becomes very slow!!!!??? why compared to myMalloc shouldn't????
    used_count++;
    tag_count[conss_TAG]++;
    tag_bytes[conss_TAG] += sizeof(conss);
    tag_count[0]++;
    tag_bytes[0] += sizeof(conss);
    }

    free_cons = c->cdr;

    c->car = a;
    c->cdr = b;
    return MKCONS(c);
}

// inline works on both unix/gcc and c99 for esp8266
inline lisp car(lisp x) { return CONSP(x) ? GETCONS(x)->car : nil; }
inline lisp cdr(lisp x) { return CONSP(x) ? GETCONS(x)->cdr : nil; }

// however, on esp8266 it's only inlined and no function exists,
// so we need to create them for use in lisp
#ifdef UNIX
  #define car_ car
  #define cdr_ cdr
#else
  lisp car_(lisp x) { return car(x); }
  lisp cdr_(lisp x) { return cdr(x); }
#endif

lisp setcar(lisp x, lisp v) { return IS(x, conss) ? GETCONS(x)->car = v : nil; }
lisp setcdr(lisp x, lisp v) { return IS(x, conss) ? GETCONS(x)->cdr = v : nil; }

lisp list(lisp first, ...) {
    va_list ap;
    lisp r = nil;
    // points to cell where cdr is next pos
    lisp last = r;
    lisp x;

    va_start(ap, first);
    for (x = first; x != (lisp)-1; x = va_arg(ap, lisp)) {
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

unsigned int allprims = 0;
//const char* const foobar = "FOOBAR";

lisp mkprim(char* name, int n, void *f) {
    // TODO: can't make it save in a cons cell? actually no need GC, same as SYM
    // can we use the range of program memory to recognize a function pointer???
    prim* r = ALLOC(prim);
    r->name = name; // symbol(name); // possible the strings already exists in ROM/RAM... so no saving?
    r->n = n;
    r->f = f;
    allprims |= (unsigned int)f;
// TEST to investigate function pointers...
//    printf("PRIM %x   %s prims=%x conses=%x allocs=%x stack=%x vars=%x consstr=%x\n",
//           (unsigned int)f, name, allprims, (unsigned int)conses, (unsigned int)allocs, (unsigned int)&r, (unsigned int)&allprims,
//           (unsigned int)foobar
//        );
    return (lisp)r;
}

lisp eval(lisp e, lisp* env);
lisp eq(lisp a, lisp b);

lisp member(lisp e, lisp r) {
    while (r) {
        if (eq(e, car(r))) return r;
        r = cdr(r);
    }
    return nil;
}

lisp symbol(char* s);
lisp funcall(lisp f, lisp args, lisp* envp, lisp e, int noeval);
lisp quote(lisp x);

// echo '
// (wget "yesco.org" "http://yesco.org/index.html" (lambda (t a v) (princ t) (cond (a (princ " ") (princ a) (princ "=") (princ v)(terpri)))))
// ' | ./run

lisp out(lisp pin, lisp value) {
    gpio_enable(getint(pin), GPIO_OUTPUT);
    gpio_write(getint(pin), getint(value));
    return value;
}

lisp in(lisp pin) {
    gpio_enable(getint(pin), GPIO_INPUT);
    return mkint(gpio_read(getint(pin)));
}

//    gpio_set_interrupt(gpio, int_type);

// wget functions...

static void f_emit_text(lisp callback, char* path[], char c) {
//    return;
    lisp env = cdr(callback);
    char s[2] = {0};
    s[0] = c;
    evalGC(list(callback, mkstring(s), END), &env);
}

static void f_emit_tag(lisp callback, char* path[], char* tag) {
    lisp env = cdr(callback);
    evalGC(list(callback, quote(symbol(tag)), END), &env);
}

static void f_emit_attr(lisp callback, char* path[], char* tag, char* attr, char* value) {
    lisp env = cdr(callback);
    evalGC(list(callback, quote(symbol(tag)), quote(symbol(attr)), mkstring(value), END), &env);
}

lisp wget_(lisp server, lisp url, lisp callback) {
    wget_data data;
    memset(&data, 0, sizeof(data));
    data.userdata = callback;
    data.xml_emit_text = (void*)f_emit_text;
    data.xml_emit_tag = (void*)f_emit_tag;
    data.xml_emit_attr = (void*)f_emit_attr;

    wget(&data, getstring(url), getstring(server));
    return nil;
}

// web functions...

// Generalize, similarly to xml stuff, with userdata etc, in order to handle several servers
static lisp web_callback = NULL;

static void header(char* buff, char* method, char* path) {
    lisp env = cdr(web_callback);
    // TODO: consider printing the returned value, need header(req, ..., need updatable state?)
    evalGC(list(web_callback, quote(symbol("header")), mkstring(buff), quote(symbol(method)), mkstring(path), END), &env);
}

static void body(char* buff, char* method, char* path) {
    lisp env = cdr(web_callback);
    // TODO: consider printing the returned value, need header(req, ..., need updatable state?)
    evalGC(list(web_callback, quote(symbol("body")), mkstring(buff), quote(symbol(method)), mkstring(path), END), &env);
}

static void response(int req, char* method, char* path) {
    lisp env = cdr(web_callback);
    lisp ret = evalGC(list(web_callback, nil, quote(symbol(method)), mkstring(path), END), &env);

    char* s = getstring(ret);
    write(req, s, strlen(s));
}

// echo '
// (web 8080 (lambda (w s m p) (princ w) (princ " ") (princ s) (princ " ") (princ m) (princ " ") (princ p) (terpri) "FISH-42"))
// ' | ./run

lisp web(lisp port, lisp callback) {
    //wget_data data;
    //memset(&data, 0, sizeof(data));
    //data.userdata = callback;
    //data.xml_emit_text = (void*)f_emit_text;
    //data.xml_emit_tag = (void*)f_emit_tag;
    //data.xml_emit_attr = (void*)f_emit_attr;

    int s = httpd_init(getint(port));
    if (s < 0) { printf("ERROR.errno=%d\n", errno); return nil; }

    // TODO: make it non-blocking, put it on list of functions to call between evals?
    // can't really do background processing as concurrent uncontrolled GC might be SHIT!
    web_callback = callback;

    while (1) {
        httpd_next(s, header, body, response);
    }
}

// lookup binding of symbol variable name (not work for int names)
lisp assoc(lisp name, lisp env) {
    while (env) {
        lisp bind = car(env);
        // only works for symbol
        if (car(bind)==name) return bind; 
        // TODO: this is required for for example integer if BIGINT
        // if (eq(car(bind), name)) return bind;
        env = cdr(env);
    }
    return nil;
}

lisp evallist(lisp e, lisp* envp) {
    if (!e) return e;
    // TODO: don't recurse!
    return cons(eval(car(e), envp), evallist(cdr(e), envp));
}

lisp noEval(lisp x, lisp* envp) { return x; }

lisp primapply(lisp ff, lisp args, lisp* envp, lisp all, int noeval) {
    //printf("PRIMAPPLY "); princ(ff); princ(args); terpri();
    int n = ATTR(prim, ff, n);
    lisp (*e)(lisp x, lisp* envp) = (noeval && n > 0) ? noEval : evalGC;
    int an = abs(n);

    // these special cases are redundant, can be done at general solution
    // but for optimization we extracted them, it improves speed quite a lot
    if (n == 2) { // eq/plus etc
        lisp (*fp)(lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(e(car(args), envp), e(car(cdr(args)), envp)); // safe!
    }
    if (n == -3) { // if...
        lisp (*fp)(lisp*,lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(envp, car(args), car(cdr(args)), car(cdr(cdr(args))));
    }
    if (n == -1) { // quote...
        lisp (*fp)(lisp*,lisp) = ATTR(prim, ff, f);
        return (*fp)(envp, car(args));
    }
    if (n == 1) {
        lisp (*fp)(lisp) = ATTR(prim, ff, f);
        return (*fp)(e(car(args), envp));
    }
    if (n == 3) {
        lisp (*fp)(lisp,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(e(car(args), envp), e(car(cdr(args)), envp), e(car(cdr(cdr(args))),envp));
    }
    if (n == -16) { // lambda, quite uncommon
        lisp (*fp)(lisp*,lisp,lisp) = ATTR(prim, ff, f);
        return (*fp)(envp, args, all);
    }

    // don't do evalist, but allocate array, better for GC
    if (1 && an > 0 && an <= 6) {
        if (n < 0) an++; // add one for neval and initial env
        lisp argv[an];
        int i;
        for(i = 0; i < an; i++) {
            // if noeval, put env first
            if (i == 0 && n < 0) { 
  	        argv[0] = (lisp)envp;
                continue;
            }
            lisp a = car(args);
            if (a && n > 0) a = e(a, envp);
            argv[i] = a;
            args = cdr(args);
        }
        lisp (*fp)() = ATTR(prim, ff, f);
        switch (an) {
        case 1: return fp(argv[0]);
        case 2: return fp(argv[0], argv[1]);
        case 3: return fp(argv[0], argv[1], argv[2]);
        case 4: return fp(argv[0], argv[1], argv[2], argv[3]);
        case 5: return fp(argv[0], argv[1], argv[2], argv[3], argv[4]);
        case 6: return fp(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
        }
    }
    // above is all optimiziations

    // this is the old fallback solution, simple and works but expensive
    // prepare arguments
    if (n >= 0) {
        args = noeval ? args : evallist(args, envp);
    } else if (n > -16) { // -1 .. -15 no-eval lambda, put env first
        // TODO: for NLAMBDA this may not work...  may need a new lisp type
        args = cons(*envp, args);
    }

    lisp r;
    if (abs(n) == 16) {
        lisp (*fp)(lisp*, lisp, lisp) = ATTR(prim, ff, f);
        r = fp(envp, args, all);
    } else {
        lisp a = args, b = cdr(a), c = cdr(b), d = cdr(c), e = cdr(d), f = cdr(e), g = cdr(f), h = cdr(g), i = cdr(h), j = cdr(i);
        // with C calling convention it's ok, but maybe not most efficient...
        lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = ATTR(prim, ff, f);
        r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    }

    return r;
}

// don't call this directly, call symbol
lisp secretMkSymbol(char* s) {
    symboll* r = ALLOC(symboll);
    r->name = s;
    // link it in first
    r->next = (symboll*)symbol_list;
    symbol_list = r;
    return (lisp)r;
}

// in many basic lisp program most function names/symbols are 6 charcters or less
// few special characters are used (- _ ? /), if other fancy character used (< + etc)
//
// first try, len <= 6:
//   pointer = aaaaabbbbbcccccdddddeeeeefffff11 = 32 bits len <= 6 each char mapped to range 32 as per below
// if len <=3 or any character out of range:
//   pointer = aaaaaaabbbbbbbcccccccxxxx1111111 = 32 bits len <= 3 ascii characters 0-127
//
// encode char -> 0..31
//    \0 _ a-z  -  ?  /   3ASCII
//    0  1 2 27 28 29 30  31

static lisp str2sym3ascii(char* s, int len) {
    if (len > 3) return nil;
    unsigned int c0 = len >= 1 ? s[0] : 0;
    unsigned int c1 = (c0 && len >= 2) ? s[1] : 0;
    unsigned int c2 = (c1 && len >= 3) ? s[2] : 0;
    if (c0 < 0 || c1 < 0 || c2 < 0 || c0 > 127 || c1 > 127 || c2 > 127) return nil;
    unsigned int n = (c0 << (32-1*7)) + (c1 << (32-2*7)) + (c2 << (32-3*7)) + 127;
    return (lisp) n;
}


static lisp str2sym(char* s, int len) {
    if (len > 6) return nil; // too long
    if (len <= 3) return str2sym3ascii(s, len);
    unsigned int n = 0;
    int i;
    for(i = 0; i < 6; i++) {
        char c = (i < len) ? *(s+i) : 0;
        int e = c == '-' ?  28 : c == '?' ? 29 : c == '/' ? 30 : c ? c - '_' + 1 : 0;
        if (e < 0 || e > 32) return nil;
        n = n * 32 + e;
    }
    return (lisp) ((n << 2) | 3);
}

static void sym2str(lisp s, char name[7]) {
    if (!s) return;
    // 3ASCII?
    unsigned int n = (unsigned int) s;
    if (n % 128 == 127) {
        name[0] = (n >> (32-1*7)) % 128;
        name[1] = (n >> (32-2*7)) % 128;
        name[2] = (n >> (32-3*7)) % 128;
        name[3] = 0;
        return;
    }
    n /= 4;
    int i;
    for(i = 0; i < 6; i++) {
        int e = n % 32;
        int c = e == 28 ? '-' : e == 29 ? '?' : e == 30 ? '/' : e ? (e + '_' - 1) : 0;
        name[6 - 1 - i] = c;
        n /= 32;
    }
    name[6] = 0;
}

lisp find_symbol(char *s, int len) {
    lisp e = str2sym(s, len);
    if (e) return e;
    symboll* cur = (symboll*)symbol_list;
    while (cur) {
        if (strncmp(s, cur->name, len) == 0 && strlen(cur->name) == len)
	  return (lisp)cur;
        cur = cur->next;
    }
    return NULL;
}

// linear search to intern the string
// will always return same symbol
lisp symbol(char* s) {
    if (!s) return nil;
    lisp sym = find_symbol(s, strlen(s));
    if (sym) return sym;
    return secretMkSymbol(s);
}

// create a copy of partial string if not found
lisp symbolCopy(char* start, int len) {
    lisp sym = find_symbol(start, len);
    if (sym) return sym;
    return secretMkSymbol(strndup(start, len));
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
        // -- pointer contains tag
        if (INTP(next)) return;
        if (SYMP(next)) return;
	if (CONSP(next)) {
	    int i = (conss*)next - &conses[0];
            if (i >= MAX_CONS) {
                printf("mark.cons.funny i=%d    %u\n", i, (int)next);
                exit(1);
            }
            if (CONS_IS_USED(i)) return; // already checked!
  	    CONS_SET_USED(i);
  	    mark_deep(car(next), deep+1);
	    next = cdr(next);
	    continue;
	}
        // -- generic pointer to heap allocated object with type tag
        // optimization, we store index position in element
        int index = next->index;
        if (index < 0) return; // no tracked here
        if (index >= MAX_ALLOCS) {
            printf("\n--- ERROR: mark_deep - corrupted data p=%u   index=%d\n", (unsigned int)next, index);
            printf("VALUE="); princ(next); terpri();
        }

        lisp p = allocs[index];
        if (!p || p != next) {
            printf("\n-- ERROR: mark_deep - index %d doesn't contain pointer. p=%u\n", index, (unsigned int)next);
            //printf("pppp = 0x%u and next = 0x%u \n", p, next);
            //princ(next);
            //return;
        } 
        
        if (IS_USED(index)) return;

        SET_USED(index);
        //printf("Marked %i deep %i :: p=%u ", index, deep, p); princ(p); terpri();
            
        // follow pointers, recurse on one end, or set next for continue
        if (IS(p, symboll)) { // shouldn't be needed?
            next = (lisp)ATTR(symboll, (void*)p, next);
        } else if (IS(p, thunk) || IS(p, immediate) || IS(p, func)) {
            mark_deep(ATTR(thunk, p, e), deep+1);
            next = ATTR(thunk, p, env);
        }
    }
}

void mark(lisp x) {
    mark_deep(x, 1);
}

static lisp reads(char *s);

///--------------------------------------------------------------------------------
// Primitives
// naming convention:
// -- lisp foo()   ==   arguments like normal lisp function can be called from c easy
// -- lsp foo_()   ==   same as lisp but name clashes with stanrdard function read_
// -- lsp _foo()   ==   special function, order may be different (like it take env& as first)

// first one liners
lisp nullp(lisp a) { return a ? nil : t; }
lisp consp(lisp a) { return IS(a, conss) ? t : nil; }
lisp atomp(lisp a) { return IS(a, conss) ? nil : t; }
lisp stringp(lisp a) { return IS(a, string) ? nil : t; }
lisp symbolp(lisp a) { return IS(a, symboll) ? t : nil; } // rename struct symbol to symbol?
lisp numberp(lisp a) { return IS(a, intint) ? t : nil; } // TODO: extend with float/real
lisp integerp(lisp a) { return IS(a, intint) ? t : nil; }
lisp funcp(lisp a) { return IS(a, func) || IS(a, thunk) || IS(a, prim) ? t : nil; }

lisp lessthan(lisp a, lisp b) { return getint(a) < getint(b) ?  t : nil; }

lisp plus(lisp a, lisp b) { return mkint(getint(a) + getint(b)); }
lisp minus(lisp a, lisp b) { return mkint(getint(a) - getint(b)); }
lisp times(lisp a, lisp b) { return mkint(getint(a) * getint(b)); }
lisp divide(lisp a, lisp b) { return mkint(getint(a) / getint(b)); }
lisp mod(lisp a, lisp b) { return mkint(getint(a) % getint(b)); }

// TODO: http://www.gnu.org/software/emacs/manual/html_node/elisp/Input-Functions.html#Input-Functions
// http://www.lispworks.com/documentation/HyperSpec/Body/f_rd_rd.htm#read
lisp read_(lisp s) { return reads(getstring(s)); }
lisp terpri() { printf("\n"); return nil; }

// TODO: consider http://picolisp.com/wiki/?ArticleQuote
lisp _quote(lisp* envp, lisp x) { return x; }
lisp quote(lisp x) { return list(symbol("quote"), x, END); } // TODO: optimize
lisp _env(lisp* e, lisp all) { return *e; }

// longer functions
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

lisp equal(lisp a, lisp b) {
    while (a != b) {
        lisp r = eq(a, b);
        if (r) return r;
        char taga = TAG(a), tagb = TAG(b);
        if (taga != tagb) return nil;
        if (taga == string_TAG)
            return strcmp(getstring(a), getstring(b)) == 0 ? t : nil;
        if (taga != conss_TAG) return nil;
        // cons, iterate
        if (!eq(car(a), car(b))) return nil;
        a = cdr(a); b = cdr(b);
    }
    return t;
}

lisp _setqq(lisp* envp, lisp name, lisp v) {
    lisp bind = assoc(name, *envp);
    if (bind) {
      setcdr(bind, v);
    } else {
      *envp = cons( cons(name, v), *envp);
    }
    return v;
}

lisp _setq(lisp* envp, lisp name, lisp v) {
    lisp bind = assoc(name, *envp);
    if (!bind) {
        // need to create binding first for self recursive functions
        bind = cons(name, nil);
        *envp = cons(bind, *envp);
    }
    v = eval(v, envp);
    setcdr(bind, v);
    return v;
}

lisp _set(lisp* envp, lisp name, lisp v) {
    name = eval(name, envp);
    lisp bind = assoc(name, *envp);
    if (!bind) {
        // need to create binding first for self recursive functions
        bind = cons(name, nil);
        *envp = cons(bind, *envp);
    }
    v = eval(v, envp);
    setcdr(bind, v);
    return v;
}

lisp eval(lisp e, lisp* envp);

lisp apply(lisp f, lisp args) {
    lisp e = nil;
    lisp x = funcall(f, args, &e, nil, 1);
    // TODO: hmmm, combine/use in eval_hlp
    while (x && TAG(x) == immediate_TAG) {
        x = evalGC(ATTR(thunk, x, e), &ATTR(thunk, x, env));
    }
    return x;
}

lisp mapcar(lisp f, lisp r) {
    if (!r || !consp(r) || !funcp(f)) return nil;
    lisp v = apply(f, cons(car(r), nil));
    return cons(v, mapcar(f, cdr(r)));
}

lisp map(lisp f, lisp r) {
    while (r && consp(r) && funcp(f)) {
        apply(f, cons(car(r), nil));
        r = cdr(r);
    }
    return nil;
}

lisp length(lisp r) {
    int c = 0;
    while (r) {
        c++;
        r = cdr(r);
    }
    return mkint(c);
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
        input = NULL;
        return 0;
    }
    return *(input++);
}

static char next() {
    // for debuggin
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

static int readInt(int v) {
    unsigned char c = next();
    while (c && isdigit(c)) {
        v = v*10 + c-'0';
        c = next();
    }
    nextChar = c;
    return v;
}

static lisp readString() {
    char* start = input;
    char c = next();
    while (c && c != '"') c = next();
    
    int len = input - start - 1;
    return mklenstring(start, len);
}

static lisp readSymbol(char c, int o) {
    // TODO: cleanup, ugly
   if (!input) {
       char s[2] = {'-', 0};
       return symbolCopy(s, 1);
    }
    char* start = input - 1 + o;
    int len = 0;
    while (c && c!='(' && c!=')' && c!=' ' && c!='.') {
        len++;
	c = next();
    }
    nextChar = c;
    return symbolCopy(start, len-o);
}

static lisp readx();

static lisp readList() {
    skipSpace();

    char c = next();
    if (!c) return nil;
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
    unsigned char c = next();
    if (!c) return NULL;
    if (c == '(') return readList();
    if (c == ')') return nil;
    if (isdigit(c)) return mkint(readInt(c - '0'));
    if (c == '-') {
        unsigned char n = next();
        if (isdigit(n))
            return mkint(-readInt(n - '0'));
        else
            return readSymbol(n, -1);
    }
    if (c == '"') return readString();
    return readSymbol(c, 0);
}

static lisp reads(char *s) {
    input = s;
    nextChar = 0;
    return readx();
}

// TODO: prin1, princ, print, pprint/pp by calling
//    (write o output-stream= base= escape= level= circle= lines= pretty= readably=)
//
// http://www.gnu.org/software/emacs/manual/html_node/elisp/Output-Functions.html
// http://www.lispworks.com/documentation/HyperSpec/Body/f_wr_pr.htm
//
// (prin1 object output-stream)
//     ==  (write object :stream output-stream :escape t)
// (princ object output-stream)
//     ==  (write object stream output-stream :escape nil :readably nil)
// (print object output-stream)
//     ==  (progn (terpri output-stream)
//                (write object :stream output-stream :escape t)
//                (write-char #\space output-stream))
// (pprint object output-stream)
//     ==  (write object :stream output-stream :escape t :pretty t)
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
    // for now we have two "symbolls" one inline in pointer and another heap allocated
    else if (SYMP(x)) { char s[7] = {0}; sym2str(x, s); printf("%s", s); }
    else if (tag == symboll_TAG) printf("%s", ATTR(symboll, x, name));
    else if (tag == thunk_TAG) { printf("#thunk["); princ(ATTR(thunk, x, e)); putchar(']'); }
    else if (tag == immediate_TAG) { printf("#immediate["); princ(ATTR(thunk, x, e)); putchar(']'); }
    else if (tag == func_TAG) { printf("#func["); /* princ(ATTR(thunk, x, e)); */ putchar(']'); } // circular...
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

static lisp funcapply(lisp f, lisp args, lisp* envp, int noeval);

static lisp getvar(lisp e, lisp env) {
    lisp v = assoc(e, env); 
    if (v) return cdr(v);
    printf("\n-- ERROR: Undefined symbol: "); princ(e); terpri();
    //printf("ENV= "); princ(env); terpri();
    // TODO: "throw error"?
    return nil;
}

inline static lisp eval_hlp(lisp e, lisp* envp) {
    if (!e) return e;
    char tag = TAG(e);
    if (tag == symboll_TAG) return getvar(e, *envp);
    if (tag != conss_TAG) return e;

    // find function
    lisp orig = car(e);
    lisp f = orig;
    tag = TAG(f);
    while (f && tag!=prim_TAG && tag!=thunk_TAG && tag!=func_TAG && tag!=immediate_TAG) {
        f = evalGC(f, envp);
        tag = TAG(f);
    }
    if (f != orig) {
        // "macro expansion" lol (replace with implementation)
        // TODO: not safe if found through variable (like all!)
        // TODO: keep on symbol ptr to primitive function/global, also not good?
        // DEF(F,...) will then break many local passed variables
        // maybe must search all list till find null, then can look on symbol :-(
        // but that's everytime? actually, not it's a lexical scope!
        // TODO: only replace if not found in ENV and is on an SYMBOL!
        setcar(e, f);
    }

    return funcall(f, cdr(e), envp, e, 0);
}

static void mymark(lisp x) {
    if (dogc) mark(x);
}

static void mygc() {
    if (dogc) gc(NULL);
}

static int blockGC = 0;

// this is a safe eval to call from anywhere, it will not GC
// but it may blow up the stack, or the heap!!!
lisp eval(lisp e, lisp* envp) {
    blockGC++;
    lisp r = evalGC(e, envp);
    blockGC--;
    return r;
}

#define MAX_STACK 256
static struct stack {
    lisp e;
    lisp* envp;
} stack[MAX_STACK];

// prints env and stops at (nil . nil) binding (hides "globals")
void print_env(lisp env) {
    indent(level+1);
    printf(" ENV ");
    lisp xx = env;
    while (xx && car(car(xx))) {
        lisp b = car(xx);
	princ(car(b)); putchar('='); princ(cdr(b)); putchar(' ');
	xx = cdr(xx);
    }
    terpri();
}

lisp mem_usage(int count) {
    // TODO: last nubmer conses not correct new usaga
    if (traceGC) printf(" [GC freed %d used=%d bytes=%d conses=%d]\n", count, used_count, used_bytes, MAX_CONS - cons_count);
    return nil;
}

int needGC() {
    if (cons_count < MAX_CONS * 0.2) return 1;
    //printf("cons_count = %d and not need GC\n", cons_count);
    return (used_count < MAX_ALLOCS * 0.8) ? 0 : 1;
}

lisp evalGC(lisp e, lisp* envp) {
    if (!e) return e;
    char tag = TAG(e);
    // look up variable
    if (tag == symboll_TAG) return getvar(e, *envp); 
    if (tag != symboll_TAG && tag != conss_TAG && tag != thunk_TAG) return e;

    if (level >= MAX_STACK) {
        printf("%%Stack blowup! You're royally screwed! why does it still work?\n");
        #ifdef UNIX
          exit(1);
        #endif
        return nil;
    }

    stack[level].e = e;
    stack[level].envp = envp;

    // TODO: move this to function
    if (!blockGC && needGC()) {
        mymark(*envp);
        if (trace) printf("%d STACK: ", level);
        int i;
        for(i=0; i<64; i++) {
            if (!stack[i].e) break;
            mymark(stack[i].e);
            mymark(*stack[i].envp);
            if (trace) {
                printf(" %d: ", i);
                princ(stack[i].e);
            }
        }
        if (trace) terpri();
        mygc(); 
    }

    if (trace) { indent(level); printf("---> "); princ(e); terpri(); }
    level++;
    //if (trace) { indent(level+1); printf(" ENV= "); princ(env); terpri(); }
    if (trace) print_env(*envp);

    lisp r = eval_hlp(e, envp);
    while (r && TAG(r) == immediate_TAG) {
        lisp tofree = r;
	// TODO: figure out if thunk->env should be thunk->envp ???
        if (trace) // make it visible
            r = evalGC(ATTR(thunk, r, e), &ATTR(thunk, r, env));
        else
            r = eval_hlp(ATTR(thunk, r, e), &ATTR(thunk, r, env));
        // immediates are immediately consumed after evaluation, so they can be free:d directly
        tofree->tag = 0;
        sfree((void*)tofree, sizeof(thunk), immediate_TAG);
        used_count--; // TODO: move to sfree?
    }

    --level;
    if (trace) { indent(level); princ(r); printf(" <--- "); princ(e); terpri(); }

    stack[level].e = nil;
    stack[level].envp = NULL;
    return r;
}

lisp iff(lisp* envp, lisp exp, lisp thn, lisp els) {
    // evalGC is safe here as we don't construct any structes, yet
    // TODO: how did we get here? primapply does call evallist thus created something...
    // but we pass ENV on so it should be safe..., it'll mark it!
    return evalGC(exp, envp) ? mkimmediate(thn, *envp) : mkimmediate(els, *envp);
}

lisp progn(lisp* envp, lisp all);

lisp cond(lisp* envp, lisp all) {
    while (all) {
        lisp nxt = car(all);
        lisp e = car(nxt);
        e = evalGC(e, envp);
        lisp thn = cdr(nxt);
        if (e) return thn ? progn(envp, thn) : e;
        all = cdr(all);
    }
    return nil;
}

lisp and(lisp* envp, lisp all) {
    lisp r = nil;
    while(all) {
        // TODO: tail call on last?
        r = evalGC(car(all), envp);
        if (!r) return nil;
        all = cdr(all);
    }
    return r;
}

lisp or(lisp* envp, lisp all) {
    lisp r = nil;
    while(all) {
        // TODO: tail call on last?
        r = evalGC(car(all), envp);
        if (r) return r;
        all = cdr(all);
    }
    return nil;
}

lisp not(lisp x) {
    return x ? nil : t;
}

// essentially this is a quote but it stores the environment so it's a closure!
lisp lambda(lisp* envp, lisp all) {
    return mkfunc(all, *envp);
}

lisp progn(lisp* envp, lisp all) {
    while (all && cdr(all)) {
        evalGC(car(all), envp);
        all = cdr(all);
    }
    // only last form needs be tail recursive..., or if have "return"?
    return mkimmediate(car(all), *envp);
}

// use bindEvalList unless NLAMBDA
lisp bindList(lisp fargs, lisp args, lisp env) {
    // TODO: not recurse!
    if (!fargs) return env;
    lisp b = cons(car(fargs), car(args));
    return bindList(cdr(fargs), cdr(args), cons(b, env));
}

lisp bindEvalList(lisp fargs, lisp args, lisp* envp, lisp extend) {
    while (fargs) {
        lisp b = cons(car(fargs), eval(car(args), envp));
        extend = cons(b, extend);
        fargs = cdr(fargs);
        args = cdr(args);
    }
    return extend;
}

lisp funcapply(lisp f, lisp args, lisp* envp, int noeval) {
    lisp lenv = ATTR(thunk, f, env);
    lisp l = ATTR(thunk, f, e);
    //printf("FUNCAPPLY:"); princ(f); printf(" body="); princ(l); printf(" args="); princ(args); printf(" env="); princ(lenv); terpri();
    lisp fargs = car(l);
    // TODO: check if NLAMBDA!
    lenv = noeval ? bindList(fargs, args, lenv) : bindEvalList(fargs, args, envp, lenv);
    //printf("NEWENV: "); princ(lenv); terpri();
    return progn(&lenv, cdr(l)); // tail recurse on rest
}

// TODO: evals it's arguments, shouldn't... 
// TODO: prim apply/funcapply may return immediate, it should be handled and not returned from here?
lisp funcall(lisp f, lisp args, lisp* envp, lisp e, int noeval) {
    int tag = TAG(f);
    if (tag == prim_TAG) return primapply(f, args, envp, e, noeval);
    if (tag == func_TAG) return funcapply(f, args, envp, noeval);
    if (tag == thunk_TAG) return f; // ignore args

    printf("\n-- ERROR: "); princ(f); printf(" is not a function: "); printf(" in "); princ(e ? e : args); terpri();
    return nil;
}

// User, macros, assume a "globaL" env variable implicitly, and updates it
#define SET(sname, val) _setq(envp, symbol(#sname), val)
#define SETQc(sname, val) _setq(envp, symbol(#sname), val)
#define SETQ(sname, val) _setq(envp, symbol(#sname), reads(#val))
#define SETQQ(sname, val) _setq(envp, symbol(#sname), quote(reads(#val)))
#define DEF(fname, sbody) _setq(envp, symbol(#fname), reads(#sbody))
#define EVAL(what) eval(reads(#what), envp)
#define PRINT(what) ({ princ(EVAL(what)); terpri(); })
#define SHOW(what) ({ printf(#what " => "); princ(EVAL(what)); terpri(); })
#define TEST(what, expect) testss(envp, #what, #expect)
#define PRIM(fname, argn, fun) _setq(envp, symbol(#fname), mkprim(#fname, argn, fun))

static lisp test(lisp*);

char* readline(char* prompt, int maxlen);

// returns an env with functions
lisp lisp_init() {
    // enable to observer startup sequence
    if (1) {
        char* f = readline("start lisp>", 1);
        free(f);
    }
    print_memory_info(2); // init by first call

    lisp env = nil;
    lisp* envp = &env;

    // free up and start over...
    dogc = 0;

    // TODO: this is a leak!!!
    allocs_count = 0;

    // need to before gc_cons_init()...
    _FREE_ = symbol("*FREE*");

    // setup gc clean slate
    mark_clean();
    gc_cons_init();
    gc(NULL);

    t = symbol("t");

    // nil = symbol("nil"); // LOL? TODO:? that wouldn't make sense? then it would be taken as true!
    LAMBDA = mkprim("lambda", -16, lambda);

    SETQc(lambda, LAMBDA);
    SETQ(t, 1);
    SETQ(nil, nil);

    PRIM(null?, 1, nullp);
    PRIM(cons?, 1, consp);
    PRIM(atom?, 1, atomp);
    PRIM(string?, 1, stringp);
    PRIM(symbol?, 1, symbolp);
    PRIM(number?, 1, numberp);
    PRIM(integer?, 1, integerp);
    PRIM(func?, 1, funcp);

    PRIM(<, 2, lessthan);

    PRIM(+, 2, plus);
    PRIM(-, 2, minus);
    PRIM(*, 2, times);
    PRIM(/, 2, divide);
    PRIM(%, 2, mod);
    PRIM(eq, 2, eq);
    PRIM(equal, 2, equal);
    PRIM(=, 2, eq);
    PRIM(if, -3, iff);
    PRIM(cond, -16, cond);
    PRIM(and, -16, and);
    PRIM(or, -16, or);
    PRIM(not, 1, not);
    PRIM(terpri, 0, terpri);
    PRIM(princ, 1, princ);

    PRIM(cons, 2, cons);
    PRIM(car, 1, car_);
    PRIM(cdr, 1, cdr_);
    PRIM(setcar, 2, setcar);
    PRIM(setcdr, 2, setcdr);

    PRIM(list, 16, _quote);
    PRIM(length, 1, length);
    PRIM(assoc, 2, assoc);
    PRIM(member, 2, member);
    PRIM(mapcar, 2, mapcar);
    PRIM(map, 2, map);
    // PRIM(quote, -16, quote);
    // PRIM(list, 16, listlist);

    PRIM(progn, -16, progn);
    PRIM(eval, 1, eval);
    PRIM(evallist, 2, evallist);
    PRIM(apply, 2, apply);
    PRIM(env, -16, _env);

    PRIM(read, 1, read_);

    PRIM(set, -2, _set);
    PRIM(setq, -2, _setq);
    PRIM(setqq, -2, _setqq);
    PRIM(quote, -1, _quote);

    // define
    // defun
    // defmacro
    // while
    // gensym

    // network
    PRIM(wget, 3, wget_);
    PRIM(web, 2, web);
    PRIM(out, 2, out);
    PRIM(in, 1, in);

    // system stuff
    PRIM(gc, -1, gc);
    PRIM(test, -16, test);

    // another small lisp in 1K lines
    // - https://github.com/rui314/minilisp/blob/master/minilisp.c

    // neat "trick" - limit how much 'trace on' will print by injecting nil bound to nil
    // in evalGC function the print ENV stops when arriving at this token, thusly
    // will not show any variables defined above...
    env = cons( cons(nil, nil), env );

    dogc = 1;

    print_memory_info(2); // summary of init usage
    return env;
}

char* readline(char* prompt, int maxlen) {
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    char buffer[maxlen+1];
    char ch;
    int i = 0;
    // The thread will block here until there is data available
    // NB. read(...) may be called from user_init or from a thread
    // We can check how many characters are available in the RX buffer
    // with uint32_t uart0_num_char(void);
    while (read(0, (void*)&ch, 1)) { // 0 is stdin
        if (ch == '\b' || ch == 0x7f) {
            if (i > 0) {
                i--;
                printf("\b \b"); fflush(stdout);
            }
            continue;
        }

        int eol = (ch == '\n' || ch == '\r');
        if (!eol) {
            putchar(ch); fflush(stdout);
            buffer[i++] = ch;
        }
        if (i == maxlen) printf("\nWarning, result truncated at maxlen=%d!\n", maxlen);
        if (i == maxlen || eol) {
            buffer[i] = 0;
            printf("\n");
            return strdup(buffer);
        }
    }
    return NULL;
}

void hello() {
    printf("\n\nWelcome to esp-lisp!\n");
    printf("2015 (c) Jonas S Karlsson under MPL 2.0\n");
    printf("Read more on https://github.com/yesco/esp-lisp/\n");
    printf("\n");
}

void help(lisp* envp) {
    printf("\n\nDocs - https://github.com/yesco/esp-lisp/\n");
    printf("ENV: ");
    PRINT((mapcar car (env)));
    terpri();
    printf("Commands: hello/help/trace on/trace off/gc on/gc off/wifi SSID PSWD/wget SERVER URL/mem EXPR\n");
    terpri();
}

void run(char* s, lisp* envp) {
    lisp r = reads(s);
    princ(evalGC(r, envp)); terpri();
    // TODO: report parsing allocs separately?
    // mark(r); // keep history?
    gc(envp);
}

void readeval(lisp* envp) {
    hello();

    while(1) {
        char* ln = readline("lisp> ", READLINE_MAXLEN);

        if (!ln) {
            break;
        } else if (strcmp(ln, "hello") == 0) {
            hello();
        } else if (strcmp(ln, "help") == 0 || ln[0] == '?') {
            help(envp);
        } else if (strcmp(ln, "gc on") == 0) {
            traceGC = 1;
        } else if (strcmp(ln, "gc off") == 0) {
            traceGC = 0;
        } else if (strcmp(ln, "trace on") == 0) {
            trace = 1;
        } else if (strcmp(ln, "trace off") == 0) {
            trace = 0;
        } else if (strncmp(ln, "wifi ", 5) == 0) {
            strtok(ln, " "); // skip wifi
            char* ssid = strtok(NULL, " "); if (!ssid) ssid = "dsl";
            char* pass = strtok(NULL, " "); if (!pass) pass = "0xdeadbeef";
            connect_wifi(ssid, pass);
        } else if (strncmp(ln, "wget ", 5) == 0) {
            strtok(ln, " "); // skip wget
            char* server = strtok(NULL, " "); if (!server) server = "yesco.org";
            char* url = strtok(NULL, " ");    if (!url) url = "http://yesco.org/index.html";
            printf("SERVER=%s URL=%s\n", server, url);
            int r = http_get(url, server);
            printf("GET=> %d\n", r);
        } else if (strncmp(ln, "web ", 4) == 0) {
            strtok(ln, " "); // skip web
            char* ports = strtok(NULL, " ");
            int port = ports ? atoi(ports) : 8080;
            printf("WEBSERVER on port=%d\n", port);
            int s = httpd_init(port);
            if (s < 0) printf("WEBSERVER.errno=%d: s=%d\n", errno, s);
            while(1) {
                httpd_loop(s);
            }
        } else if (strncmp(ln, "mem ", 4) == 0) {
            char* e = ln + 4;
            if (*e) {
                print_memory_info(0);
                run(e, envp);
            }
            print_memory_info(2);
        } else if (strcmp(ln, "mem") == 0) {
            print_memory_info(1);
        } else if (strlen(ln) > 0) { // lisp
            print_memory_info(0);
            run(ln, envp);
            //print_memory_info(1);
        }

        free(ln);
    }

    printf("OK, bye!\n");
}

void treads(char* s) {
    printf("\nread-%s: ", s);
    princ(reads(s));
    terpri();
}
    
int fibo(int n) {
    if (n < 2) return 1;
    else return fibo(n-1) + fibo(n-2);
}

// lisp implemented library functions hardcoded
void load_library(lisp* envp) {
    DEF(fibo, (lambda (n) (if (< n 2) 1 (+ (fibo (- n 1)) (fibo (- n 2))))));
}

void testc(lisp* envp , char* whats, lisp val, lisp expect) {
    printf("TEST: %s\n=> ", whats);
    lisp r = val;
    princ(r);
    printf("\nexpected: "); princ(expect); terpri();
    printf("status: "); printf("%s\n\n", equal(r, expect) ? "passed" : "failed");
}

void testee(lisp* envp , lisp what, lisp expect) {
    printf("TEST: "); princ(what); printf("\n=> ");
    lisp r = eval(what, envp);
    princ(r);
    printf("\nexpected: "); princ(expect); terpri();
    printf("status: "); printf("%s\n\n", equal(r, expect) ? "passed" : "failed");
}

void testss(lisp* envp , char* what, char* expect) {
    testee(envp, reads(what), reads(expect));
}

// TODO: implement, (port 8080) => p, (listen p) (http @) (close @)
//   https://github.com/SuperHouse/esp-open-rtos/commit/147257efa472307608019f04f38f8ebadadd7c01
//   http://john.freml.in/teepeedee2-vs-picolisp
//   http://picolisp.com/wiki/?ErsatzWebApp

static lisp test(lisp* e) {

// removing the body of this function gives: (- 42688 41152) 1536
// gives 25148 bytes, 19672k
#ifdef REMOVED
// enabling this take 1K from RAM :-(
//   also: -rw-r--r-- 1 knoppix knoppix  43712 Nov  6 20:12 0x00000.bin 
//   becomes 43712 instead of 42688
//static IROM 
//const char const xxx[] = "===============================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================================\n";
//    printf(&xxx[0]);

    lisp env = *e;
    lisp* envp = &env; // make local, don't leak out!

    terpri();

    TEST(nil, nil);
    TEST(42, 42);

    // make sure have one failure
    TEST(t, fail);

    // equal
    //testee(list(symbol("eq"), mkint(42), mkint(42), END), t);

    TEST((equal nil nil), t);
    TEST((equal 42 42), t);
    TEST((equal (list 1 2 3) (list 1 2 3)), t);
    TEST((equal "foo" "bar"), nil);
    TEST((equal "foo" "foo"), t);
    TEST((equal equal equal), t);

    // list & read function
    testee(envp, list(nil, mkstring("fihs"), mkint(1), symbol("fish"), mkint(2), mkint(3), mkint(4), nil, nil, nil, END),
           reads("(nil fihs 1 fish 2 3 4 nil nil nil)"));

    // misc
    testss(envp, "123", "123");
    TEST("()", "nil");
    TEST("(1)", "(1)");
    TEST("(1 2)", (1 2));
    treads("(1 2 3)");
    treads("((1) (2) (3))");
    treads("(A)");
    treads("(A B)");
    treads("(A B C)");
    printf("\n3=3: "); princ(eq(mkint(3), mkint(3)));
    printf("\n3=4: "); princ(eq(mkint(3), mkint(4)));
    printf("\na=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\na=b: "); princ(eq(symbol("a"), symbol("b")));
    printf("\na=a: "); princ(eq(symbol("a"), symbol("a")));
    printf("\n");

    treads("(lambda (n) if (eq n 0) (* n (fac (- n 1))))");

    // read
    TEST((read "foo"), foo);
    TEST((read "(+ 3 4)"), (+ 3 4));
    TEST((number? (read "42")), t);
    
    // set, setq, setqq
    TEST((setq a (+ 3 4)), 7);
    TEST((setqq b a), a);
    TEST(b, a);
    TEST((set b 3), 3);
    TEST(a, 3);
    TEST(b, a);
       
    // if
    lisp IF = mkprim("if", -3, iff);
    testee(envp, IF, IF);
    testee(envp, cons(IF, cons(mkint(7), cons(mkint(11), cons(mkint(22), nil)))), mkint(11));
    testee(envp, cons(IF, cons(nil, cons(mkint(11), cons(mkint(22), nil)))), mkint(22));
    TEST((if 7 11 22), 11);
    TEST((if nil 11 22), 22);
    TEST((if nil 11 22 33), 22);

    // thunk
    lisp th = mkthunk(mkint(14), NULL);
    testee(envp, th, th); // eval(thunk)
    testee(envp, cons(th, nil), th); // (eval (thunk))

    // lambda
    TEST((func? (lambda (n) 37)), t);
    TEST(((lambda (n) 37) 99), 37);
    TEST(((lambda (n) n) 99), 99);
    TEST(((lambda (a) ((lambda (n) (+ n a)) 33)) 66), 99); // lexical scoping

    // recursion
    DEF(fac, (lambda (n) (if (= n 0) 1 (* n (fac (- n 1))))));
    TEST((fac 6), 720);
    TEST((fac 21), 952369152);

    // tail recursion optimization test (don't blow up stack!)
    DEF(bb, (lambda (b) (+ b 3)));
    DEF(aa, (lambda (a) (bb a)));
    TEST((aa 7), 10);

    DEF(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
    TEST(tail, xyz);
    testss(envp, LOOPTAIL, LOOPS);

    // progn, progn tail recursion
    TEST((progn 1 2 3), 3);
    TEST((setq a nil), nil);
    TEST((progn (setq a (cons 1 a)) (setq a (cons 2 a)) (setq a (cons 3 a))),
         (3 2 1));

    // implicit progn in lambda
    DEF(f, (lambda (n) (setq n (+ n 1)) (setq n (+ n 1)) (setq n (+ n 1))));
    TEST((f 0), 3);

//    PRINT((setq tailprogn (lambda (n) (progn 3 2 1 (if (= n 0) (quote ok) (tailprogn (- n 1)))))));
//    TEST(tailprogn, 3);
//    TEST((tailprogn 10000), ok);

    // cond
    TEST((cond), nil);
    TEST((cond (7)), 7);
    TEST((cond (1 2 3)), 3);
    TEST((cond (nil 7)), nil);
    TEST((cond (nil 7)(2 3 4)(7 99)), 4);
    TEST((cond (nil 7)((eq 3 5) 9)((eq 5 5) 77)), 77);

    TEST((and), nil);
    TEST((and 1 2 3), 3);
    TEST((and 1 nil 3), nil);
    TEST((and 1 (eq 3 3) 7), 7);
    TEST((and 1 (eq 3 4) 7), nil);

    TEST((or), nil);
    TEST((or 1 2 3), 1);
    TEST((or nil 1 3), 1);
    TEST((or (eq 3 3) 7), t);
    TEST((or (eq 3 4) 7), 7);

    // mapcar
    TEST((mapcar (lambda (x) (+ 5 x)) (list 1 2 3)), (6 7 8));
    TEST((mapcar car (list (cons 1 2) (cons 3 4) (cons 5 6))), (1 3 5));
    TEST((mapcar cdr (list (cons 1 2) (cons 3 4) (cons 5 6))), (2 4 6));

#endif
    return nil;
}

void lisp_run(lisp* envp) {
    load_library(envp);
    readeval(envp);
    return;
}

// find this display for chinese price - http://digole.com/index.php?productID=1208 (17.89 USD)
//
// esp8266 st7735 lcd/tft display driver ucglib
//
// https://github.com/spapadim/ESPClock
//
// https://github.com/bblanchon/ArduinoJson
//
// http://www.pjrc.com/teensy/td_libs_Time.html
//
// https://github.com/spapadim/ESPClock
//
// https://gist.github.com/spapadim/a4bc258df47f00831006

// http://stackoverflow.com/questions/8751264/memory-mapped-using-linker
// https://github.com/esp8266/esp8266-wiki/wiki/Memory-Map
// http://esp8266-re.foogod.com/wiki/Memory_Map#Data_RAM_.280x3FFE8000_-_0x3FFFFFFF.29
