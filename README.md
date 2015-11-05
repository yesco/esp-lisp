# esp-lisp
BETA: A small fast lisp interpeter for a ESP8266 as alternative to lua on the nodemcu.

## why

Who doesn't need a lisp? I always wanted my own "lispmachine" anyway ;) It's ideally small and interactive for experimentation, very little typing, compared to lua for example, easy to extend with FFI to add new functions.

## design goals

- embedded small lisp
- memory efficient
- full closures ala scheme
- tail recursion to allow for actors/coroutines
- interactive development instead of compile/upload/run
- readline or better terminal interface
- easy to add functions by registering, good FFI
- no macros use NLAMBDA concept instead

## status

The core is about 1000 lines of code. Total less than 3000 with xml/web server support.

I just have the first  working [relase](https://github.com/yesco/esp-lisp/releases), got a editable readline interface! See the docs in [wiki](https://github.com/yesco/esp-lisp/wiki) for simple examples.

Lot's of stuff is missing...

## features

- small (~ n*1000 lines of code)
- full scheme style closures (make your own objects)
- lisp reader/printer
- datatypes: string, atom, int, cons, prim, thunk, immediate, func
- simple mark/sweep GC
- efficient storage of conses, with no overhead per cell, no tag word needed
- inline (no overhead at all!) small ints, short symbols (&lt;=6 chars) stored INSIDE POINTER!
- tail recursion optimization using "immediate" thunks, handles mutual/self recursion
- eval/neval primitive functions, no need for macros, no code explosion
- readline, with limited editing (backspace), similar to nodemcu lua
- interpreted

## performace

The esp-lisp is interpreted, to keep the code small and simple. Compared to lua from the NodeMcu it's about 2x slower, but lua is compiled and uses lots of memory for functions. Lua uses about 600 bytes for a simple function, whereas esp-lisp about 100 bytes for a function printing hello.

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

- Get https://github.com/yesco/esp-lisp

These will now be in the same directory.

- unix:esp-lisp> ./run

That will compile and run it on your desktop, it will also make the flash for the esp.

- unix:esp-lisp> make flash

Flashes it to your esp-8266 device.

- unix:esp-lisp> ./mcu

To connect to it and run it.

## TODO

- regression tests - DONE
- more tests
- simple web interface (ala http://john.freml.in/teepeedee2-vs-picolisp) - DONE
- speed regression test suite (tail recursion and http://software-lab.de/radical.pdf)
- add functionality ala nodemcu see http://www.nodemcu.com/docs/
- want nodemcu spiffs style filesystem on flash - depends on esp-open-rtos/issue
- hardware io functions: spi i2c
- put lisp functions in ROM at compile time, see below for inspiriation
- get lcd/tft display functions ala nodemcu
- make a stand-alone lisp machine, that can take PC2 keyboard and a 240x160 color display

## Readings idea for embedded ROM lisps

Optimize more for storage/simplify and avoid using malloc here are some ideas from elsewhere.

- http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.109.6660&rep=rep1&type=pdf
- picolisp speed tests... http://software-lab.de/radical.pdf
- picolisp puts compiled lisp in ROM - http://picolisp.com/wiki/!pdf?-B1054

## Related stuff

- https://hackaday.io/project/3116-pip-arduino-web-browser/log/17804-a-picture-is-worth-2-re-writes
- https://github.com/obdev/v-usb
- https://github.com/denilsonsa/atmega8-magnetometer-usb-mouse
- http://blog.tynemouthsoftware.co.uk/2012/02/arduino-based-zx81-usb-keyboard.html

