/* Distributed under Mozilla Public Licence 2.0   */
/* https://www.mozilla.org/en-US/MPL/2.0/         */
/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* A mini "lisp machine", symbols                 */

// moved this out to symbols.c

// lisp> (time (fibo 13 
//   (530 . 377)
// lisp> (time (fibo 14
//   (870 . 610)
// lisp> (time (fibo 15
//   (1430 . 987)
// lisp> (time (fibo 16 
//   (2330 . 1597)

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "lisp.h"

#define LARSONS_SALT 0

static unsigned long larsons_hash(const char* s, int len) {
    unsigned long h = LARSONS_SALT;
    while (len-- > 0 && *s)
        h = h * 101 + *(unsigned char*)s++;
    return h;
}

// in many basic lisp program most function names/symbols are 6 charcters or less
// few special characters are used (- _ ? /), if other fancy character used (< + etc)
//
// first try, len <= 6:
//   pointer = aaaaabbbbbcccccdddddeeeeefffff11 = 32 bits len <= 6 each char mapped to range 32 as per below
// if len <=3 or any character out of range:
//   pointer = aaaaaaabbbbbbbccccccc00001111111 = 32 bits len <= 3 ascii characters 0-127
// TODO: find "clever" use of 0000 "xxxx"!!!!
//
// encode char -> 0..30, 31=11111 on last char indicates 3ascii
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

