# esp-lisp
Embroy for a ESP8266 lisp interpreter as alternative to lua on the nodemcu

## why

Lua is too much typing, a minimal closure or function on the nodemcu lua takes up about 600 bytes!
I always wanted my own "lispmachine" ;)

## status

Embryo:
- lisp reader
- lisp princ, terpri
- datatypes: string, atom, int, cons, prim
- fake eval
- mark/sweep GC

## features
- esp-lisp is interpreted so it's about 2x slower than lua that is compiled and use lots of memory
- full closures
- mutal/self tail recursion optimization using "immediate" thunks
- simple GC/mark sweep
- eval/neval primitive functions, no need for macros
- cheap to embed
- readline, with limited editing (backspace)

## how to build

In a directory:

- Get https://github.com/SuperHouse/esp-open-rtos (and all it wants)
- Build it.

This is temporary, we need to patch in for read/write to get readline interactive on device!

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




