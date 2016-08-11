/* Distributed under Mozilla Public Licence 2.0   */
/* https://www.mozilla.org/en-US/MPL/2.0/         */
/* 2016-03-15 (C) Jonas S Karlsson, jsk@yesco.org */
/* A mini "lisp machine", main                    */

// http://stackoverflow.com/questions/3482389/how-many-primitives-does-it-take-to-build-a-lisp-machine-ten-seven-or-five

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

// DEFINE(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
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

// 20151121: (time (fibo 24)) (5170 . xxx)
// 
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
#include <setjmp.h>

// #include "esp/spi.h"

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

#include "MAX7219_font.h"

// use pointer to store some tag data, use this for exteded types
// last bits (3 as it allocates in at least 8 bytes boundaries):
// ----------
// 000 = heap pointer, generic extended lisp data/struct with tag field - DONE
//  01 = integer << 2 - DONE
//  11 = inline symbol stored inside pointer! - DONE
//       32 bits = 6 chars * 5 bits = 30 bits + 11 or 3*ASCII(7)=28, if shifted
// ....fffff11 :  fffff != 11111 means 6 char atom name inline
// 00001111111 :  fffff == 11111 means 3 ascii atom name inline (names like "+" "-")
// xxxx1111111 :  xxxxx > 0 FREE! FREE! FREE!
// xxx11111111 :  24 bits left!!! == hashsymbol!!!
// TODO: 15 type of "enums" possible each with values of 3*7=21 bits 0-2,097,152
// 21+3 = 24 !!! use for hash syms!!!!
//
// -- byte[8] lispheap[MAX_HEAP]
// 010 = lispheap, cons == 8 bytes, 2 cells - DONE
// 100 = lispheap, symbol == name + primptr, not same as value
// 110 = ??

//  000 HEAP (string, symbol, prim...) - DONE
//  001 INTP 1 - DONE
//  010 CONSP 1 - heap - DONE
//  011 SYMP 1 inline pointer symbol - DONE
//  100       ??? hash symbol ??? ??? array cons???
//  101 INTP 2 - DONE
//  110 PRIMP
//  111 SYMP 2 inline pointer symbol - DONE
//
// ROM storage, issue of serializing atom and be able to execute from ROM symbols cannot change name
// so need "unique" pointer, but since symbols can be determined dynamically we cannot change
// in ROM. Let's say we use 24 bits hash (1M /usr/share/dict/words cause 290 clashes),
// 32-24 = 8 - 3 bits id => 5 (length) of following string/symbol name. len=0 means it's "unique":
// 1. a symbol can be SYMP (ROM/ROM compatible)
// 2. a symbol can be HEAP ALLOC
// 3. a symbol can be hash in ROM string (len < 2^5 = 32)
// 4. a symbol can be hash in RAM
//
// 3 & 4 needs to verify when used???? Hmmmm, harmonize number 2 with 3 & 4

// symbol hashes
// -------------
// (hash, pointer to value, pointer to string)
// (0, 0, 0) end, or use linked list

// linear list: (value, hash, inline string<32), ..., (0000, nil)
// extended cons list: (cons (symbol, value), ...)
// extended env list: next, value, symbol ("cons3")
// extended env list: next, value, hashp/string/value (var sized, "cons3+")

#include "lisp.h"
#include "compat.h"

// forwards
void gc_conses();
int kbhit();
static inline lisp callfunc(lisp f, lisp args, lisp* envp, lisp e, int noeval);
void error(char* msg);
void run(char* s, lisp* envp);

lisp* global_envp = NULL;

// big value ok as it's used mostly no inside evaluation but outside at toplevel
#define READLINE_MAXLEN 1024

// set to 1 to get GC tracing messages
// adding real debugging - http://software-lab.de/doc/tut.html#dbg
static int traceGC = 0;
static int trace = 0;

// handle errors, break
jmp_buf lisp_break = {0};

// use for list(mkint(1), symbol("foo"), mkint(3), END);

// if non nil enables continous GC
// TODO: remove
static int dogc = 0;

typedef struct {
    char tag;
    char xx;
    short index;

    char* p; // TODO: make it inline, not second allocation
} string;

// conss name in order to be able to have a function named 'cons()'
// these are special, not stored in the allocated array
typedef struct {
    lisp car;
    lisp cdr;
} conss;

// lisp is a pointer with some semantics according to its lowest bits
// ==================================================================
// cons cells are 8 bytes, heap allocated objects are all 8 byte aligned too,
// thus, the three lowest bits aren't used so we use it for tagging/inline types.
//
// this is not ACCURATE!!! see later.... 
//
// 00000000 nil
//      000 heap allocated objects
//       01 int stored inline in the pointer
//      010 cons pointer into conses array
//       11 symbol names stored inside the pointer
//
//      100 special pointer, see below...
//     0100 UNUSED: ??? (IROM) symbol/string, n*16 bytes, zero terminated
//     1100 UNUSED: ??? (IROM) longcons (array consequtive nil terminated list) n*16 bytes (n*8 cars)
//
//     x1yy cons style things? but with other type
//     0110 UNUSED: maybe we have func/thunk/immediate
//     1110 UNUSED: 

// Espruino Javascript for esp8266 memory usage
// - http://www.esp8266.com/viewtopic.php?f=44&t=6574
// - uses 20KB, so 12 KB available for JS code + vars...

// stored using inline pointer
typedef struct {
    char tag;
    char xx;
    short index;

    int v;
} intint; // TODO: handle overflow...

// TODO: can we merge this and symbol, as all prim:s have name
typedef struct {
    char tag;
    signed char n; // TODO: maybe could use xx tag above?
    short index;

    void* f;
    char* name; // TODO: could point to symbol, however "foo" in program will always be there as string
} prim;

// Pseudo closure that is returned by if/progn and other construct that takes code, should handle tail recursion
typedef struct thunk {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    // This needs be same as immediate
} thunk;

typedef struct immediate {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    // This needs be same as thunk
} immediate;

typedef struct func {
    char tag;
    char xx;
    short index;

    lisp e;
    lisp env;
    lisp name; // TODO: recycle
} func;

int tag_count[MAX_TAGS] = {0};
int tag_bytes[MAX_TAGS] = {0};
int tag_freed_count[MAX_TAGS] = {0};
int tag_freed_bytes[MAX_TAGS] = {0};

char* tag_name[MAX_TAGS] = { "total", "string", "cons", "int", "prim", "symbol", "thunk", "immediate", "func", 0 };
int tag_size[MAX_TAGS] = { 0, sizeof(string), sizeof(conss), sizeof(intint), sizeof(prim), sizeof(thunk), sizeof(immediate), sizeof(func) };

int gettag(lisp x) {
    return TAG(x);
}

// essentially total number of cons+symbol+prim
// TODO: remove SYMBOL since they are never GC:ed! (thunk are special too, not tracked)
//#define MAX_ALLOCS 819200 // (fibo 22)
//#define MAX_ALLOCS 8192
//#define MAX_ALLOCS 1024 // keesp 15K free
//#define MAX_ALLOCS 512 // keeps 17K free
//#define MAX_ALLOCS 256 // keeps 21K free
//#define MAX_ALLOCS 128 // keeps 21K free
//#define MAX_ALLOCS 128 // make slower!!!
#define MAX_ALLOCS 256 // make faster???

int allocs_count = 0; // number of elements currently in used in allocs array
int allocs_next = 0; // top of allocations in allocs array
void* allocs[MAX_ALLOCS] = { 0 };
unsigned int used[MAX_ALLOCS/32 + 1] = { 0 };
    
#define SET_USED(i) ({int _i = (i); used[_i/32] |= 1 << _i%32;})
#define IS_USED(i) ({int _i = (i); (used[_i/32] >> _i%32) & 1;})

// any slot with no value/nil can be reused
int reuse_pos = 0;
int reuse() {
    int n = allocs_next;
    while(n--) {
        if (!allocs[reuse_pos]) return reuse_pos;
        reuse_pos++;
        if (reuse_pos >= allocs_next) reuse_pos = 0;
    }
    return -1;
}

// total number of things in use
int used_count = 0;
int used_bytes = 0;

// TODO: maybe not good idea, just pre allocate 12 and 16 bytes stuff 4 at a time and put on free list?
// permanent malloc, no way to give back
#define PERMALLOC_CHUNK 64

// used_count=72 cons_count=354 free=19280 USED=12 bytes
// used_count=72 cons_count=354 free=19580 USED=16 bytes
// saved: (- 19368 19580) = -122
// 84 mallocs = (* 84 4) 336 bytes "saved"?

// used_count=72 cons_count=354 free=19288 USED=12 bytes
// used_count=72 cons_count=354 free=19580 USED=16 bytes
// used_count=72 cons_count=354 free=18892 USED=12 bytes

// unmodified: used_count=72 cons_count=354 free=18900 USED=12 bytes startMem=34796 
// this:       used_count=72 cons_count=354 free=19540 USED=16 bytes startMem=34796
// (- 34796 19540) = 15256

void* perMalloc(int bytes) {
    if (bytes > PERMALLOC_CHUNK) return malloc(bytes);

    static char* heap = NULL;
    static int used = 0;
    if (used + bytes > PERMALLOC_CHUNK) {
//        printf("[perMalloc.more waste=%d]\n", PERMALLOC_CHUNK - used);
        // TODO: how to handle waste? if too much, then not worth it...
        heap = NULL;
        used = 0;
    }
    if (!heap) heap = malloc(PERMALLOC_CHUNK);
    void* r = heap;
//    printf("[perMalloc %d]", bytes); fflush(stdout);
    heap += bytes;
    used += bytes;
    return r;
}

// SLOT salloc/sfree, reuse mallocs of same size instead of free, saved 20% speed
#define SALLOC_MAX_SIZE 32
void* alloc_slot[SALLOC_MAX_SIZE] = {0}; // TODO: probably too many sizes...

void sfree(void** p, int bytes, int tag) {
    if (IS((lisp)p, symboll) || CONSP((lisp)p)) {
        error("sfree.ERROR: symbol or cons!\n");
    }
    if (bytes >= SALLOC_MAX_SIZE) {
        used_bytes -= bytes;
        return free(p);
    }
    /// TODO: use sfree?
    if (IS((lisp)p, string)) {
        free(((string*)p)->p);
    }

    // store for reuse
    void* n = alloc_slot[bytes];
    *p = n;
    alloc_slot[bytes] = p;
    // stats
    if (tag > 0) {
        tag_freed_count[tag]++;
        tag_freed_bytes[tag] += bytes;
    }
    tag_freed_count[0]++;
    tag_freed_bytes[0] += bytes;
}

static void* salloc(int bytes) {
    void** p = alloc_slot[bytes];
    if (bytes >= SALLOC_MAX_SIZE) {
        used_bytes += bytes;
        return malloc(bytes);
    } else if (!p) {
        used_bytes += bytes;
        return malloc(bytes);
        int i = 8;
        char* n = malloc(bytes * i);
        while (i--) {
            sfree((void*)n, bytes, -1);
            n += bytes;
        }
        p = alloc_slot[bytes];
    }
    alloc_slot[bytes] = *p;
    return p;
}

// call this malloc using ALLOC(typename) macro
// if tag < 0 no GC on these (don't keep pointer around)
void* myMalloc(int bytes, int tag) {
    ///printf("MALLOC: %d %d %s\n", bytes, tag, tag_name[tag]);

    if (1) { // 830ms -> 770ms 5% faster if removed, depends on the week!?
        if (tag > 0) {
            tag_count[tag]++;
            tag_bytes[tag] += bytes;
            used_count++;
        }

        tag_count[0]++;
        tag_bytes[0] += bytes;
    }

    // use for heap debugging, put in offending addresses
    //if (allocs_next == 269) { printf("\n==============ALLOC: %d bytes of tag %s ========================\n", bytes, tag_name[tag]); }
    //if ((int)p == 0x08050208) { printf("\n============================== ALLOC trouble pointer %d bytes of tag %d %s ===========\n", bytes, ag, tag_name[tag]); }

    void* p = salloc(bytes);

    // immediate optimization, only used transiently, so given back fast, no need gc.
    // symbols and prims are never freed, so no need keep track of or GC
    if (tag <= 0 || tag == immediate_TAG || tag == symboll_TAG || tag == prim_TAG) {
        ((lisp)p)->index = -1;
        return p;
    }

    int pos = reuse();
    if (pos < 0) pos = allocs_next++;

    allocs[pos] = p;
    allocs_count++;
    ((lisp)p)->index = pos;

    if (allocs_next >= MAX_ALLOCS) {
        report_allocs(2);
        error("Exhausted myMalloc array!\n");
    }
    return p;
}

static void mark_clean() {
    memset(used, 0, sizeof(used));
}

static int blockGC = 0;

PRIM gc(lisp* envp) {
    if (blockGC) {
        printf("\n%% [warning: GC called with blockGC=%d]\n", blockGC);
        return nil;
    }

    // mark
    syms_mark();

    //if (envp) { printf("ENVP %u=", (unsigned int)*envp); princ(*envp); terpri();}
    if (envp) mark(*envp);

    // sweep
    gc_conses();
    
    int count = 0;
    int i ;
    for(i = 0; i < allocs_next; i++) {
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
            allocs_count--;
            used_count--;
        }
    }
    mark_clean();

    return mem_usage(count);
}


////////////////////////////////////////////////////////////////////////////////
// string

// only have this in order to keep track of allocations
char* my_strndup(char* s, int len) {
    int l = strlen(s);
    if (l > len) l = len;
    char* r = myMalloc(len + 1, -1);
    strncpy(r, s, len);
    r[len] = 0;
    return r;
}

// make a string from POINTER (inside other string) by copying LEN bytes
// if len < 0 then it's already malloced elsewhere
PRIM mklenstring(char* s, int len) {
    if (!s) return nil;
    string* r = ALLOC(string);
    if (len >= 0) {
        r->p = my_strndup(s, len); // TODO: how to deallocate?
    } else {
        r->p = s;
    }
    return (lisp)r;
}

PRIM mkstring(char* s) {
    return mklenstring(s, strlen(s));
}

char* getstring(lisp s) {
    return IS(s, string) ? ATTR(string, s, p) : "";
}
    