char* sym2str(lisp s, char name[7]) {
    if (!s) return NULL;
    // 3ASCII?
    unsigned int n = (unsigned int) s;
    if (n % 128 == 127) {
        name[0] = (n >> (32-1*7)) % 128;
        name[1] = (n >> (32-2*7)) % 128;
        name[2] = (n >> (32-3*7)) % 128;
        name[3] = 0;
        return &name[0];
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
    return &name[0];
}

lisp symbol_len(char *s, int len) {
    if (s && len == 3 && strncmp(s, "nil", len) == 0) return nil; // hack, to keep nil==0
    lisp sym = str2sym(s, len);
    if (sym) return sym;

    // string doesn't fit inside pointer, hash the name
    unsigned long h = larsons_hash(s, len);
    h = (h ^ (h >> 16) ^ (h << 16)) & 0xffffff; // 24 bits
    sym = (lisp)(h << 8 | 0xfff); // lower 8 bits all set! (and one 0)
    // TODO: detect collission!!!!
    hashsym(sym, s, len, 1); // create binding
    return sym;
}

// use char* as string as already in RAM/ROM/program memory, no need copy
lisp symbol(char* s) {
    if (!s) return nil;
    return symbol_len(s, strlen(s));
}

// hash symbols
// purpose is two fold:
// 1. find symbol (to unique-ify the pointer for a name)
// 2. lookup value of "global" symbol

// ==== unmodified
// lisp> (time (fibo 22))
//   (23 . 28657)
// lisp> (time (fibo 30))
//   (1099 . 1346269)
// === with hashsyms
// lisp> (time (fibo 22))
//   (19 . 28657)
// lisp> (time (fibo 30))
//   (914 . 1346269)
// === summary
// lisp> (- 1099 914)
//   185
// lisp> (/ 18500 914)
//   20
// !!!! 20% faster!!!

// memory usage:
// === unmodified
// used_count=72 cons_count=354 free=19580 USED=16 bytes
// === hashsym
// used_count=72 cons_count=486 free=17320 USED=12 bytes  // SLOTS = 63
// used_count=72 cons_count=486 free=17608 USED=12 bytes  // SLOTS = 31 (latest)
// used_count=72 cons_count=486 free=17592 USED=12 bytes  // SLOTS = 31
// used_count= 8 cons_count=510 free=17360 USED=12 bytes  // SLOTS = 2
// 
// (- 19580 17608) = 1972 bytes!!! WTF?
// (* 31 16) = 496 bytes for hashsym array // 31 slots
// (* (- 70 31) (+ 16 8)) = 936 bytes for hashsym allocs of linked list items
// TOTAL: (+ 496 936) = 1432
// however, we free (- 486 354) = 132 conses for the bindings (* 132 8) = 1056 bytes "saved" (or moved)
//
// (- 1972 1432) = 540 bytes unaccounted for... (extra strings?)
//
// we even saved 63*4 bytes (maybe in prim)? :=( ???? where they go?

// TODO: remove more error strings
// TODO: no need create binding for symbol that has no value (HASH function not to be called)
// TODO: since never free some types (prim symbol (and symbol name) symbol value allocate from separate heap with no overhead)
//                                   that is *8 bytes per allocation no other overhead (to get correct pointers)
//                                   symbol name can be from another heap space, no alignment needed, allocate block?
// 
//    prim:  63 allocations of  1008 bytes, and still use  63 total  1008 bytes
//    symbol:   7 allocations of    84 bytes, and still use   7 total    84 bytes

// so if can merge prim with symbol_value could save 1008 bytes with the saved 1056 bytes concells that's even...

// I'm still thinking that most global named variables == binding == symbol == primitive function, sot let's combine all!

// -- untouched w task and stacksize=2048
// used_count=73 cons_count=353 free=13620 USED=16 bytes stackMax=36 startMem=29384 startTask=20108 afterInit=14328
// lisp> (time (fibo 22))
//   (1980 . 28657)
// lisp> (time (fibo 30))
//   (94570 . 1346269)

// -- hashsym w task and stacksize=2048
// used_count=74 cons_count=486 free=12792 USED=12 bytes stackUsed=1642 startMem=29248 startTask=19876 afterInit=12892 
// lisp> (time (fibo 22))
//   (1840 . 28657)
// lisp> (time (fibo 30))
//   (86830 . 1346269)
// used_count=74 cons_count=487 free=12172 USED=12 bytes stackUsed=26 startMem=29248 startTask=19876 afterInit=12892 
//
// ==> (- 1980 1840) = 140 (/ 14000 1980) = 7% faster
// ==> (- 94570 86830) = 7740 (/ 774000 94570) = 8% faster
// ==> (- 14328 12892) = 1436 bytes more memory used, (- 486 353) = 133 cons:es freed => (* 133 16) = 2128! bytes
//     TOTAL: gained (- 2128 1436) = 692 bytes!
// 
// Drawback, each (non global) symbol creates global entry with binding to nil

// this structure has three usages:
// 1. it's an element of the symbol hash table, the symbol's name is "hashed"
// 2. hashSym() & getBind() finds the symbol given and returns it as a "cons"
//    of binding by changing the ponter to a conss (MKCONS)
// 3. store PRIM primtive functions, as they are global/persistent in same way
//    as a symbol. value will store a pointer to the struct marked as PRIM (MKPRIM).

// TODO: PRIM alternative construct, make symbol_val part of "prim"
// advantage 1) since it's alreay heap allocated 16b -> 24b, might as well use 4m byte of "lisp" tag
// advantage 2) free's up a bit pattern
typedef struct { // a "super-cons" (scons)
    lisp symbol; // car.car (also used as the name of PRIM function)
    lisp value;  // car.cdr
    lisp next;   // cdr - linked list of ones in same bucket
    lisp extra;  // used to store PRIM primitive function pointer, if not prim, TODO: may not be needed, hmmm // how to?
    char s[0];   // only if HSYMP(symbol), then allocated
} symbol_val;

#define GETSYM(p) ((symbol_val*) (((unsigned int)(p)) & ~7))

// be aware this only works for !SYMP(s) && IS(s, symboll)
char* symbol_getString(lisp s) {
    if (!HSYMP(s)) return "*NOTSYMBOL*";
    lisp hs = hashsym(s, NULL, 0, 0); // no create binding
    symbol_val* sv = GETSYM(hs);
    return (char*)&(sv->s);
}

// http://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
//static unsigned int int_hash(unsigned int x) {
//    x = ((x >> 16) ^ x) * 0x45d9f3b;
//    x = ((x >> 16) ^ x) * 0x45d9f3b;
//    x = ((x >> 16) ^ x);
//    return x;
//}

// cannot be 2^N (because we're dealing with ascii and it's regularity in bits)
#define SYM_SLOTS 63
//#define SYM_SLOTS 31
//#define SYM_SLOTS 2

// afterInit=14716 when using ARRAY
// afterInit=14264 ... lost (- 14716 14264) = 452 bytes (80*4 bytes overhead of malloc)
// BUT TOTAL => (- 14262 12104) 2158 bytes saved!!!! 
symbol_val** symbol_hash; // malloc to align correctly on esp8266

// TODO: generlize for lisp type ARRAY and HASH!!!

// returns a "binding" as a "conss" (same structure, but isn't)
// optionalString if given is used to create a new entry/check collision if not inline symbol pointer
lisp hashsym(lisp sym, char* optionalString, int len, int create_binding) {
    if (!symbol_hash) {
        symbol_hash = myMalloc(SYM_SLOTS * sizeof(symbol_val*), -1);
        memset(symbol_hash, 0, SYM_SLOTS * sizeof(symbol_val*));
    }
    if (!sym) return nil;

    unsigned long h = (unsigned long) sym; // inline characters, or hashed symbol, just use the bits as is
    if (!SYMP(sym)) {
        printf("\n\n%% hashsym.error: unknown type of symbol (%s): ", optionalString); princ(sym); terpri();
        exit(1);
    }
    h = h % SYM_SLOTS;
    symbol_val* s = symbol_hash[h];
    while (s && s->symbol != sym) s = (symbol_val*)s->next;
    if (s) {
        if (optionalString && HSYMP(h)) { // hashed name - check is same!!!
            // TODO: check, and if error do WHAT?
            // if not same, means collision, it's serious
            // (ly unprobable, but may happen, 290 words english collide out of 99171)
        }
        return MKCONS(s);
    } else if (!create_binding) {
        printf("%% Symbol unbound: "); princ(sym);
        error("%% Symbol unbound"); // this will show stack and go back toplevel
        return nil;
    } else {
        // not there, insert first
        symbol_val* nw = myMalloc(sizeof(symbol_val) + len + 1, -1);
        nw->symbol = sym;
        nw->value = nil;
        nw->next = (lisp) symbol_hash[h];
        nw->extra = nil;
        if (len) {
            //printf("STRING: %s\n", optionalString);
            strncpy((char*)&(nw->s), optionalString, len);
            *((char*)&(nw->s) + len) = 0; // need to terminate explicitly
        }
        symbol_hash[h] = nw;

        return MKCONS(nw); // pretend it's a cons!
    }
}

void init_symbols() {
    // initialize symbol stuff with allocate one real symbol
    hashsym(nil, NULL, 0, 0);
}

// quirky - piggy back on a symbol/hash_sym
//   x: -rw-r--r-- 1 knoppix knoppix 220688 Dec 15 19:17 0x20000.bin
//  64: -rw-r--r-- 1 knoppix knoppix 222768 Dec 15 20:41 0x20000.bin
// 127: -rw-r--r-- 1 knoppix knoppix 235344 Dec 15 19:31 0x20000.bin
// ==> (- 222768 220688) 2080 bytes extra for 64, but saves XXXX bytes from heap!!! (- 14716 12104) = 2614 bytes!!!
// ==> 14656 bytes extra for 128 bytes !!!
lisp mkprim(char* name, int n, void *f) {
    lisp s = hashsym(symbol(name), name, strlen(name), 1);
    symbol_val* prim = (symbol_val*) (((unsigned int)s) & ~2); // GETCONS()
    
    prim->value = nil; // set later anyway
    if ((unsigned int)f & 15 || abs(n) > 7) {
        printf("\n\n%% Function: %s %d not aligned %d = %x, need specify LISP\n", name, n, (unsigned int)f, (unsigned int)f);
        exit(1);
    }
    // maybe don't need -7 .. 7, will require 4 bits!
    prim->extra = f + n + 7;
    return MKPRIM(prim);
}

inline int getprimnum(lisp p) {
    symbol_val* prim = GETSYM(p);
    return ((unsigned int)(prim->extra) & 15) - 7;
}

inline void* getprimfunc(lisp p) {
    symbol_val* prim = GETSYM(p);
    return (void*)((unsigned int)(prim->extra) & ~15);
}

void syms_mark() {
    int i;
    for(i = 0; i < SYM_SLOTS; i++) {
        symbol_val* s = symbol_hash[i];
        while (s && s->symbol) {
            if (s->value) mark(s->value);
            s = (symbol_val*)s->next;
        }
    }
}

// print the slots
// TODO: maybe call it apropos? http://www.gnu.org/software/mit-scheme/documentation/mit-scheme-user/Debugging-Aids.html
// TODO: https://groups.csail.mit.edu/mac/ftpdir/scheme-7.4/doc-html/scheme_11.html#SEC97
// symbol? symbol->string intern inter-soft string->symbol symbol-append symbol-hash symbol-hash-mod symbol<?
PRIM syms(lisp f) {
    int n = 0;
    int i;
    for(i = 0; i < SYM_SLOTS; i++) {
        symbol_val* s = symbol_hash[i];
        if (!f) printf("%3d : ", i);
        int nn = 0;
        while (s && s->symbol) {
            nn++;
            if (!f) {
                princ(s->symbol); putchar('='); princ(s->value); putchar(' ');
            } else {
                // TODO: may run out of memory... GC?
                apply(f, list(s->symbol, s->value, mkint(i), END));
            }
            s = (symbol_val*)s->next;
        }
        n += nn;
        if (!f) printf(" --- #%d\n", nn);
    }
    
    return mkint(n);
}
