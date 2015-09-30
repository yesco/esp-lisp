# esp-lisp
Embroy for a ESP8266 lisp interpreter as alternative to lua on the nodemcu.

## why

Lua is too much typing, a minimal closure or function on the nodemcu lua takes up about 600 bytes!
I always wanted my own "lispmachine" anyway ;)

## design goals

- full closures
- tail recursion
- interactive development instead of compile/upload/run
- readline or better interface
- embedded small lisp
- easy to add functions by registering, good FFI
- no macros use NLAMBDA concept instead

## status

It's one step away from being useful. 

- esp-lisp is interpreted so it's about 2x slower than lua that is compiled and use lots of memory

It's about 1200 lines of code.

## features

- small
- full scheme style closures
- lisp reader/printer
- datatypes: string, atom, int, cons, prim, thunk, immediate, func
- simple mark/sweep GC
- tail recursion optimization using "immediate" thunks, handles mutual/self recursion
- eval/neval primitive functions, no need for macros
- readline, with limited editing (backspace), similar to nodemcu lua

## how to build

In a directory:

- Get https://github.com/SuperHouse/esp-open-rtos (and all it wants)
- Build it.

This is temporary; we need to patch in for read/write to get readline interactive on device!

- (temp) patch it for IO read using uart https://github.com/SuperHouse/esp-open-rtos/pull/31
- (temp) instructions on https://help.github.com/articles/checking-out-pull-requests-locally/
- (temp) esp-open-rtos> git fetch origin pull/ID/head:uart
- (temp) esp-open-rtos> git checkout uart
- (temp) buid it...

- Get https://github.com/yesco/esp-lisp

These will now be in the same directory.

- esp-lisp> ./run

That will compile and run it on your desktop, it will also make the flash for the esp.

- esp-lisp> make flash

Flashes it to your esp-8266 device.

- esp-lisp> ./mcu

To connect to it and run it.

## TODO

- remove cons from c heap, make real lisp heap
- make inline atoms
- regression tests for types and functions
- simple web interface (ala http://john.freml.in/teepeedee2-vs-picolisp)
- speed regression test suite (tail recursion and http://software-lab.de/radical.pdf)

## Readings idea for embedded ROM lisps

Optimize more for storage/simplify and avoid using malloc here are some ideas from elsewhere.

- http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.109.6660&rep=rep1&type=pdf
- picolisp speed tests... http://software-lab.de/radical.pdf
- picolisp puts compiled lisp in ROM - http://picolisp.com/wiki/!pdf?-B1054