// TODO:
// (string-ref s 0index)
// (string-set! s 0index char) -- maybe not modify
// string=? (substring=? s1 start s2 start end)
// cmp instead of string-compare
// string-hash, string-upcase/string-downcase
// (substring s start end)
// (string-search-forward pattern string)
// (substring-search-forward pattern string start end)
// (string-search-all pattern start end) => (pos1 pos2...) / nil

// srfi.schemers.org/srfi-13/srfi-13.html -- TOO BOORING!!!
//   predicate: string? string-null? string-every string-any
//   construct: make-string string string-tabulate
//   convertn: string->list list->string reverse-list->string string-join
//   select: string-length string-ref string-copy substring/shared string-copy!
//     string-take string-take-right string-drop- string-drop-right
//     string-pad string-pad-right string-trim string-trim-both
//   modify: string-set! string-fill!
//   compare: string-compare string<> string= string< string> string<= string>=
//     string-hash
//   prefix: string-prefix-length string-suffix-length string-prefix? string-suffix?
//   searching: string-index string-index-right string-skip string-skip-right string-count string-contains
//   reverse & append: string-reverse string-append string-concatenate
//   modify: string-replace string-delete

////////////////////////////////////////////////////////////////////////////////
// CONS

//#define MAX_CONS 137 // allows allocation of (list 1 2)
#define MAX_CONS 2048
//#define MAX_CONS 512
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

