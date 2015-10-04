# esp-lisp
BETA: A small fast lisp interpeter for a ESP8266 as alternative to lua on the nodemcu.

## why

Who doesn't need a lisp? I always wanted my own "lispmachine" anyway ;) It's ideally small and interactive for experimentation, very little typing, compared to lua for example, easy to extend with FFI to add new functions.

## design goals

- embedded small lisp
- full closures ala scheme
- tail recursion to allow for actors/coroutines
- interactive development instead of compile/upload/run
- readline or better terminal interface
- easy to add functions by registering, good FFI
- no macros use NLAMBDA concept instead

## status

It's about 1000 lines of code.

I just have the first  working [relase](https://github.com/yesco/esp-lisp/releases), got a readline interface! See the docs in [wiki](https://github.com/yesco/esp-lisp/wiki) for simple examples.

Lot's of stuff is missing...

## features

- small
- full scheme style closures
- lisp reader/printer
- datatypes: string, atom, int, cons, prim, thunk, immediate, func
- simple mark/sweep GC
- tail recursion optimization using "immediate" thunks, handles mutual/self recursion
- eval/neval primitive functions, no need for macros
- readline, with limited editing (backspace), similar to nodemcu lua
- interpreted

## performace

The esp-lisp is interpreted, to keep the code small and simple. Compared to lua from the NodeMcu it's about 2x slower, but lua is compiled and uses lots of memory for functions (about 600 bytes for a simple call).

## how to build

### I want to run it on my linux/cygwin, I have GCC

- Get https://github.com/yesco/esp-lisp
- esp-lisp> ./run

It'll compile and run it for you, you'll have a lisp prompt.

	lisp> help
	...

try out the commands, it also shows what functions/symbols there are

	lisp> (+ 3 4)
	7

	lisp> (setq fac (lambda (n) (if (= n 0) 1 (* n (fac (- n 1))))))
	#func[]

	lisp> (fac 6)
	720

### build embeddable image and flash it to a nodemcu/EPS8266 device

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
- add functionality ala nodemcu see http://www.nodemcu.com/docs/
- want nodemcu spiffs style filesystem on flash
- hardware io functions: spi i2c
- put lisp functions in ROM at compile time, see below for inspiriation
- get display functions ala nodemcu

## Readings idea for embedded ROM lisps

Optimize more for storage/simplify and avoid using malloc here are some ideas from elsewhere.

- http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.109.6660&rep=rep1&type=pdf
- picolisp speed tests... http://software-lab.de/radical.pdf
- picolisp puts compiled lisp in ROM - http://picolisp.com/wiki/!pdf?-B1054


