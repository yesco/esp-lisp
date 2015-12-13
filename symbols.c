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
//#include <ctype.h>
//#include <stdarg.h>
#include <string.h>
//#include <stdlib.h>
#include <stdio.h>

#include "lisp.h"

// TODO: somehow a symbol is very similar to a conss cell.
// it has two pointers, next/cdr, diff is first pointer points a naked string/not lisp string. Maybe it should?
// TODO: if we make this a 2-cell or 1-cell lisp? or maybe symbols should have no property list or value, just use ENV for that
typedef struct symboll {
    char tag;
    char xx;
    short index;

    struct symboll* next;
    char* name; // TODO should be char name[1]; // inline allocation!
    // TODO: lisp value; // globla value (prims)
} symboll;

// be aware this only works for !SYMP(s) && IS(s, symboll)
char* symbol_getString(lisp s) {
    if (SYMP(s) || !IS(s, symboll)) return "*NOTSYMBOL*";
    return ATTR(symboll, s, name);
}

// TODO: remove! use hashsyms instead
static symboll* symbol_list = NULL;

// don't call this directly, call symbol
static lisp secretMkSymbol(char* s) {
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

void sym2str(lisp s, char name[7]) {
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

static lisp find_symbol(char *s, int len) {
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

inline lisp HASH(lisp s) { 
    hashsym(s); // make sure have global binding in "symtable"
    return s;
}

// linear search to intern the string
// will always return same symbol
lisp symbol(char* s) {
    if (!s) return nil;
    lisp sym = find_symbol(s, strlen(s));
    if (sym) return HASH(sym);
    return HASH(secretMkSymbol(s));
}

// only have this in order to keep track of allocations
char* my_strndup(char* s, int len) {
    int l = strlen(s);
    if (l > len) l = len;
    char* r = myMalloc(len + 1, -1);
    strncpy(r, s, len);
    return r;
}

// create a copy of partial string if not found
lisp symbolCopy(char* start, int len) {
    lisp sym = find_symbol(start, len);
    if (sym) return HASH(sym);
    return HASH(secretMkSymbol(my_strndup(start, len)));
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

#define LARSONS_SALT 0

//static unsigned long larsons_hash(const char* s) {
//    unsigned long h = LARSONS_SALT;
//    while (*s)
//        h = h * 101 + *(unsigned char*)s++;
//    return h;
//}

typedef struct {
    lisp symbol;
    lisp value;
    lisp next; // linked list of ones in same bucket
    // padding as this struct is returned as a fake conss pointer (!) from getBind(), need align correctly
    lisp extra; // TODO: what more to store for symbols??? get rid of PRIM type maybe!!!
} symbol_val;

// http://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
//static unsigned int int_hash(unsigned int x) {
//    x = ((x >> 16) ^ x) * 0x45d9f3b;
//    x = ((x >> 16) ^ x) * 0x45d9f3b;
//    x = ((x >> 16) ^ x);
//    return x;
//}

// cannot be 2^N (because we're dealing with ascii and it's regularity in bits)
//#define SYM_SLOTS 63
#define SYM_SLOTS 31
//#define SYM_SLOTS 2

symbol_val* symbol_hash; // malloc to align correctly on esp8266

// TODO: generlize for lisp type ARRAY and HASH!!!

// returns a "binding" as a "conss" (same structure, but isn't)
lisp hashsym(lisp sym) {
    if (!symbol_hash) {
        symbol_hash = myMalloc(SYM_SLOTS * sizeof(symbol_val), -1);
        memset(symbol_hash, 0, SYM_SLOTS * sizeof(symbol_val));
    }

    unsigned long h = 0;
    if (SYMP(sym)) h = (unsigned long)sym;
    //else if (IS(sym, symboll)) h = larsons_hash(ATTR(symboll, sym, name)); // TODO: this is close to hashatoms effect
    else if (IS(sym, symboll)) h = (unsigned int)sym; // TODO: ok for now, but should use  hashatoms?

    h = h % SYM_SLOTS;
    symbol_val* s = &symbol_hash[h];
    while (s && s->symbol != sym) s = (symbol_val*)s->next;
    if (s) {
        return MKCONS(s);
    } else {
        // not there, insert first
        symbol_val* prev = NULL;
        if (symbol_hash[h].symbol) {
            prev = myMalloc(sizeof(symbol_val), -1);
            memcpy(prev, &symbol_hash[h], sizeof(symbol_val));
        }
        symbol_val* nw = &symbol_hash[h];
        nw->symbol = sym;
        nw->value = nil;
        nw->next = (lisp)prev;
        return MKCONS(nw); // pretend it's a cons!
    }
}

void syms_mark() {
    int i;
    for(i = 0; i < SYM_SLOTS; i++) {
        symbol_val* s = &symbol_hash[i];
        while (s && s->symbol) {
            if (s->value) mark(s->value);
            s = (symbol_val*)s->next;
        }
    }
}

// print the slots
lisp syms(lisp f) {
    int n = 0;
    int i;
    for(i = 0; i < SYM_SLOTS; i++) {
        symbol_val* s = &symbol_hash[i];
        if (!f) printf("%3d : ", i);
        int nn = 0;
        while (s && s->symbol) {
            nn++;
            if (!f) {
                princ(s->symbol); putchar('='); princ(s->value); putchar(' ');
            } else {
                lisp env = nil;
                // TODO: may run out of memory... GC?
                funcall(f, list(s->symbol, s->value, mkint(i), END), &env, nil, 1);
            }
            s = (symbol_val*)s->next;
        }
        n += nn;
        if (!f) printf(" --- #%d\n", nn);
    }
    
    return mkint(n);
}