PRIM cons(lisp a, lisp b) {
    conss* c = GETCONS(free_cons);
    cons_count--;
    if (!c) {
        error("Run out of conses\n");
    }
    if (cons_count < 0) {
        error("Really ran out of conses\n");
    }
    if (c->car != _FREE_) {
        printf("Conses corruption error %u ... %u CONSP=%d\n", (int)c, (int)free_cons, CONSP(free_cons));
        printf("CONS="); princ((lisp)c); terpri();
        exit(1);
    }
    // TODO: this is updating counter in myMalloc stats, maybe refactor...
    if (0) { // TOOD: enable this and it becomes very slow!!!!??? why compared to myMalloc shouldn't????
    used_count++; // not correct as cons are different...
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
inline PRIM car(lisp x) { return CONSP(x) ? GETCONS(x)->car : nil; }
inline PRIM cdr(lisp x) { return CONSP(x) ? GETCONS(x)->cdr : nil; }

int cons_count; // forward

// however, on esp8266 it's only inlined and no function exists,
// so we need to create them for use in lisp
#ifdef UNIX
  #define car_ car
  #define cdr_ cdr
#else
  PRIM car_(lisp x) { return car(x); }
  PRIM cdr_(lisp x) { return cdr(x); }
#endif

PRIM setcar(lisp x, lisp v) { return IS(x, conss) ? GETCONS(x)->car = v : nil; }
PRIM setcdr(lisp x, lisp v) { return IS(x, conss) ? GETCONS(x)->cdr = v : nil; }

PRIM list(lisp first, ...) {
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

void report_allocs(int verbose) {
    int i;

    if (verbose) terpri();
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

    // print static sizes...
    if (verbose) {
        int tot = 0, b;
        printf("\nSTATICS ");
        b = sizeof(tag_name); printf("tag_name: %d ", b); tot += b;
        b = sizeof(tag_size); printf("tag_size: %d ", b); tot += b;
        b = sizeof(tag_count); printf("tag_count: %d ", b); tot += b;
        b = sizeof(tag_bytes); printf("tag_bytes: %d ", b); tot += b;
        b = sizeof(tag_freed_count); printf("tag_freed_count: %d ", b); tot += b;
        b = sizeof(tag_freed_bytes); printf("tag_freed_bytes: %d ", b); tot += b;
        b = sizeof(allocs); printf("allocs: %d ", b); tot += b;
        b = sizeof(alloc_slot); printf("alloc_slot: %d ", b); tot += b;
        b = CONSES_BYTES; printf("conses: %d ", b); tot += b;
        b = sizeof(cons_used); printf("cons_used: %d ", b); tot += b;
        printf(" === TOTAL: %d\n", tot);
    }

    // TODO: this one doesn't make sense?
    if (verbose) {
        printf("\nused_count=%d cons_count=%d ", used_count, cons_count);
        fflush(stdout);
    }
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

PRIM eq(lisp a, lisp b);

PRIM member(lisp e, lisp r) {
    while (r) {
        if (eq(e, car(r))) return r;
        r = cdr(r);
    }
    return nil;
}

PRIM out(lisp pin, lisp value) {
    gpio_enable(getint(pin), GPIO_OUTPUT);
    gpio_write(getint(pin), getint(value));
    return value;
}

PRIM in(lisp pin) {
    gpio_enable(getint(pin), GPIO_INPUT);
    return mkint(gpio_read(getint(pin)));
}

// CONTROL INTERRUPTS:
// -------------------
// (interrupt PIN 0)  : disable
// (interrupt PIN 1)  : EDGE_POS
// (interrupt PIN 2)  : EDGE_NEG
// (interrupt PIN 3)  : EDGE_ANY
// (interrupt PIN 4)  : LOW
// (interrupt PIN 5)  : HIGH
//
// TODO: this has 200 ms, "ignore" if happen within 200ms, maybe add as parameter?
//
// CALLBACK API:
// -------------
// If any interrupt is enabled it'll call intXX where XX=pin if the symbol exists.
// This is called from the IDLE loop, no clicks counts will be lost, clicks is the
// new clicks since last invokation/clear. It will only be invoked once if any was
// missed, and only the last time in ms is retained.
//
//   (define (int00 pin clicks count ms)
//     (printf " [button %d new clicks=%d total=%d last at %d ms] " pin clicks count ms))
// 
// POLLING API:
// ------------
// (interrupt PIN)    : get count
// (interrupt PIN -1) : get +count if new, or -count if no new
// (interrupt PIN -2) : get +count if new, or 0 otherwise
// (interrupt PIN -3) : get ms of last click
PRIM interrupt(lisp pin, lisp changeType) {
    if (!pin && !changeType) return nil;
    int ct = getint(changeType);
    if (changeType && ct >= 0) {
        interrupt_init(getint(pin), ct);
        return pin;
    } else {
        return mkint(getInterruptCount(getint(pin), changeType ? ct : 0));
    }
}

void vTaskDelay( uint32_t xTicksToDelay );

PRIM delay(lisp ticks) {
	int delayTime = getint(ticks);

	vTaskDelay(delayTime);

	return ticks;
}

int random_basic(int low, int high) {
	int r = rand() % (high - low + 1) + low;

	return r;
}

PRIM random(lisp low, lisp high) {
	int r = random_basic(getint(low), getint(high));

	return mkint(r);
}

void spi_led(int, int, int, int, int);

PRIM led_show(lisp init, lisp digit, lisp val, lisp decode, lisp delay) {
	spi_led(getint(init), getint(digit), getint(val), getint(decode), getint(delay));

	return mkint(1);
}

int spiData[] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 };

unsigned char decodeMode = 1;

PRIM led_data(lisp data, lisp offset) {

	int pos = getint(offset);

	int i = 0;

	int val = 0;

	while (data && ((i + pos) < 15)) {
		val = getint(car(data));

		// show nums and hex
		if (decodeMode == 0) {
			if (val > 9 && val < 16) {
				// offset by space ascii and start of lowercase alphabet
				val = val + 32 + 65 - 10;
			}
			else
				if (val >= 0  && val < 10) {
					val = val + 32 + 65 - 49;
				}
		}

		spiData[pos + i] = val;

		i = i + 1;

        data = cdr(data);
    }

	return nil;
}

// wget functions...
// echo '
// (wget "yesco.org" "http://yesco.org/index.html" (lambda (t a v) (princ t) (cond (a (princ " ") (princ a) (princ "=") (princ v)(terpri)))))
// ' | ./run

static void f_emit_text(lisp callback, char* path[], char c) {
    maybeGC();

    apply(callback, list(mkint(c), END)); // more effcient with "integer"/char
}

static void f_emit_tag(lisp callback, char* path[], char* tag) {
    maybeGC();
    
    apply(callback, list(symbol(tag), END));
}

static void f_emit_attr(lisp callback, char* path[], char* tag, char* attr, char* value) {
    maybeGC();
    
    apply(callback, list(symbol(tag), symbol(attr), mkstring(value), END));
}

// TODO: http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-ref/XML-Input.html#XML-Input
// https://www.gnu.org/software/guile/manual/html_node/Reading-and-Writing-XML.html
// https://www.gnu.org/software/guile/manual/html_node/sxml_002dmatch.html
PRIM wget_(lisp server, lisp url, lisp callback) {
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
    buff = buff ? buff : "";
    maybeGC();

    apply(web_callback, list(symbol("header"), mkstring(buff), symbol(method), mkstring(path), END));
}

static void body(char* buff, char* method, char* path) {
    buff = buff ? buff : "";
    maybeGC();

    apply(web_callback, list(symbol("body"), mkstring(buff), symbol(method), mkstring(path), END));
}

static void response(int req, char* method, char* path) {
    maybeGC();

    lisp ret = apply(web_callback, list(nil, mkint(req), symbol(method), mkstring(path), END));
    printf("RET="); princ(ret); terpri();

    // TODO: instead redirect output to write!!! 
    char* s = getstring(ret);

    do {
        int r = write(req, s, strlen(s));
        if (r < 0) { printf("%%Error on writing response, errno=%d\n", errno); break; }
        s += r;
    } while (*s);

    maybeGC();
}

// echo '
// (web 8080 (lambda (r w s m p) (princ w) (princ " ") (princ s) (princ " ") (princ m) (princ " ") (princ p) (terpri) "FISH-42"))
// ' | ./run

int web_socket = 0;

int web_one() {
    int r = -1;
    if (!web_socket) return 0;
    if (setjmp(lisp_break) == 0) {
        r = httpd_next(web_socket, header, body, response);
    } else {
        printf("\n%%web_one.error... recovering...\n");
    }
    // disable longjmp
    memset(lisp_break, 0, sizeof(lisp_break));
    return r;
}

PRIM web(lisp* envp, lisp port, lisp callback) {
    //wget_data data;
    //memset(&data, 0, sizeof(data));
    //data.userdata = callback;
    //data.xml_emit_text = (void*)f_emit_text;
    //data.xml_emit_tag = (void*)f_emit_tag;
    //data.xml_emit_attr = (void*)f_emit_attr;

    // store a pointer in global env to the function so it doesn't get gc:ed
    web_callback = evalGC(callback, envp);
    _define(envp, list(symbol("webcb"), reads("web_callback"), END));

    int s = httpd_init(getint(port));
    if (s < 0) { printf("ERROR.errno=%d\n", errno); return nil; }

    web_socket = s;

    web_one();
    return mkint(s);
}


// lookup binding of symbol variable name (not work for int names)
PRIM assoc(lisp name, lisp env) {
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

PRIM evallist(lisp e, lisp* envp) {
    if (!e) return e;
    // TODO: don't recurse!
    return cons(eval(car(e), envp), evallist(cdr(e), envp));
}

// dummy function that doesn't eval, used instead of eval
static PRIM noEval(lisp x, lisp* envp) { return x; }

PRIM primapply(lisp ff, lisp args, lisp* envp, lisp all, int noeval) {
    //printf("PRIMAPPLY "); princ(ff); princ(args); terpri();
    int n = GETPRIMNUM(ff);
    lisp (*e)(lisp x, lisp* envp) = (noeval && n > 0) ? noEval : evalGC;
    int an = abs(n);

    // these special cases are redundant, can be done at general solution
    // but for optimization we extracted them, it improves speed quite a lot
    if (n == 2) { // eq/plus etc
        lisp (*fp)(lisp,lisp) = GETPRIMFUNC(ff);
        return (*fp)(e(car(args), envp), e(car(cdr(args)), envp)); // safe!
    }
    if (n == -3) { // if...
        lisp (*fp)(lisp*,lisp,lisp,lisp) = GETPRIMFUNC(ff);
        return (*fp)(envp, car(args), car(cdr(args)), car(cdr(cdr(args))));
    }
    if (n == -1) { // quote...
        lisp (*fp)(lisp*,lisp) = GETPRIMFUNC(ff);
        return (*fp)(envp, car(args));
    }
    if (n == 1) {
        lisp (*fp)(lisp) = GETPRIMFUNC(ff);
        return (*fp)(e(car(args), envp));
    }
    if (n == 3) {
        lisp (*fp)(lisp,lisp,lisp) = GETPRIMFUNC(ff);
        return (*fp)(e(car(args), envp), e(car(cdr(args)), envp), e(car(cdr(cdr(args))),envp));
    }
    if (n == -7) { // lambda, quite uncommon
        lisp (*fp)(lisp*,lisp,lisp) = GETPRIMFUNC(ff);
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
        lisp (*fp)() = GETPRIMFUNC(ff);
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
    } else if (n > -7) { // -1 .. -7 no-eval lambda, put env first
        // TODO: for NLAMBDA this may not work...  may need a new lisp type
        args = cons(*envp, args);
    }

    lisp r;
    if (abs(n) == 7) {
        lisp (*fp)(lisp*, lisp, lisp) = GETPRIMFUNC(ff);
        r = fp(envp, args, all);
    } else {
        lisp a = args, b = cdr(a), c = cdr(b), d = cdr(c), e = cdr(d), f = cdr(e), g = cdr(f), h = cdr(g), i = cdr(h), j = cdr(i);
        // with C calling convention it's ok, but maybe not most efficient...
        lisp (*fp)(lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp, lisp) = GETPRIMFUNC(ff);
        r = fp(car(a), car(b), car(c), car(d), car(e), car(f), car(g), car(h), car(i), car(j));
    }

    return r;
}

// TODO: not used??? this can be used to implement generators
static inline lisp mkthunk(lisp e, lisp env) {
    thunk* r = ALLOC(thunk);
    r->e = e;
    r->env = env;
    return (lisp)r;
}

// an immediate is a continuation returned that will be called by eval directly to yield a value
// this implements continuation based evaluation thus maybe allowing tail recursion...
// these are used to avoid stack growth on self/mutal recursion functions
// if, lambda, progn etc return these instead of calling eval on the tail
static inline lisp mkimmediate(lisp e, lisp env) {
    immediate* r = ALLOC(immediate); //(thunk*)mkthunk(e, env); // inherit from func_TAG
    r->e = e;
    r->env = env;
    return (lisp)r;
}


// these are formed by evaluating a lambda
PRIM mkfunc(lisp e, lisp env) {
    func* r = ALLOC(func);
    r->e = e;
    r->env = env;
    r->name = nil;
    return (lisp)r;
}

PRIM fundef(lisp f) {
    if (IS(f, func)) return ((func*)f)->e;
    return nil;
}

PRIM funenv(lisp f) {
    if (IS(f, func)) return ((func*)f)->env;
    return nil;
}

PRIM funame(lisp f) { // fun-name lol (6 char) => funame
    if (IS(f, func)) return ((func*)f)->name;
    if (IS(f, prim)) return *(lisp*)GETPRIM(f);
    return nil;
}

////////////////////////////// GC

#define FLASHP(x) ((unsigned int)next >= (unsigned int)flash_memory && ((unsigned int)next <= (unsigned int)(flash_memory + SPI_FLASH_SIZE_BYTES - FS_ADDRESS)))

void mark_deep(lisp next, int deep) {
    while (next) {
        // -- pointer to FLASH? no follow...
        if (FLASHP(next)) {
            printf("[mark.flash %x]\n", (unsigned int)next);
            return;
        }
        // -- pointer contains tag
        if (INTP(next)) return;
        if (SYMP(next)) return;
        if (PRIMP(next)) return;
	if (CONSP(next)) {
	    int i = (conss*)next - &conses[0];
            if (i >= MAX_CONS) { // pointing to other RAM/FLASH not allocated to cons array
                printf("mark.cons.badcons i=%d    %u max=%d\n", i, (int)next, MAX_CONS);
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
            
        int tag = TAG(p);
        if (tag == thunk_TAG || tag == immediate_TAG || tag == func_TAG) {
            mark_deep(ATTR(thunk, p, e), deep+1);
            next = ATTR(thunk, p, env);
        }
    }
}

inline void mark(lisp x) {
    mark_deep(x, 1);
}

///--------------------------------------------------------------------------------
// Primitives
// naming convention:
// -- lisp foo()   ==   arguments like normal lisp function can be called from c easy
// -- lsp foo_()   ==   same as lisp but name clashes with stanrdard function read_
// -- lsp _foo()   ==   special function, order may be different (like it take env& as first)

// first one liners
PRIM nullp(lisp a) { return a ? nil : t; }
PRIM consp(lisp a) { return IS(a, conss) ? t : nil; }
PRIM atomp(lisp a) { return IS(a, conss) ? nil : t; }
PRIM stringp(lisp a) { return IS(a, string) ? t : nil; }
PRIM symbolp(lisp a) { return IS(a, symboll) ? t : nil; } // rename struct symbol to symbol?
PRIM numberp(lisp a) { return IS(a, intint) ? t : nil; } // TODO: extend with float/real
PRIM integerp(lisp a) { return IS(a, intint) ? t : nil; }
PRIM funcp(lisp a) { return IS(a, func) || IS(a, thunk) || IS(a, prim) ? t : nil; }

PRIM lessthan(lisp a, lisp b) { return getint(a) < getint(b) ?  t : nil; }

PRIM plus(lisp a, lisp b) { return mkint(getint(a) + getint(b)); }
PRIM minus(lisp a, lisp b) { return b ? mkint(getint(a) - getint(b)) : mkint(-getint(a)); }
PRIM times(lisp a, lisp b) { return mkint(getint(a) * getint(b)); }
PRIM divide(lisp a, lisp b) { return mkint(getint(a) / getint(b)); }
PRIM mod(lisp a, lisp b) { return mkint(getint(a) % getint(b)); }

// TODO: http://www.gnu.org/software/emacs/manual/html_node/elisp/Input-Functions.html#Input-Functions
// http://www.lispworks.com/documentation/HyperSpec/Body/f_rd_rd.htm#read
PRIM read_(lisp s) {
    if (s) {
        return reads(getstring(s));
    } else {
        char* str = readline(">", READLINE_MAXLEN);
        lisp r = str ? reads(str) : nil;
        if (str) free(str);
        return r;
    }
}

// TODO: consider http://picolisp.com/wiki/?ArticleQuote
PRIM _quote(lisp* envp, lisp x) { return x; }
PRIM quote(lisp x) { return list(symbol("quote"), x, END); } // TODO: optimize
PRIM _env(lisp* e, lisp all) { return *e; }

// longer functions
PRIM eq(lisp a, lisp b) {
    if (a == b) return t;
    char ta = TAG(a);
    char tb = TAG(b);
    if (ta != tb) return nil;
    // only int needs to be eq with other int even if on heap...
    if (ta != intint_TAG) return nil;
    if (getint(a) == getint(b)) return t;
    return nil;
}

PRIM equal(lisp a, lisp b) {
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

inline lisp getBind(lisp* envp, lisp name, int create) {
    //printf("GETBIND: envp=%u global_envp=%u ", (unsigned int)envp, (unsigned int)global_envp); princ(name); terpri();
    if (create && envp == global_envp) return hashsym(name, NULL, 0, create);
    if (create) return nil;

    lisp bind = assoc(name, *envp);
    if (bind) return bind;

    // check "global"
    return hashsym(name, NULL, 0, 0); // not create, read only
}
lisp getBind(lisp* envp, lisp name, int create);

// like setqq but returns binding, used by setXX
// 1. define, de - create binding in current environment
// 2. set! only modify existing binding otherwise give error
// 3. setq ??? (allow to define?)
inline lisp _setqqbind(lisp* envp, lisp name, lisp v, int create) {
    lisp bind = getBind(envp, name, create);
    if (!bind) {
        bind = cons(name, nil);
        *envp = cons(bind, *envp);
    }
    setcdr(bind, v);
    return bind;
}
// magic, this "instantiates" an inline function!
lisp _setqqbind(lisp* envp, lisp name, lisp v, int create);
    
inline PRIM _setqq(lisp* envp, lisp name, lisp v) {
    _setqqbind(envp, name, nil, 0);
    return v;
}
// magic, this "instantiates" an inline function!
PRIM _setqq(lisp* envp, lisp name, lisp v); 

// next line only needed because C99 can't get pointer to inlined function?
PRIM _setqq_(lisp* envp, lisp name, lisp v) { return _setqq(envp, name, v); }

inline PRIM _setbang(lisp* envp, lisp name, lisp v) {
    if (!symbolp(name)) { printf("set! of non symbol="); prin1(name); terpri(); error("set! of non atom: "); }
    lisp bind = _setqqbind(envp, name, nil, 0);
    // TODO: evalGC? probably safe as steqqbind changed an existing env
    // eval using our own named binding to enable recursion
    v = eval(v, envp);
    setcdr(bind, v);

    return v;
}

inline PRIM _set(lisp* envp, lisp name, lisp v) {
    // TODO: evalGC? probably safe as steqqbind changed an existing env
    return _setbang(envp, eval(name, envp), v);
}
// magic, this "instantiates" an inline function!
PRIM _set(lisp* envp, lisp name, lisp v);

PRIM de(lisp* envp, lisp namebody);

PRIM _define(lisp* envp, lisp args) {
    if (SYMP(car(args))) { // (define a 3)
        lisp name = car(args);

        // like _setq but with create == 1
        lisp bind = _setqqbind(envp, name, nil, 1);
        lisp r = eval(car(cdr(args)), envp);
        setcdr(bind, r);

        if (IS(r, func)) ((func*)r)->name = name;
        return r;
    } else { // (define (a x) 1 2 3)
        lisp name = car(car(args));
        lisp fargs = cdr(car(args));
        return de(envp, cons(name, cons(fargs, cdr(args))));
    }
}

PRIM de(lisp* envp, lisp namebody) {
    return _define(envp, cons(car(namebody), cons(cons(symbol("lambda"), cdr(namebody)), nil)));
}

lisp reduce_immediate(lisp x);

// not apply does not evaluate it's arguments
PRIM apply(lisp f, lisp args) {
    // TODO: for now, block GC as args could have been built out of thin air!
    blockGC++; 

    lisp e = nil; // dummy
    // TODO: like eval push on stack so can GC safely?
    lisp x = callfunc(f, args, &e, nil, 1);
    x = reduce_immediate(x);

    blockGC--;
    return x;
}

PRIM mapcar(lisp f, lisp r) {
    if (!r || !consp(r) || !funcp(f)) return nil;
    lisp v = apply(f, cons(car(r), nil));
    return cons(v, mapcar(f, cdr(r)));
}

PRIM map(lisp f, lisp r) {
    while (r && consp(r) && funcp(f)) {
        apply(f, cons(car(r), nil));
        r = cdr(r);
    }
    return nil;
}

PRIM length(lisp r) {
    if (IS(r, string)) return mkint(strlen(getstring(r)));
    if (!IS(r, conss)) return mkint(0);
    int c = 0;
    while (r) {
        c++;
        r = cdr(r);
    }
    return mkint(c);
}

// scheme string functions - https://www.gnu.org/software/guile/manual/html_node/Strings.html#Strings
// common lisp string functions - http://www.lispworks.com/documentation/HyperSpec/Body/f_stgeq_.htm
PRIM concat(lisp* envp, lisp x) {
    int len = 1;
    lisp i = x;
    while (i) {
        len += strlen(getstring(car(i)));
        i = cdr(i);
    }
    char* s = malloc(len), *p = s;;
    *s = 0;
    i = x;
    while (i) {
        p = strcat(p, getstring(car(i)));
        i = cdr(i);
    }
    lisp r = mklenstring(s, -len);
    return r;
}

PRIM char_(lisp i) {
    if (IS(i, string)) {
        return mkint(getstring(i)[0]);
    } else if (IS(i, intint)) {
        char s[2] = {0};
        s[0] = getint(i);
        return mkstring(s);
    } else 
        error("char.unsupported type");
    return nil;
}

// optional n, number of entries to return (1..x), 0, <0, nil, non-number => all
PRIM split(lisp s, lisp d, lisp n) {
    int i = getint(n);
    char* src = getstring(s);
    char* delim = getstring(d);
    int len = strlen(delim);
    lisp r = nil;
    lisp p = r;
    lisp last = nil;
    while (src && (i > 0 || i <= 0)) {
        char* where = strstr(src, delim);
        if (!where) {
            where = src + strlen(src);
            i = 1; // will terminate after add
        }
        lisp m = mklenstring(src, where - src);

        // add conscell at end and add value
        p = cons(nil, nil);
        if (!r)
            r = p;
        else
            setcdr(last, p);
        setcar(p, m);

        last = p;
        p = cdr(p);

        src = where + len;
        i--;
        if (!i) break;
    }
    return r;
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
    while (c && isspace((int)c)) c = next();
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
    while (c && c != '"') {
        if (c == '\\') c = next();
        c = next();
    }
    if (!c) error("string.not_terminated");
    int len = input - start - 1;

   // we modify the copied string as the string above may be constant
    lisp r = mklenstring(start, len);
    // remove '\', this may waste a byte or two if any
    char* from = getstring(r);
    char* to = from;
    while (*from) {
        // TODO: \n \t ...
        if (*from == '\\') from++;
        *to = *from; // !
        from++; to++;
    }
    *from = 0;
    return r;
}

static lisp readSymbol(char c, int o) {
    // TODO: cleanup, ugly
   if (!input) {
       char s[2] = {'-', 0};
       return symbol_len(s, 1);
    }
    char* start = input - 1 + o;
    int len = 0;
    while (c && c!='(' && c!=')' && !isspace((int)c) && c!='.') {
        len++;
	c = next();
    }
    nextChar = c;
    return symbol_len(start, len-o);
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
    c = next();
    lisp d = nil;
    if (c == '.') {
        d = readx();
        c = next();
        if (c != ')') error("Dotted pair expected ')'!");
        c = next();
    } else {
        nextChar = c;
        d = readList();
        c = next();
    }
    nextChar = c;
    return cons(a, d);
}

static lisp readx() {
    skipSpace();
    unsigned char c = next();
    if (!c) return NULL;
    if (c == '\'') return quote(readx());
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

PRIM reads(char *s) {
    input = s;
    nextChar = 0;
    return readx();
}

///////////////////////////////////////////////////////////////////////////////
// output, with capturing
typedef int putcf(int c);
putcf *writeputc, *origputc;

// TODO: more efficient...
//typedef int writef(char* s, int len);
//putcf *writewrite;


PRIM with_putc(lisp* envp, lisp args) {
    lisp fn = car(args);
    putcf *old = writeputc;

    int recurse = 0;
    int myputc(int c) {
        recurse++;
        if (recurse > 1) error("with-putc called with function that calls putc - prohibited!");
        lisp r= callfunc(fn, cons(mkint(c), nil), envp, nil, 1);
        recurse--;
        return getint(r);
    }

    lisp r = nil;
    writeputc = myputc; {
        // need catch errors and restore... (setjmp/error?)
        r = progn(envp, cdr(args));

    } writeputc = old;
    return r;
}

PRIM with_fd(lisp* envp, lisp args) {
    int fd = getint(eval(car(args), envp));
    putcf *old = writeputc;

    int myputc(int c) {
        char cc = c;
        return write(fd, &cc, 1);
    }

    lisp r = nil;
    writeputc = myputc; {
        // need catch errors and restore... (setjmp/error?)
        r = reduce_immediate(progn(envp, cdr(args)));
    } writeputc = old;
    return r;
}

// TODO: move out to an ide.c file? consider with ide-www

PRIM with_fd_json(lisp* envp, lisp args) {
    int fd = getint(eval(car(args), envp));
    putcf *old = writeputc;

    int myputc(int c) {
        char cc = c;
        origputc(c);
        // TODO: specific for jsonp?
        if (c == '\n' || c == '\r')
            return write(fd, "\\n", 2);
        else if (c == '"')
            return write(fd, "\\\"", 2);
        else if (c == '\'')
            return write(fd, "\\\'", 2);
        else
            return write(fd, &cc, 1);
    }

    lisp r = nil;
    writeputc = myputc; {
        // need catch errors and restore... (setjmp/error?)
        r = reduce_immediate(progn(envp, cdr(args)));
    } writeputc = old;
    return r;
}

int writec(int c) {
    if (!writeputc) origputc = writeputc = &putchar;
    return writeputc(c);
} 

int writes(char* s) {
    int n = 0;
    while (*s && ++n) writec(*s++);
    return n;
} 

int writef(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len;

    if (writeputc == &putchar) { // more efficient
        len = vprintf(fmt, args);
    } else {
        // capture output of printf by first faking it, just counting
        // then, write to buffer, then call writec...
        char* tst = NULL;
        len = vsnprintf(tst, 0, fmt, args) + 1;
        char out[len]; // TODO: this may not be safe...
        memset(out, 0, len);
        vsnprintf(out, len, fmt, args);
        writes(out);
    }

    va_end(args);
    return len;
}

#undef putchar
#undef printf
#undef puts

#define printf writef
#define putchar writec
#define puts writes

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
lisp princ_hlp(lisp x, int readable) {
//    printf(" [:: %u ::] ");
    int tag = TAG(x);
    // simple one liners
    if (!tag) printf("nil");
    else if (tag == intint_TAG) printf("%d", getint(x));
    else if (tag == prim_TAG) { putchar('#'); princ_hlp(*(lisp*)GETPRIM(x), readable); }
    // for now we have two "symbolls" one inline in pointer and another heap allocated
    else if (HSYMP(x)) writes(symbol_getString(x));
    else if (SYMP(x)) { char s[7] = {0}; sym2str(x, s); writes(s); }
    // can't be made reable really unless "reveal" internal pointer
    else if (tag == thunk_TAG) { printf("#thunk["); princ_hlp(ATTR(thunk, x, e), readable); putchar(']'); }
    else if (tag == immediate_TAG) { printf("#immediate["); princ_hlp(ATTR(thunk, x, e), readable); putchar(']'); }
    else if (tag == func_TAG) { putchar('#'); princ_hlp(ATTR(func, x, name), readable); }
    // string
    else if (tag == string_TAG) {
        if (readable) putchar('"');
        char* c = ATTR(string, x, p);
        while (*c) {
            if (readable && *c == '\"') putchar('\\');
            putchar(*c); //printf("[%d]", *c);
            c++;
        }
        if (readable) putchar('"');
    }
    // cons
    else if (tag == conss_TAG) {
        putchar('(');
        princ_hlp(car(x), readable);
        lisp d = cdr(x);
        while (d && gettag(d) == conss_TAG) {
            putchar(' ');
            princ_hlp(car(d), readable);
            d = cdr(d);
        }
        if (d) {
            putchar(' '); putchar('.'); putchar(' ');
            princ_hlp(d, readable);
        }
        putchar(')');
    } else {
        printf("*UnknownTag:%d;%u;%x*", tag, (unsigned int)x, (unsigned int)x);
    }
    // is need on esp, otherwise it's buffered and comes some at a time...
    // TODO: check performance implications
    fflush(stdout); 
    return x;
}

PRIM princ(lisp x) {
    return princ_hlp(x, 0);
}

// print in readable format
PRIM prin1(lisp x) {
    return princ_hlp(x, 1);
}

PRIM print(lisp x) {
    terpri();
    lisp r = prin1(x);
    putchar(' ');
    return r;
}

PRIM terpri() { putchar('\n'); return nil; }

// format conforms to - http://www.gnu.org/software/emacs/manual/html_node/elisp/Formatting-Strings.html
// TODO: make it return the numbers of characters printed?
// TODO: make a sprintf, or call it "format".
// which essentially is printf for lisp, they call it format in elisp
// TODO: format - http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-ref/Format.html#Format
PRIM printf_(lisp *envp, lisp all) {
    char* f = getstring(car(all));
    all = cdr(all);
    while (*f) {
        if (*f == '%') {
            char fmt[16] = {0};
            fmt[14] = 1; fmt[15] = 1;
            char* p = &(fmt[0]);
            while (*f && !isalpha((int)*f) && !*p)
                *p++ = *f++;
            char type = *p++ = *f++;
            switch (type) {
            case '%':
                putchar('%'); break;
            case 's':
                princ(car(all)); break;
            case 'S':
                prin1(car(all)); break;
            case 'o': case 'd': case 'x': case 'X': case 'c':
                printf(fmt, getint(car(all))); break;
            case 'e': case 'f': case 'g': break;
                // printf(fmt, getfloat(car(x))); break;
            }
            if (type != '%')
                all = cdr(all);
        } else {
            putchar(*f++);
        }
    }
    return nil;
}

lisp pp_hlp(lisp e, int indent) {
    void nl() { terpri(); int i = indent*3; while(i--) putchar(' '); };
    void print_list(lisp e) {
        indent++;
        while (e) {
            nl();
            pp_hlp(car(e), indent+1);
            e = cdr(e);
        }
        putchar(')');
        indent--; nl();
    }

    if (!e) return prin1(e);
    if (!consp(e)) return prin1(e);
    lisp a1 = car(e), l2 = cdr(e),
        a2 = car(l2), l3 = cdr(l2),
        a3 = car(l3), l4 = cdr(l3),
        a4 = car(l4);
    if (funame(a1)) a1 = funame(a1); // decompile! haha
    if (symbolp(a1)) {
        if (a1 == symbol("if")) {
            printf("(if "); indent++; pp_hlp(a2, indent+1); nl();
            pp_hlp(a3, indent+2); indent--;
            if (l4) { nl(); pp_hlp(a4, indent+1); }
            putchar(')');
        //} else if (a1 == symbol("lambda")) {
        } else if (a1 == symbol("cond")) {
            printf("(cond ");
            print_list(l3);
        } else if (a1 == symbol("case")) {
            printf("(case "); pp_hlp(a2, indent+1);
            print_list(l3);
        } else if (a1 == symbol("define")) {
            prin1(a2);
            print_list(l3);
        } else if (a1 == symbol("de")) {
            printf("(de "); prin1(a2); putchar(' '); prin1(a3);
            print_list(l4);
        } else {
            printf("(");
            pp_hlp(a1, indent+1);
            lisp r = l2;
            while (r) {
                putchar(' ');
                pp_hlp(car(r), indent+1);
                r = cdr(r);
            }
            printf(")");
        }
    } else if (consp(a1)) { // list of list
        putchar('('); pp_hlp(a1, indent+2);
        print_list(l2);
    } else { // a list of some kind
        // TODO: try figure out complexity of list and if to do print_list on it...
        // use length and "estimated" chars for output (recursive length?)
        prin1(e); nl();
    }
    return nil;
}

PRIM pp(lisp e) {
    if (funcp(e))
        e = cons(symbol("de"), cons(funame(e), fundef(e)));
    pp_hlp(e, 0);
    return symbol("");
}

static void indent(int n) {
    n *= 2;
    while (n-- > 0) putchar(' ');
}

static lisp funcapply(lisp f, lisp args, lisp* envp, int noeval);

// get value of var, or complain if not defined
static inline lisp getvar(lisp e, lisp env) {
    lisp v = getBind(&env, e, 0);
    if (v) return cdr(v);

    printf("\n-- ERROR: Undefined symbol: "); princ(e); terpri();
    error("%% Undefined symbol");
    return nil;
}

// don't call directly, call evalGC() or eval()
static inline lisp eval_hlp(lisp e, lisp* envp) {
    if (!e) return e;
    char tag = TAG(e);
    if (tag == symboll_TAG) return getvar(e, *envp);
    if (tag != conss_TAG) return e;

    // find function
    lisp orig = car(e);
    lisp f = orig;
    tag = TAG(f);
    lisp last = nil; // if eval to exactly same value return, can still loop
    while (f && f!=last && tag!=prim_TAG && tag!=thunk_TAG && tag!=func_TAG) {
        last = f;
        f = evalGC(f, envp);
        tag = TAG(f);
    }

    // This may return a immediate, this allows tail recursion evalGC will reduce it.
    lisp r = callfunc(f, cdr(e), envp, e, 0);

    // we replace it after as no error was generated...
    if (f != orig) {
        // "macro expansion" lol (replace with implementation)
        // TODO: not safe if found through variable (like all!)
        // TODO: keep on symbol ptr to primitive function/global, also not good?
        // DEFINE(F,...) will then break many local passed variables
        // maybe must search all list till find null, then can look on symbol :-(
        // but that's everytime? actually, not it's a lexical scope!
        // TODO: only replace if not found in ENV and is on an SYMBOL!
        if (symbolp(orig) && eq(funame(f), orig))
            setcar(e, f);
    }

    return r;
}

// inline this is essential to not have stack grow!
inline lisp reduce_immediate(lisp x) {
    while (x && IS(x, immediate)) {
        lisp tofree = x;

        if (trace) // make it visible
            x = evalGC(ATTR(thunk, x, e), &ATTR(thunk, x, env));
        else
            x = eval_hlp(ATTR(thunk, x, e), &ATTR(thunk, x, env));

        // immediates are immediately consumed after evaluation, so they can be free:d directly
        tofree->tag = 0;
        sfree((void*)tofree, sizeof(thunk), immediate_TAG);
        used_count--;
    }
    return x;
}

static void mymark(lisp x) {
    if (dogc) mark(x);
}

static void mygc() {
    if (dogc) gc(NULL);
}

// this is a safe eval to call from anywhere, it will not GC
// but it may blow up the stack, or the heap!!!
lisp eval(lisp e, lisp* envp) {
    blockGC++;
    lisp r = evalGC(e, envp);
    blockGC--;
    return r;
}

PRIM _eval(lisp e, lisp env) {
    // taking pointer creates a new "scrope"
    // if no env given, use the global env (so define will be on top level)
    return evalGC(e, env ? &env : global_envp);
}

lisp mem_usage(int count) {
    // TODO: last number conses not correct new useage
    if (traceGC) printf(" [GC freed %d used=%d bytes=%d conses=%d]\n", count, used_count, used_bytes, MAX_CONS - cons_count);
    return nil;
}

inline int needGC() {
    if (cons_count < MAX_CONS * 0.2) return 1;
    return (allocs_count < MAX_ALLOCS * 0.8) ? 0 : 1;
}
// magic, this "instantiates" an inline function!
int needGC();


// (de rec (n) (print n) (rec (+ n 1)) nil) // not tail recursive!
// === optimized:
// ...
// #767 0x0804d8b3 in evalGC ()
//
// #768 0x08051818 in progn ()
// #770 0x0804d8b3 in evalGC ()
//
// #771 0x08051e67 in run ()
// #772 0x08052d58 in readeval ()
// #773 0x08048b57 in main ()

#define MAX_STACK 80

static struct stack {
    lisp e;
    lisp* envp;
} stack[MAX_STACK];

static int level = 0;

// TODO: because of tail call optimization, we can't tell where the error occurred as it's not relevant on the stack???
PRIM print_detailed_stack() {
    int l;
    // TODO: DONE but too much: using fargs of f can use .envp to print actual arguments!
    for(l = 0; l < level + 5; l++) {
        if (!stack[l].e && !stack[l].envp) break;

        if (!l) terpri();
        printf("%4d : ", l);
        prin1(stack[l].e); printf(" ==> ");

        lisp f = car(stack[l].e);
        lisp* envp = stack[l].envp;
        lisp env = envp ? *envp : nil; // env before
        while (f && IS(f, symboll) && !IS(f, func) && !IS(f, thunk) && !IS(f, prim) && !IS(f, immediate)) {
            f = getvar(f, env); // eval?
        }

        putchar('['); princ(f);
        if (f && IS(f, func)) {
            // get env AFTER it has been extended... (using next expression)
            lisp *nenvp = (l+1 < MAX_STACK) ? stack[l+1].envp : NULL;
            lisp nenv = nenvp ? *nenvp : nil;

            if (nenv) {
                lisp def = ATTR(thunk, f, e); // get definition
                lisp fargs = car(def);
                //printf("\nFARGS="); princ(fargs); printf("  ENV="); princ(nenv); terpri();
                while (fargs && nenv) {
                    putchar(' ');
                    prin1(car(car(nenv))); putchar('='); prin1(cdr(car(nenv)));
                    env = cdr(nenv);
                    fargs = cdr(fargs);
                }
            }
            putchar(']');
        } else if (f && IS(f, prim)) {
            printf(" ... ] ");
        } else {
            printf(" ... ] ");
            printf(" ...car did not evaluate to a function... (it's an %s)\n", f ? tag_name[TAG(f)] : "nil");
            break;
        }
        terpri();
    }
    return nil;
}

void print_stack() {
    int l;
    lisp last = nil;
    int count = 0;
    // TODO: DONE but too much: using fargs of f can use .envp to print actual arguments!
    for(l = 0; l < level; l++) {
        if (!stack[l].e && !stack[l].envp) break;
        if (!l) printf(" @ ");
        lisp f = car(stack[l].e);
        if (f == last) {
            count++;
            continue;
        }

        if (count > 1) {
            printf("%d ", count);
            count = 0;
        }
        if (last) {
            princ(last);
            printf(" -> ");
        }

        last = f;
    }
    if (count > 1) printf("%d ", count);
    if (last) princ(last);
}

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

PRIM evalGC(lisp e, lisp* envp) {
    if (!e) return e;
    char tag = TAG(e);
    // look up variable
    if (tag == symboll_TAG) return getvar(e, *envp); 
    if (tag != symboll_TAG && tag != conss_TAG && tag != thunk_TAG) return e;

    if (level >= MAX_STACK) { error("Stack blowup!"); exit(3); }

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
        // TODO: address growth?
        if (needGC()) {
            printf("\n[We GC:ed but after GC we need another GC - expect slowdowns!!!]\n");
        }
        // check ctlr-t and maybe at queue (GC issue needs resolve first)
        kbhit();
    }

    if (trace) { indent(level); printf("---> "); princ(e); terpri(); }
    level++;
    //if (trace) { indent(level+1); printf(" ENV= "); princ(env); terpri(); }
    if (trace) print_env(*envp);

    lisp r = eval_hlp(e, envp);
    r = reduce_immediate(r);

    --level;
    if (trace) { indent(level); princ(r); printf(" <--- "); princ(e); terpri(); }

    stack[level].e = nil;
    stack[level].envp = NULL;
    return r;
}

PRIM if_(lisp* envp, lisp exp, lisp thn, lisp els) {
    // evalGC is safe here as we don't construct any structes, yet
    // TODO: how did we get here? primapply does call evallist thus created something...
    // but we pass ENV on so it should be safe..., it'll mark it!
    return evalGC(exp, envp) ? mkimmediate(thn, *envp) : mkimmediate(els, *envp);
}

PRIM cond(lisp* envp, lisp all) {
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

PRIM case_(lisp* envp, lisp all) {
    lisp x = evalGC(car(all), envp);
    all = cdr(all);
    while (all) {
        lisp ths = car(all);
        lisp m = member(x, car(ths));
        if (m) return progn(envp, cdr(ths));
        all = cdr(all);
    }
    return nil;
}

PRIM and(lisp* envp, lisp all) {
    lisp r = nil;
    while(all) {
        // TODO: tail call on last?
        r = evalGC(car(all), envp);
        if (!r) return nil;
        all = cdr(all);
    }
    return r;
}

PRIM or(lisp* envp, lisp all) {
    lisp r = nil;
    while(all) {
        // TODO: tail call on last?
        r = evalGC(car(all), envp);
        if (r) return r;
        all = cdr(all);
    }
    return nil;
}

PRIM not(lisp x) {
    return x ? nil : t;
}

// essentially this is a quote but it stores the environment so it's a closure!
PRIM lambda(lisp* envp, lisp all) {
    return mkfunc(all, *envp);
}

PRIM progn(lisp* envp, lisp all) {
    while (all && cdr(all)) {
        evalGC(car(all), envp);
        all = cdr(all);
    }
    // only last form needs be tail recursive..., or if have "return"?
    return mkimmediate(car(all), *envp);
}

static inline lisp letevallist(lisp args, lisp* envp, lisp extend);
static inline lisp letstarevallist(lisp args, lisp* envp, lisp extend);

PRIM let(lisp* envp, lisp all) {
    lisp lenv = letevallist(car(all), envp, *envp);
    return progn(&lenv, cdr(all));
}

PRIM let_star(lisp* envp, lisp all) {
    lisp lenv = letstarevallist(car(all), envp, *envp);
    return progn(&lenv, cdr(all));
}

// use bindEvalList unless NLAMBDA
static inline lisp bindList(lisp fargs, lisp args, lisp env) {
    // TODO: not recurse!
    if (!fargs) return env;
    lisp b = cons(car(fargs), car(args));
    return bindList(cdr(fargs), cdr(args), cons(b, env));
}

static inline lisp bindEvalList(lisp fargs, lisp args, lisp* envp, lisp extend) {
    while (fargs) {
        // This eval cannot be allowed to GC! (since it's part of building a cons structure
        lisp b = cons(car(fargs), eval(car(args), envp));
        extend = cons(b, extend);
        fargs = cdr(fargs);
        args = cdr(args);
    }
    return extend;
}

static inline lisp letevallist(lisp args, lisp* envp, lisp extend) {
    while (args) {
        lisp one = car(args);
        lisp r = eval(car(cdr(one)), envp);
        // This eval cannot be allowed to GC! (since it's part of building a cons structure
        extend = cons(cons(car(one), r), extend);
        args = cdr(args);
    }
    return extend;
}

static inline lisp letstarevallist(lisp args, lisp* envp, lisp extend) {
    while (args) {
        lisp one = car(args);
        lisp r = eval(car(cdr(one)), &extend);
        // This eval cannot be allowed to GC! (since it's part of building a cons structure
        extend = cons(cons(car(one), r), extend);
        args = cdr(args);
    }
    return extend;
}

static inline lisp funcapply(lisp f, lisp args, lisp* envp, int noeval) {
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
// TODO: prim apply/funcapply may return immediate... (so users should call apply instead)
static inline lisp callfunc(lisp f, lisp args, lisp* envp, lisp e, int noeval) {
    int tag = TAG(f);
    if (tag == prim_TAG) return primapply(f, args, envp, e, noeval);
    if (tag == func_TAG) return funcapply(f, args, envp, noeval);
    if (tag == thunk_TAG) return f; // ignore args

    printf("%% "); princ(f); printf(" did not evaluate to a function in: "); princ(e ? e : cons(f, args));
    printf(" evaluated to "); princ(cons(f, args)); terpri();
    error("Not a function");
    return nil;
}

static PRIM test(lisp*);

// ticks are counted up in idle() function, as well as this one, they are semi-unique per run
static long lisp_ticks = 0;
PRIM ticks() { return mkint(lisp_ticks++ & 0xffffffff); } // TODO: mklong?

PRIM clock_() { return mkint(clock_ms()); }

PRIM time_(lisp* envp, lisp exp) {
    int start = clock_ms();
    lisp ret = evalGC(exp, envp);
    int ms = clock_ms() - start;
    return cons(mkint(ms), ret);
}

PRIM load(lisp* envp, lisp name) {
    void evalIt(char* s, char* filename, int startno, int endno) {
        if (!s || !s[0] || s[0] == ';') return;
        printf("\n========================= %s :%d-%d>\n%s\n", filename, startno, endno, s);
        // TODO: way to make it silent?
        // TODO: also, abort on error?
        lisp r = reads(s);
        print(r);
        printf("===>\n\n");
        prin1(evalGC(r, envp)); terpri();
        printf("\n------------------------\n\n");
        //gc(envp); // NOT SAFE!!!?? filename dissapears!
    }

    int r = process_file(getstring(name), evalIt);
    return mkint(r);
}

PRIM at(lisp* envp, lisp spec, lisp f) {
    int c = clock_ms();
    int w = getint(spec);
    lisp r = cons(mkint(c + abs(w)), cons(spec, evalGC(f, envp)));
    lisp bind = getBind(envp, ATSYMBOL, 0);
    // TOOD: insert sort should be easy, only problem is the first
    // so, we could prefix by atom QUEUE.
    setcdr(bind, cons(r, cdr(bind)));
    return r;
}

// we allow stopping original scheduled event, or any repeated
PRIM stop(lisp* envp, lisp at) {
    lisp att = evalGC(at, envp);
    lisp bind = getBind(envp, ATSYMBOL, 0);
    lisp lst = cdr(bind);
    lisp prev = bind;
    while (lst) {
        lisp entry = car(lst);
        if (entry == att || cdr(entry) == cdr(att)) {
            setcdr(prev, cdr(lst)); // remove
            return symbol("*stop*");
        }
        prev = lst;
        lst = cdr(lst);
    }
    return nil;
}

PRIM atrun(lisp* envp) {
    lisp prev = getBind(envp, ATSYMBOL, 0);
    lisp lst = cdr(prev);
    // TODO: sort? now we're checking all tasks all the time.
    // for now don't care as we do it as idle.
    while (lst) {
        int c = clock_ms();
        lisp entry = car(lst);
        int when = getint(car(entry));
        if (when && when < c) {
            int spec = getint(car(cdr(entry)));
            lisp exp = cdr(cdr(entry));
            //printf("[ @%d :: ", when, c); princ(exp); printf(" ");
            //lisp ret =
            apply(exp, nil);
            // TODO: add a way by special return value to unschedule itself if its a repeating task
            //printf(" => "); princ(ret); printf(" ]\n");
            if (spec > 0)
                setcdr(prev, cdr(lst)); // remove
            else
                setcar(entry, mkint(c + abs(spec)));
        }
        prev = lst;
        lst = cdr(lst);
        //if (!lst) terpri();
    }
    return nil;
}

//lisp imacs_(lisp name) {
//    return mkint(imacs_main(0, NULL));
//}

#ifdef UNIX
int xPortGetFreeHeapSize() { return -1; }
#endif

// test stack usage on simple function
// a simple function call takes 32 bytes (8 stack entry as reported by uxTaskGetStackHighWaterMark)
// This means we can recurse about 233 times!
//extern unsigned long uxTaskGetStackHighWaterMark(void*);

PRIM rec(lisp i) {
    int a = getint(i);
//    printf("%d - %u ::: stackUsed=%lu\n", a, (unsigned int)&a, uxTaskGetStackHighWaterMark(NULL));
    return mkint(1 + getint(rec(mkint(a - 1))));
}

// test function: eat the heap
PRIM heap() {
    void* a[60];
    int c = 0;

    int t = 0;
    int z = 16384;
    // TODO: keep allocated in free list and deallocate?
    while (z > 0) {
        //while (xPortGetFreeHeapSize() < z) z = z/2; // enable to make allocation overflow "safer"
        //void* p = pvPortMalloc(z);
        void* p = malloc(z);
        if (!p) {
            z = z/2;
            continue;
        }
        printf("%u %d %d %d\n", (unsigned int)p, z, t, xPortGetFreeHeapSize());
        t += z;
        // test is writable
        int* x = p;
        *x = 4711;
        if (*x != 4711) printf("--- Write failed! %d\n", *x);
        a[c++] = p;
        if (c == 60) break; // before it crashes!
    }
    int i;
    for (i = 0; i < c; i++) {
        free(a[i]);
        printf("%u %d\n", (unsigned int)a[i], xPortGetFreeHeapSize());
    }
    return mkint(t);
}


////////////////////////////////////////////////////////////////////////////////
// flash file access
// 
// - https://blog.cesanta.com/esp8266_using_flash
// ~/GIT/Espruino-on-ESP8266/user/user_main.c 

// TODO: use header
// "FF FF 1A 5H" --- 'FFFFLASH' // GOOD
// "BA DF 1A 5H" --- 'BADFLASH'
// "DE 1F 1A 5H" --- 'DELFLASH'
// "E0 FF 1A 5H" --- 'EOFFLASH'

// record:
// 'FF FF FF FF' - free to write
// 'yy cc xx xx' - yy != 00/ff, active len xx xx, of type yy = 1..254
// '00 cc xx xx' - deleted len xx xx
//
// cc: CRC?

// type yy:
// 0 - (deleted)
// ff - (free)
// bit 7: in USE
// bit 6: printable C string(s) (0=binary)
// bit 5: lisp (serialized string, or binary)
// bit 4: key/value
// bit 0-3: 16 sub types app defined for content

#ifdef UNIX

// Highlevel

// (save '(http boot) (lambda (x) (init-web-server)) (lambda (x) (print "Done")))
//lisp save(lisp key, lisp data, lisp cb) {
//}

// tail-recurses on: cb(key, info, cb)
//lisp dir(lisp matchkey, lisp cb) {
//}

// tail-recurses on: cb(key, info, data, cb, end?)
//lisp load(lisp matchkey, lisp cb) {
//}

#else

// how lua can compile functions to flash file and then call it
// - http://www.esp8266.com/viewtopic.php?f=19&t=1940

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <espressif/spi_flash.h>
#include <espressif/esp_system.h>

#include <FreeRTOS.h>
#include <task.h>

// Wrote 41984 bytes at 0x00000000 in 4.1 seconds (81.6 kbit/s)...
// Wrote 211968 bytes at 0x00020000 in 20.9 seconds (81.0 kbit/s)...
// 
// spi (0-...) address:
// 0x00000000 42 KB
// 0x00020000 (128 KB) 211 KB start ---
// 0x0003C000 ESP private area? (16KB)
// 0x00054C00 end --- 211 KB
// 0x00100000 MINE?
// 0x00400000 end 4MB

// memory mapped range:
// 0x40200000
// 0x40600000 4MByte

/* (swrite "myfile" (list 1 2 3)) */
/* (swrite "abc" "foobar") */
/* (swrite "myfile" 1235) */

/* (sreset "myfile") -> */
/* (sread "myfile") -> (1 2 3) */
/* (sread "myfile") -> 12345 */

/* FLASH: */
/* ("myfile" . (1 2 3))\0 */
/* ("abc" . "foobar")\0 */
/* ("myfile" . 1235)\0 */

/* (point x y c) */
/* (line x y x y c) */
/* (circle ....) */
/* (text ...) */

/* (zap) */
/* (ping) */

#endif

int writeBytesToFlash(char* code, int len, int offset) {
    if (offset % 4 !=0) {
        printf("Illegal offset %d!", offset);
        error("Illegal offset");
        return -1;
    }
    int addr = FS_ADDRESS + offset;
    int sector = addr/SPI_FLASH_SEC_SIZE;
    int to = addr + len;
    int error;
    while (addr < to) {
        // only erase if start write at boundary, otherwise erase already written stuff
        // it will break at next sector and while loop back here and erase next one
        if (offset % SPI_FLASH_SEC_SIZE == 0) sdk_spi_flash_erase_sector(sector);
        char buff[SPI_FLASH_SEC_SIZE] = {0xff};
        int sz = strlen(code) + 1;
        if (sz + offset > SPI_FLASH_SEC_SIZE) {
            sz = SPI_FLASH_SEC_SIZE - offset;
            offset -= SPI_FLASH_SEC_SIZE;
        }
        memcpy(&buff[0], code, sz);
        sz = (sz + 3) & ~3; // round up to 4byte boundary
        if (SPI_FLASH_RESULT_OK != (error = sdk_spi_flash_write(addr, (uint32 *)buff, sz))) {
            printf("\nwriteBytesToFlash error %d\n", error);
        }
        addr += sz;
        code += sz;
        sector++;
    }

    // write and overwritable end marker
//    char c = 0xff;
//    if (SPI_FLASH_RESULT_OK != (error = sdk_spi_flash_write(addr + offset, (uint32 *)&c, 1))) {
//        printf("\nwriteBytesToFlash error %d\n", error);
//    }

    return to - FS_ADDRESS;
}

// TODO: connect to lisp writer?
int writeStringToFlash(char* code, int offset) {
    int len = strlen(code) + 1;
    return writeBytesToFlash(code, len, offset);
}

// TODO: connect to lisp reader!
// if called with NULL just count otherwise write
// returns len in bytes, to read next, round offset up to 4b boundary and call again
// see findLastFlash()

// readFromFlash(NULL, 0, offset) --> count of bytes including 0 at end
// readFromFlash(buff, maxbytes, offset) --> read into buff, length including 0 at end
// readFromFlash(buff, -bytes, offset) --> read bytes into buff, allow 0 // TODO: use sdk_spi_flash_read directly?
// returns -1 if in zeroterm mode and read 0xff
int readFromFlash(char* buff, int maxlen, int offset) {
    int zeroterm = (maxlen >= 0 || !buff);
    maxlen = maxlen >= 0 ? maxlen : -maxlen;

    int error;
    unsigned char c;
    int i;
    for (i = 0; (i < maxlen) || !buff; i++) {
        // TODO: can it really read one char at a time?
        // suspect to cast to uint32 pointer for &(char c)
        if (SPI_FLASH_RESULT_OK != (error = sdk_spi_flash_read(FS_ADDRESS + i + offset, (uint32 *)&c, 1))) {
            printf("\nreadFromFlash.error %d\n", error);
            break;
        }

        // if zeroterm mode end at 0, or at ff, as that's unwritten (?)
        if (zeroterm) {
            if (i == 0 && c == 0xff) return -1;
        }

        if (buff) buff[i] = c;
        if (zeroterm && !c) break;

        if (!buff && i > 100) { // TODO: change
            printf("...enough...!\n");
            return -1;
        }
                                
    }
    if (zeroterm) {
        // make sure zero terminated
        if (buff) buff[maxlen-1] = 0;
        return i + 1;
    } else {
        return i;
    }
}

int findLastFlash() {
    int offset = 0;
    int len;
    while (1) {
        len = readFromFlash(NULL, 0, offset);
        if (len < 0) break;
        offset += (len + 3) & ~3;
    }
    return offset;
}

PRIM scan(lisp s) {
    int maxlen =  SPI_FLASH_SIZE_BYTES - FS_ADDRESS;
    int i;

    if (1) {
//      // includes program ROM
//      uint32* a = (uint32*)0x40200000;
//      a += FS_ADDRESS/4;

        uint32* a = (uint32*)flash_memory;
        int s = 0;
        int c0 = 0;
        int cffffffff = 0;
        //int stop = 0;
        for(i = 0; i < maxlen/4; i++) {
            uint32 v = *a;
            a++;
            //if ((i % (16*1024/4) == 0)) { putchar('.'); fflush(stdout); }
            s += v;
            if (v == 0xffffffff) cffffffff++;
            if (v == 0) c0++;
            //while (v && !stop) {
            if (v) {
                int j;
                for(j = 4; j; j--) {
                    char c = v & 0xff;
                    if (c >= 32 && c <= 125) { putchar(v & 0xff); fflush(stdout); }
                    else if (1) { printf("[%d]", (unsigned char)c); fflush(stdout); }
                    // if (v && 0xff == 0xff) stop = 1;
                    v >>= 8;
                }
            }
            //putchar('/');
        }
        printf("\n");
        return cons(mkint(c0), mkint(cffffffff));
    }

    int error;
    char c;
    int c0 = 0;
    int cff = 0;
    for (i = 0; i < maxlen; i++) {
        // TODO: can it really read one char at a time?
        // suspect to cast to uint32 pointer for &(char c), probably works because alignment allows overwrite
        if (SPI_FLASH_RESULT_OK != (error = sdk_spi_flash_read(FS_ADDRESS + i, (uint32 *)&c, 1))) {
            printf("\nerror %d\n", error);
            break;
        }
        if ((i % (16*1024) == 0)) { putchar('.'); fflush(stdout); }

        if (c == 0) c0++;
        if (c == 0xff) cff++;
    }
    return cons(mkint(c0), mkint(cff));
}

// returns lisp pointer into buffer
lisp serializeLisp(lisp x, lisp* buffer, int *n) {
    printf("\n------ Serializelisp "); print(x);
    if (*n <= 2) return symbol("*FULL*");
    //if (HSYMP(x)) {
        // for now just "pray" - collisions in english language are 190/99K!
        // TODO: serialize by putting first HSYM then string directly after...
        // maybe make all symbol strings in linked list as local symbol table
        // This will be known for flash memory, maybe need bit to indicate?
    //}
    if (!x || INTP(x) || SYMP(x)) return x;
    if (IS(x, string)) {
        // string is simple, just serialize a "heap" object with, pointer (to next cells)
        int sz = sizeof(string);
        memcpy(buffer, x, sz);
        // point to next cell
        sz = (sz + 3) / 4;
        *n -= sz;
        lisp* p = buffer + sz;
        lisp s = (lisp) p;
        buffer[1] = s;
        int len = strlen(getstring(x));

        int ilen = (len + 4) & ~3;
        int iz = ilen / 4;
        if (*n <= 2 + iz) return symbol("*FULL*");
        strncpy((char*)s, getstring(x), len);
        *n -= 2 + iz;
        return (lisp)buffer;
    }
    if (CONSP(x)) {
        // TODO: what if buffer not aligned? cons need be lisp[2] (8 bytes boundary)
        if ((unsigned int)buffer & 7) {
            printf("serializeLisp: not aligned\n");
            *n -= 1;
            return serializeLisp(x, buffer + 1, n);
        }
        *n -= 2;
        lisp* cr = buffer+2; int beforecar = *n;
        buffer[0] = serializeLisp(car(x), cr, n);
        int sz = beforecar - *n;
        buffer[1] = serializeLisp(cdr(x), cr + sz, n);
        printf("CONSP! %x  ", (unsigned int)MKCONS(buffer)); prin1(MKCONS(buffer)); terpri();
        return MKCONS(buffer);
    }

    printf("flashit.ERROR: unsupported type: %d %s\n", TAG(x), tag_name[TAG(x)]);
    return symbol("*ERROR*");
}

#define MAXFLASHPRIM 256

PRIM flashArray(lisp *serialized, int len) {
    if (!CONSP(serialized) && !IS((lisp)serialized, string)) {
        printf("flashArray.ERROR: wrong type %d\n", TAG((lisp)serialized));
        return nil;
    }

    // TODO: move to relocate function, that relocates a RAM region array of lisp*

    // patch up internal references
    lisp* where = malloc(len * sizeof(lisp));
    int i;

    lisp* from = (lisp*)GETCONS(serialized);
    for(i = 0; i < len; i++) {
        lisp p = from[i];
        // TODO: not safe... as it could overlap with some bitrepresentation of other type...
        int o = (lisp)(((unsigned int)p) & ~3) - (lisp)from; // index from from[0]
        //printf("%2d : %d [%x] : ", i, o, (unsigned int)p); prin1(p); terpri();

        where[i] = p;
        if (INTP(p) || SYMP(p))
            ; // TODO: skip over inline symbol...
        else if (stringp(p))
            ; // TODO: skip over inline string...
        else if (o >= 0 && o < MAXFLASHPRIM) // pointers are generated for cons, string (maybe symbol) and some heap types
            where[i] = (lisp)((unsigned int)p - (unsigned int)from + (unsigned int)where);
        //printf("%u %u %d %s\n", (unsigned int) p, (unsigned int) where[i], TAG(where[i]), tag_name[TAG(where[i])]);
    }

    // TODO: actually copy to flash
    int offset = 0; // TODO: find free space to store
    writeBytesToFlash((char*)where, len * sizeof(lisp), offset);
    int flashaddr = (unsigned int)flash_memory + offset;

    free(where);
    if (CONSP(*serialized)) {
        return MKCONS(flashaddr);
    } else {
        // TODO: pointer stupidity, works fine for CONS, string/heap, but not symboll?
        unsigned int us = (unsigned int)serialized;
        return (lisp)((unsigned int)flashaddr | (us & 2)); // 2 ???
    }

    // TODO: remove
    // return pointer to memory, first attemtp
    if (CONSP(*serialized)) return MKCONS(where);

    // TODO: pointer stupidity, works fine for CONS, string/heap, but not symboll?
    unsigned int us = (unsigned int)serialized;
    return (lisp)((unsigned int)where | (us & 2)); // 2 ???
}

PRIM flashit(lisp x) {
    lisp* buffer = malloc(MAXFLASHPRIM * sizeof(lisp)); // align as conss
    memset(buffer, 0, MAXFLASHPRIM * sizeof(lisp));
    int n = MAXFLASHPRIM;
    lisp ret = serializeLisp(x, (lisp*)buffer, &n);
    int len = MAXFLASHPRIM - n;
    if (!len) return x;

  printf("flashit.serialized [len=%d]: ", len); prin1(ret); terpri();
    lisp f = flashArray((lisp*)ret, len);
  printf("flashit.flash [len=%d]: ", len); prin1(f); terpri();
    free(buffer);
    return f;
}

// (flash) -> read all (print), return count
// (flash 3) -> read 3rd entry (1..n), return string, or nil
// (flash -30) -> read at offset 30 (0..), return string, or nil
// (flash "foo") -> append one string
// (flash "foo" 0) -> force write at offset (EOR will mess thins up) (offset = 0 will format)
PRIM flash(lisp s, lisp o) {
    if (!s) { // read all
        int offset = 0;
        int n = 0;
        while (1) {
            int len = readFromFlash(NULL, 0, offset);
            //printf("[LEN=%d]...\n", len);
            if (len < 0) break;

            char buff[len];
            readFromFlash(buff, len, offset);
            n++;
            printf("%d @%d :: [%d] '%s'\n", n, offset, len, buff);

            offset += (len + 3) & ~3;
        }
        return mkint(n);
    } else if (INTP(s)) {
            int n = getint(s);
            int len = -3;
            int offset = n <= 0 ? -n : 0;
            n = n <= 0 ? 1 : n;
            while (n--) {
                offset += (len + 3) & ~3;
                len = readFromFlash(NULL, 0, offset);
                if (len < 0) return nil;
            }
            char buff[len];
            readFromFlash(buff, len, offset);
            return mklenstring(buff, len);
    } else if (IS(s, string)) {
        int offset = integerp(o) ? getint(o) : findLastFlash();
        writeStringToFlash(getstring(s), offset);
        return mkint(findLastFlash());
    } else {
        return nil;
    }
}

////////////////////////////////////////////////////////////////////////////////
// stuff

PRIM fibb(lisp n);

// returns an env with functions
lisp lisp_init() {
    nil = 0;
    int verbose;

    init_symbols();

    // enable to observer startup sequence
    if (1) {
        char* f = readline("start lisp>", 2);
        if (f) {
            verbose = (f[0] != 0);
            free(f);
        } else {
            verbose = 0;
        }
    }
    print_memory_info( verbose ? 2 : 0 ); // init by first call

    lisp env = nil;
    lisp* envp = &env;

    // free up and start over...
    dogc = 0;

    // TODO: this is a leak!!!
    allocs_next = 0;

    // need to before gc_cons_init()...
    _FREE_ = symbol("*FREE*");

    // setup gc clean slate
    mark_clean();
    gc_cons_init();
    gc(NULL);

    t = symbol("t");
    DEFINE(t, 1);
    ATSYMBOL = symbol("*at*");
    DEFINE(ATSYMBOL, nil);

    DEFPRIM(lambda, -7, lambda);

    // types
    DEFPRIM(null?, 1, nullp);
    DEFPRIM(cons?, 1, consp);
    DEFPRIM(atom?, 1, atomp);
    DEFPRIM(string?, 1, stringp);
    DEFPRIM(symbol?, 1, symbolp);
    DEFPRIM(number?, 1, numberp);
    DEFPRIM(integer?, 1, integerp);
    DEFPRIM(func?, 1, funcp);

    // mathy stuff
    DEFPRIM(+, 2, plus);
    DEFPRIM(-, 2, minus);
    DEFPRIM(*, 2, times);
    DEFPRIM(/, 2, divide);
    DEFPRIM(%, 2, mod);

    DEFPRIM(eq, 2, eq);
    DEFPRIM(equal, 2, equal);
    DEFPRIM(=, 2, eq);
    DEFPRIM(<, 2, lessthan);

    // https://www.gnu.org/software/guile/manual/html_node/Pattern-Matching.html#Pattern-Matching
    DEFPRIM(if, -3, if_);
    DEFPRIM(cond, -7, cond);
    DEFPRIM(case, -7, case_);
    DEFPRIM(and, -7, and);
    DEFPRIM(or, -7, or);
    DEFPRIM(not, 1, not);

    // output
    // TODO: write procedures? - http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-ref/Output-Procedures.html
    // TODO: make these take a "fd" or output stream?
    DEFPRIM(terpri, 0, terpri);
    DEFPRIM(princ, 1, princ);
    DEFPRIM(prin1, 1, prin1);
    DEFPRIM(print, 1, print);
    DEFPRIM(printf, 7, printf_);
    DEFPRIM(pp, 1, pp); // TODO: pprint?
    DEFPRIM(with-putc, -7, with_putc);
    DEFPRIM(with-fd, -7, with_fd);
    DEFPRIM(with-fd-json, -7, with_fd_json);

    // cons/list
    DEFPRIM(cons, 2, cons);
    DEFPRIM(car, 1, car_);
    DEFPRIM(cdr, 1, cdr_);
    DEFPRIM(set-car!, 2, setcar);
    DEFPRIM(set-cdr!, 2, setcdr);

    DEFPRIM(list, 7, _quote);
    DEFPRIM(length, 1, length);
    DEFPRIM(concat, 7, concat); // scheme: string-append/string-concatenate
    DEFPRIM(char, 1, char_); // scheme: integer->char
    DEFPRIM(split, 3, split); // scheme: string-split
    DEFPRIM(assoc, 2, assoc);
    DEFPRIM(member, 2, member);
    DEFPRIM(mapcar, 2, mapcar);
    DEFPRIM(map, 2, map);
    DEFPRIM(quote, -1, _quote);
    // DEFPRIM(quote, -7, quote); // TODO: consider it to quote list?
    // DEFPRIM(list, 7, listlist);

    DEFPRIM(let, -7, let);
    DEFPRIM(let*, -7, let_star);
    DEFPRIM(progn, -7, progn);
    DEFPRIM(eval, 2, _eval);
    DEFPRIM(evallist, 2, evallist);
    DEFPRIM(apply, 2, apply);
    DEFPRIM(env, -7, _env);

    DEFPRIM(read, 1, read_);

    // TODO: consider introducting these that will create local bindings if no global exists, hmm bad?
    DEFPRIM(set, -2, _set);
    //DEFPRIM(setq, -2, _setq);
    //DEFPRIM(setqq, -2, _setqq_);
    DEFPRIM(set!, -2, _setbang);

    DEFPRIM(define, -7, _define);
    DEFPRIM(de, -7, de);

    DEFPRIM(fundef, 1, fundef);
    DEFPRIM(funenv, 1, funenv);
    DEFPRIM(funame, 1, funame);

    // define
    // defun
    // defmacro
    // while
    // gensym

    // network
    DEFPRIM(wget, 3, wget_);
    DEFPRIM(web, -2, web);

    // hardware
    DEFPRIM(out, 2, out);
    DEFPRIM(in, 1, in);
    DEFPRIM(interrupt, 2, interrupt);

    DEFPRIM (delay, 1, delay);
    DEFPRIM (led_data, 2, led_data);
    DEFPRIM (led_show, 5, led_show);

    // system stuff
    DEFPRIM(gc, -1, gc);
    DEFPRIM(test, -7, test);

    DEFPRIM(ticks, 1, ticks);
    DEFPRIM(clock, 1, clock_);
    DEFPRIM(time, -1, time_);
    DEFPRIM(load, -1, load);

    // debugging - http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-user/Debugging-Aids.html 
    // http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-user/Command_002dLine-Debugger.html#Command_002dLine-Debugger
    // TODO: set-trace! set-break! http://www.lilypond.org/doc/v2.19/Documentation/contributor/debugging-scheme-code
    DEFPRIM(pstack, 0, print_detailed_stack); 

    // flash stuff - experimental
    DEFPRIM(flash, 2, flash);
    DEFPRIM(flashit, 1, flashit);
    DEFPRIM(scan, 2, scan);
    // TODO: consider integrating with - https://www.gnu.org/software/guile/manual/html_node/Symbol-Props.html
    // 2d-!!! https://groups.csail.mit.edu/mac/ftpdir/scheme-7.4/doc-html/scheme_12.html#SEC105
    // 2d-put! 2d-remove! 2d-get 2d-get-alist-x 2d-get-alist-y

    // scheduling
    DEFPRIM(at, -2, at); // TODO: eval at => #stop?!?!??!
    DEFPRIM(stop, -1, stop);
    // DEFPRIM(atrun, -1, atrun); // no reason for user to call (yet)

    // DEFPRIM(imacs, -1, imacs_); // link in the imacs?
    DEFPRIM(syms, 0, syms); // TODO: rename to apropos?
    DEFPRIM(fib, 1, fibb);

    //DEFPRIM(readit, 0, readit);
    DEFPRIM(heap, 1, heap);
    DEFPRIM(rec, 1, rec);

    // another small lisp in 1K lines
    // - https://github.com/rui314/minilisp/blob/master/minilisp.c

    // neat "trick" - limit how much 'trace on' will print by injecting nil bound to nil
    // in evalGC function the print ENV stops when arriving at this token, thusly
    // will not show any variables defined above...
    env = cons( cons(nil, nil), env );

    dogc = 1;

    print_memory_info( verbose ? 2 : 0 ); // summary of init usage
    return env;
}

void help(lisp* envp) {
    printf("\n\nWelcome to esp-lisp!\n");
    printf("2016 (c) Jonas S Karlsson under MPL 2.0\n");
    printf("Read more on https://github.com/yesco/esp-lisp/\n\n");
    printf("Global/SYMBOLS: ");
    PRINT((syms (lambda (x) (princ x) (princ " "))));
    printf("\nCOMMANDS: help/trace on/trace off/gc on/gc off/wifi SSID PSWD/wget SERVER URL/mem EXPR/quit/exit\n\n");
    printf("CTRL-C: to break execution, CTRL-T: shows current time/load status, CTRL-D: to exit\n\n");
    printf("Type 'help' to get this message again\n");
}

// TODO: make it take one lisp parameter?
// TODO: https://groups.csail.mit.edu/mac/ftpdir/scheme-7.4/doc-html/scheme_17.html#SEC153
void error(char* msg) {
    jmp_buf empty = {0};
    static int error_level = 0;

    // restore output to stdout, if error inside print function we're screwed otherwise!
    writeputc = origputc;

    if (error_level == 0) {
        error_level++;
        if (level) { printf("\n%%%s\nBacktrace: ", msg); print_stack(); terpri(); }
        print_detailed_stack();
        printf("\n%% %s\n", msg);
        error_level--;
    } else {
        error_level = 0;
        printf("\n%% error(): error inside error... recovering...\n");
    }

    printf("\n%%%% type 'help' to get help\n");

    if (memcmp(lisp_break, empty, sizeof(empty))) { // contains valid value
        // reset stack
        level = 0;
        stack[0].e = nil;
        stack[0].envp = NULL;

        longjmp(lisp_break, 1);
        // does not continue!
    } else {
        printf("\n%%%% error(): didn't get here as setjmp not called, continuing... possibly bad\n");
    }
}

void run(char* s, lisp* envp) {
    if (setjmp(lisp_break) == 0) {
        lisp r = reads(s);
        prin1(evalGC(r, envp)); terpri();
        // TODO: report parsing allocs separately?
        // mark(r); // keep history?
    } else {
        // escaped w ctrl-c (longjmp)

        // enableGC and kill stack
        blockGC = 0;
        level = 0;
    }
    // disable longjmp
    memset(lisp_break, 0, sizeof(lisp_break));

    gc(envp); // TODO: maybe move out!
}

// it would not be completely safe to run multiple threads of lisp at the
// same time, problems are GC and allocations. Therefore we provide a tasker
// that handles "actors". To drive this, we only do it while idle, i.e,
// when waiting for keyboard input. This is similar to NodeMCU.
//
// The system actors that are checked are:
// - TODO: web server
// - TODO: wget 

void maybeGC() {
    if (blockGC || !global_envp) return;
    if (needGC()) gc(global_envp);
}

void handleInterrupts() {
    // TODO: maybe cache the symbols? not matter in idle, but if called elsewhere
    int checkpin(int pin, uint32_t clicked, uint32_t count, uint32_t last) {
        char name[16] = {0};
        snprintf(name, sizeof(name), "int%02d", pin);
        // this will define the symbol, but only for interrupts enabled
        lisp handler = getvar(symbol(name), *global_envp);
        if (!handler) return -666;
        //printf("BUTTON: %d count %d last %d\n", pin, count, last);
        lisp r = apply(handler, list(mkint(pin), mkint(clicked), mkint(count), mkint(last), END));
        return getint(r);
    }
                  
    checkInterrupts(checkpin);
}

PRIM atrun(lisp* envp);

PRIM idle(int lticks) {
    // 1 000 000 == 1s for x61 laptop
    //if (lticks % 1000000 == 0) { putchar('^'); fflush(stdout); }

    // polling tasks, invoking callbacks
    web_one();
    atrun(global_envp);
    handleInterrupts();

    // gc
    maybeGC(); // TODO: backoff, can't do all the time???

    // clean stats
    print_memory_info(0);

    return nil;
}

// tweenex style ctrl-t process status
// freebsd10$ sleep 5 ... ctrl-t
// load: 0.67  cmd: sleep 90628 [nanslp] 0.92r 0.00u 0.00s 0% 1464k
// sleep: about 4 second(s) left out of the original 5
// Could print "load" (ticks last second, keep time last tick)
void print_status(long last_ticks, long ticks, int last_ms, int ms, int max_ticks_per_ms) {
    int s = ms / 1000;
    int m = s / 60; s -= m * 60;
    // we approximate the load with ticks seen since "last"
    int ld = 100 - (100 * (ticks - last_ticks)) / max_ticks_per_ms;
    if (ld < 0) ld = 0;
    if (ld > 99) ld = 99;
    // %3.2f doesn't work on esp-open-rtos
    printf("[%% %d:%02d load: 0.%02d ", m, s, ld);
    // printf("dticks: %ld, last: %ld, ticks: %ld mtps = %d",
    //    ld, ticks - last_ticks, last_ticks, ticks, max_ticks_per_ms);
    print_stack();
    printf("]\n");
}

// make an blocking mygetchar() that waits for kbhit() to be true 
// and meanwhile calls idle() continously.
static int thechar = 0;

// called from idle in tight loop, also called each time we gc() during execution
int kbhit() {
    static int last_ms = 0;
    static int last_ticks = 0;
    static int max_ticks_per_ms = 1;

    int ms = clock_ms();
    int update = 0;
    // update every s
    if (ms - last_ms > 1000) {
        int tms = (lisp_ticks - last_ticks) / (ms - last_ms);
        if (tms > max_ticks_per_ms) max_ticks_per_ms = tms;
        update = 1;
    }

    // TODO: "bug", if there is a ungotten char that won't be read, ctrl-c ctrl-t will not be seen :-(
    // alt 1) could dispose of characters :-(
    // alt 2) have a "buffer" (but how long?) may be unbounded and trouble for piped strings to program
    if (!thechar) {
        thechar = nonblock_getch();
        if (thechar < 0) thechar = 0;
        if (thechar == 'T'-64) {
            print_status(last_ticks, lisp_ticks++, last_ms, ms, max_ticks_per_ms);
            thechar = 0;
        }
        if (thechar == 'C'-64) {
            int c = thechar;
            thechar = 0;
            error("CTRL-C");
            // error only returns if couln't longjmp to setjmp position, so keep the ctrl-c
            thechar = c;
        }
    }

    if (update) {
        last_ms = ms;
        last_ticks = lisp_ticks;
    }

    return thechar;
}

int mygetchar() {
    while (!kbhit()) idle(lisp_ticks++); 
    int c = thechar;
    thechar = 0;
    return c;
}

int lispreadchar(char *chp) {
    int c = mygetchar();
    if (c >= 0) *chp = c;
    return c < 0 ? -1 : 1;
}

char *pLightsDefines[] = {
  "(list (define initialStateNum 1) (define mult 5))",
  "(define stNum initialStateNum)",
  "(define cols '(red amber green))",
  "(list (define redl   (lambda (n) (out 12 n))) (define amberl (lambda (n) (out 0 n))) (define greenl (lambda (n) (out 5 n))))",
  "(define redld (lambda (n o) (list (redl n) (delay (* o mult)) (clearl))))",
  "(define amberld (lambda (n o) (list (amberl n) (delay (* o mult)) (clearl))))",
  "(define greenld (lambda (n o) (list (greenl n) (delay (* o mult)) (clearl))))",
  "(define redPattern (lambda (n) (list (redld 1 50) (delay (* 10 mult)) (redld 1 20) ) ))",
  "(define amberPattern (lambda (n) (list (amberld 1 50) (delay (* 10 mult)) (amberld 1 20) ) ))",
  "(define greenPattern (lambda (n) (list (greenld 1 50) (delay (* 10 mult)) (greenld 1 20) ) ))",
  "(define lights (lambda (m n o) (list (redl m) (amberl n) (greenl o))))",
  "(list (define clearl (lambda () (lights 0 0 0 ))) (define stopl  (lambda () (lights 1 0 0))) )",
  "(list (define readyl (lambda () (lights 1 1 0))) (define gol    (lambda () (greenPattern))) (define slowl  (lambda () (lights 0 0 1))))",
  "(list (define stopc  '(redPattern)) (define readyc '(redl amberl)))",
  "(list (define goc    '(greenl)) (define slowc  '(amberl)))",
  "(define states '(stopc readyc goc slowc))",
  "(define incf (lambda (m) (let ((xx (+ (eval m) 1))) (set m xx))))",
  "(define decf (lambda (m) (let ((xx (- (eval m) 1))) (set m xx))))",
  "(define nth (lambda (n xs) (cond ((eq n 1) (car xs)) (t (nth (- n 1) (cdr xs))))))",
  "(define stateItem (lambda (n) (nth n states)))",
  "(define loopStNum (lambda () (cond ((eq stNum (length states)) (set 'stNum 1)) (t (incf 'stNum)))))",
  "(define backStNum (lambda () (cond ((eq stNum 1) (set 'stNum (length states))) (t (decf 'stNum)))))",
  "(define setl (lambda (s) ((eval s) 1)))",
  "(define showLights (lambda () (mapcar setl (eval (stateItem stNum)))))",
  "(define changeLights (lambda () (list (loopStNum) (clearl) (showLights))))",
  "(define backLights (lambda () (list (backStNum) (clearl) (showLights))))",
  "(define upMult   (lambda () (cond ((eq mult 10) (set 'mult 10)) (t (incf 'mult)))))",
  "(define downMult (lambda () (cond ((eq mult 1)  (set 'mult 1))  (t (decf 'mult)))))",
  "(list (interrupt 2 2) (interrupt 4 2))",
  "(list (define (int02 pin clicks count ms) (downMult)) (define (int04 pin clicks count ms) (upMult)))",
  "(at -5000 (lambda () (changeLights)))"
};

char *pNumbersDefines[] = {
  "(define spt (lambda () (led_show 15 8 1 1 5)))",
  "(define sptt (lambda () (led_show 15 8 1 0 5)))",
  "(define ledd (lambda () (list (led_data '( 6 6 5 5 ) 0) (led_show 4 0 0 0 5) (led_show 1 0 0 0 5) (led_show 2 0 0 0 5) (sptt) )))",
  "(ledd)",
  "(define nth (lambda (n xs) (cond ((eq n 1) (car xs)) (t (nth (- n 1) (cdr xs))))))",
  "(define drop (lambda (x xs) (cond ((eq x 0) xs) (t (drop (- x 1) (cdr xs))))))",
  "(define take (lambda (x xs) (cond ((eq x 0) nil) (t (cons (car xs) (take (- x 1) (cdr xs)))))))",
  "(define append (lambda (xs ys) (if (= (car xs) nil) ys (cons (car xs) (append (cdr xs) ys) ))))",
  "(define rotate (lambda (n xs) (if (= (car xs) nil) nil (append (drop n xs) (take n xs)))))",
  "(define incf (lambda (m) (let ((xx (+ (eval m) 1))) (set m xx))))",
  "(define wheels 1)",
  "(set! wheels '( ( 6 6 5 5 ) ( 2 2 4 3 ) ( 2 3 3 5 ) (10 11 12 13) ))",
  "(define curWheel 1)",
  "(define rotCount '(0 0 0 0))",
  "(define srcHelper (lambda (n v) (append (take (- n 1) rotCount) (cons v (drop n rotCount)))))",
  "(define setRotCount (lambda (n v) (let ((xx (cond ((eq n 1) (cons v (drop 1 rotCount))) ((eq n 2) (srcHelper n v)) ((eq n 3) (srcHelper n v)) (t (append (take 3 rotCount) (cons v nil))) ))) (set 'rotCount xx))))",
  "(define loopRotDisp (lambda () (cond ((eq (nth curWheel rotCount) 3) (setRotCount curWheel 0)) (t (setRotCount curWheel (+ (nth curWheel rotCount) 1))))))",
  "(define loopCurWheel (lambda () (cond ((eq curWheel 4) (set 'curWheel 1)) (t (incf 'curWheel)))))",
  "(define rotDisp (lambda () (loopRotDisp)))",
  "(define wheelDisp (lambda () (nth curWheel wheels)))",
  "(define showDisp (lambda () (list (led_data (rotate (nth curWheel rotCount) (wheelDisp)) 0) (ans) (sptt))))",
  "(interrupt 2 2)",
  "(interrupt 4 2)",
  "(define (int02 pin clicks count ms) (list (rotDisp) (showDisp)))",
  "(define (int04 pin clicks count ms) (list (loopCurWheel) (showDisp)))",
  "(define wheelShow (lambda (n) (rotate (nth n rotCount) (nth n wheels))))",
  "(define zip2 (lambda (xs ys zs) (cond ((eq (car xs) nil) nil) ((eq (car ys) nil) nil) ((eq (car zs) nil) nil) (t (cons (list (car xs) (car ys) (car zs)) (zip2 (cdr xs) (cdr ys) (cdr zs) ) )) ) ))",
  "(define sum3 (lambda (t) (+ (+ (car t) (nth 2 t)) (nth 3 t))))",
  "(define ans (lambda () (led_data (mapcar sum3 (zip2 (wheelShow 1) (wheelShow 2) (wheelShow 3))) 4) ))",
  ";",
  ";",
  ";"
};

// lights
// int defineCount = 31;
// numbers
int defineCount = 29; 

// char **pDefines = pLightsDefines;
char **pDefines = pNumbersDefines;

int libLoaded = 0; 
int currentDefine = 0; 

int noFree = 0;

char *pComment = ";";

void readeval(lisp* envp) {
    help(envp);

    int last = 0;
    
    while(1) {
        global_envp = envp; // allow idle to gc
        char* ln = NULL;

        // execute auto-load functions from defines array
        if (libLoaded == 0) {

       	  // default value
       	  ln = pComment;
          noFree = 1;

          // clock check in loop creates space between executing functions
          if ((clock_ms() - last) > 200) {
    		  ln = pDefines[currentDefine];

			  currentDefine = currentDefine + 1;

			  if (currentDefine == defineCount) {
				libLoaded = 1;
			  }

			  last = clock_ms();
          }
        }
        else {
          ln = readline_int("lisp> ", READLINE_MAXLEN, lispreadchar);
        }

        global_envp = NULL;

        if (ln != NULL) {
//        	printf("ln %s", ln);
        }
        else {
        	printf("ln == NULL", ln);
        }

        if (!ln) {
        	printf("break");
            break;
        } else if (strncmp(ln, ";", 1) == 0) {
            ; // comment - ignore
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
        } else if (strcmp(ln, "exit") == 0 || strcmp(ln, "quit") == 0 || strcmp(ln, "bye") == 0) {
            exit(0);
        } else if (strlen(ln) > 0) { // lisp
            global_envp = envp; // allow idle to gc
            print_memory_info(0);
            run(ln, envp);
            global_envp = NULL;
            //print_memory_info(1);
        }

        if (noFree == 0) {

          free(ln);
        }
        else {
          noFree = 0;
        }
    }

    printf("OK, bye!\n");
}

void treads(char* s) {
    printf("\nread-%s: ", s);
    princ(reads(s));
    terpri();
}
    
int fib(int n) {
    if (n < 2) return 1;
    else return fib(n-1) + fib(n-2);
}

PRIM fibb(lisp n) { return mkint(fib(getint(n))); }

// lisp implemented library functions hardcoded
void init_library(lisp* envp) {
  //DEFINE(fibo, (lambda (n) (if (< n 2) 1 (+ (fibo (- n 1)) (fibo (- n 2))))));
  DE((fibo (n) (if (< n 2) 1 (+ (fibo (- n 1)) (fibo (- n 2))))));

// POSSIBLE encodings to save memory:
    // symbol: fibo
    // "fibo" 
// 0: (lambda
// 1:  (n)
// 2: (if
// 3:  (<
// 4:   n
// 5:   2)
// 6:  1
// 7:  (+
// 8:   (fibo
//10:    (-
//11:     n
//12:      1))
//14:    (fibo
//15:     (-
//16:      n
//17:      2))))));
// 18 lines + 9 paranthesises, a cons 8 bytes
// (* (+ 18 9) 8) = 216 bytes
//
// possible storage strategies:
// - 64 bytes: (lambda (n) (if (< n 2) 1 (+ (fibo (- n 1)) (fibo (- n 2)))))
// - 52 bytes: (lambda(n)(if(< n 2)1(+(fibo(- n 1))(fibo(- n 2)))))
//
// - 24 conses! (* 24 8) = 192 bytes
// - 25: L3 lambda L1 n L4 if L3 < n 2 1 L2 + L2 fibo L3 - n 1 L2 fibo L3 - n 2
//   25 slots 25*4 = 100 bytes, can't detect end of list by "cdr"
// - 34: L3 lambda L1 n \0 L4 if L3 < n 2 1 \0 L2 + L2 fibo L3 - n 1 \0 \0 L2 fibo L3 - n 2 \0 \0 \0 \0 \0
//   34 slots (* 34 4) = 136 bytes, 
// - L:fibo  lambda L:P L:IF \0  4
//   L:P     n \0                2
//   L:IF    if L:X 1 L:E \0     4
//   L:X     < n 2 \0            4
//   L:E     + L:F1 L:F2 \0      4
//   L:F1    fibo L:M1 \0        3
//   L:M1    - n 1 \0            4
//   L:F2    fibo L:M2 \0        3
//   L:M2    - n 2 \0            4
//                            =====
//                              32 = 128 bytes
//
// - skip list with end and compact
//  #34 lambda #3 n \0 #26 if #5 < n 2 \0 1 #18 + #8 fibo #5 - n 1 \0 \0 #8 fibo #5 - n 2 \0 \0 \0 \0 \0
//  skip marker 1-2 bytes, end marker \0 1 bytes
//  9 skips, 9 ends = 18 bytes
//  8 functions, 6 are basic (2b), 2 user (fibo) (4b) =  (+ (* 6 2) (* 2 4)) = 20 bytes
//  4 numbers: 2, 1, 1, 2 - 2 byte x 4 = 8 bytes
//  TOTAL: (+ 18 20 8) = 46 bytes compact!
//  but: can execute? need a new "VM", or decode on the fly to "cons"
//
// - forth stack style: : fibo 1 ref 2 < 0skip? jmp:+3 1 ret 1 ref 2 - fibo 1 ref 2 - fibo + ret ;
//   20 instructions: 40 bytes
// 
// HOWEVER: If put in flash, maybe we don't need to care?
// 
//   picolisp: Only 20 of the 196 bytes stay in RAM (for the fibo symbol), the rest is moved off to ROM.
//   esp-lisp: all conses can move "moved" to FLASH! (192 bytes!) + 4 bytes for the "function", must be RAM, plus symbol (none)
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

static PRIM test(lisp* e) {

// removing the body of this function gives: (- 42688 41152) 1536
// gives 25148 bytes, 19672k
#ifdef TEST_REMOVED
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
    TEST(nil, (car (quote (nil . nil)))); // lol (was an issue)
    TEST(nil, (cdr (quote (nil . nil)))); // lol

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
    TEST((define a (+ 3 4)), 7);
    //TEST((setqq b a), a);
    TEST((set! b (quote a)));
    TEST(b, a);
    //TEST((set b 3), 3);
    //TEST(a, 3);
    //TEST(b, a);
       
    // if
    lisp IF = mkprim("if", -3, if_);
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
    DEFINE(fac, (lambda (n) (if (= n 0) 1 (* n (fac (- n 1))))));
    TEST((fac 6), 720);
    TEST((fac 21), 952369152);

    // tail recursion optimization test (don't blow up stack!)
    DEFINE(bb, (lambda (b) (+ b 3)));
    DEFINE(aa, (lambda (a) (bb a)));
    TEST((aa 7), 10);

    DEFINE(tail, (lambda (n s) (if (eq n 0) s (tail (- n 1) (+ s 1)))));
    TEST(tail, xyz);
    testss(envp, LOOPTAIL, LOOPS);

    // progn, progn tail recursion
    TEST((progn 1 2 3), 3);
    TEST((set! a nil), nil);
    TEST((progn (set! a (cons 1 a)) (set! a (cons 2 a)) (set! a (cons 3 a))),
         (3 2 1));

    // implicit progn in lambda
    DEFINE(f, (lambda (n) (set! n (+ n 1)) (set! n (+ n 1)) (set! n (+ n 1))));
    TEST((f 0), 3);

//    PRINT((define tailprogn (lambda (n) (progn 3 2 1 (if (= n 0) (quote ok) (tailprogn (- n 1)))))));
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

    TEST((set! a 2));
    TEST((list 1 2 (let ((a (+ 1 a)) (b a)) (list a (+ b b))) 5 (+(+ a (+ a a))), (1 2 (3 4) 5 6)));
    TEST(a, 2);

#else
    printf("%%Tests have been commented out.\n");
#endif
    return nil;
}

void lisp_run(lisp* envp) {
    init_library(envp);
    readeval(envp);
    return;
}

// hardware spi pins
int cs_pin = 12 ;
int clk_pin = 14;
int data_pin = 13;

#define MAXREG_DECODEMODE 0x09
#define MAXREG_INTENSITY  0x0A
#define MAXREG_SCANLIMIT  0x0B
#define MAXREG_SHUTDOWN   0x0C
#define MAXREG_DISPTEST   0x0F

void shiftOut(unsigned char* data, int delay);
unsigned char sendChar(const char data, const bool dp);

void spi_led(int init, int digit, int val, int decode, int delay)
{
	gpio_enable(cs_pin, GPIO_OUTPUT);
	gpio_enable(clk_pin, GPIO_OUTPUT);
	gpio_enable(data_pin, GPIO_OUTPUT);

//	bool bSpi = spi_init(0, 2, 4, true, SPI_BIG_ENDIAN, true);
//	bool bSpi = spi_init(1, 0, 4, true, SPI_BIG_ENDIAN, true);

//	const spi_settings_t my_settings = {
//	.mode = SPI_MODE0,
//	.freq_divider = SPI_FREQ_DIV_4M,
//	.msb = true,
//	.endianness = SPI_LITTLE_ENDIAN,
//	.minimal_pins = true
//	};

//	spi_settings_t old;
//	spi_get_settings(1, &old); // save current settings

//	printf("mode %d ", old.mode);
//	printf("dvd %d ", old.freq_divider);
//	printf("msb %d ", old.msb);
//	printf("end %d ", old.endianness);
//	printf("min %d ", old.minimal_pins);

	// useful comments in this code re cpol, cpha
	//https://github.com/MetalPhreak/ESP8266_SPI_Driver/blob/master/driver/spi.c
	// settings from spi.h, look reasonable
//	spi_init(1, SPI_MODE0, SPI_FREQ_DIV_10M, true, SPI_LITTLE_ENDIAN, false ); //true);

	// send two bytes, d15 first
	//see pdf p6 for format
//	Table 1. Serial-Data Format (16 Bits)
//	D15 D14
//	X
//	D13 D12
//	X X
//	D11 D10 D9 D8
//	ADDRESS
//	D7 D6 D5 D4
//	X
//	D3 D2 D1 D0
//	MSB DATA LSB


	unsigned char bytes[2];

	unsigned char initC = (unsigned char)init;

	if (init > 0) {

		if (initC & 0x04) {
			bytes[0] = MAXREG_SHUTDOWN;
			bytes[1] = 0x01;
			shiftOut(bytes, delay);
		}

		if (initC & 0x01) {
			bytes[0] = MAXREG_SCANLIMIT;
			bytes[1] = 0x07;
			shiftOut(bytes, delay);
		}

		if (initC & 0x02) {
			bytes[0] = MAXREG_DECODEMODE;

			if (decode > 0) {
				bytes[1] = 0xFF;
				decodeMode = 1;
			}
			else {
				bytes[1] = 0x0;
				decodeMode = 0;
			}

			shiftOut(bytes, delay);
		}

		if (initC & 0x08) {
			bytes[0] = MAXREG_DISPTEST;
			bytes[1] = 0x00;
			shiftOut(bytes, delay);
		}

		if (initC & 0x10) {
			bytes[0] = MAXREG_INTENSITY;
			bytes[1] = (unsigned char)val;
			shiftOut(bytes, delay);
		}

		if (initC & 0x20) {
			for (unsigned char i = 0; i < 8; i++) {
				bytes[0] = i + 1;
				bytes[1] = 0;

				shiftOut(bytes, delay);
			}
		}
	}

	for (unsigned char i = 0; i < digit; i++) {
		bytes[0] = 8-i; // i+ 1;

		if (decodeMode == 1) {
			bytes[1] = spiData[i]; 
		}
		else {
			bytes[1] = sendChar(spiData[i], false);
		}

		shiftOut(bytes, delay);
	}

//	spi_set_settings(1, &old); // restore saved settings
}

void send2Byte(unsigned char reg, unsigned char data);

// check this page
// http://www.instructables.com/id/MAX7219-8-Digit-LED-Display-Module-Driver-for-ESP8/step4/MAX7219-Driver-Implementation/
// also
// https://github.com/wayoda/LedControl/blob/master/src/LedControl.cpp
void shiftOut(unsigned char* data, int delay)
{
    gpio_write(cs_pin, 0);

    send2Byte(data[0], data[1]);

    gpio_write(cs_pin, 1);

	vTaskDelay(delay);
    
    return;
}

void send2Byte(unsigned char reg, unsigned char data) {

    uint16_t info = reg*256+data;

//	    uint16_t retVal = spi_transfer_16(1, info);
//
//	    printf("rv %d ", retVal);
//
//	    return;

    char i = 16;

    do {
        gpio_write(clk_pin, 0);
        
        if(info & 0x8000) {
    	    gpio_write(data_pin, 1);
      	} 
    	else {
    	    gpio_write(data_pin, 0);
    	}

        gpio_write(clk_pin, 1);

    	info <<= 1;
    } while(--i);
}

unsigned char sendChar(const char data, const bool dp)
{
	unsigned char converted = 0b0000001;    // hyphen as default

  // look up bit pattern if possible
  if (data >= ' ' && data <= 'z')
    converted = MAX7219_font[data - ' '];

  // 'or' in the decimal point if required
  if (dp)
    converted |= 0b10000000;

  return converted;
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
