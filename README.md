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

1. Get https://github.com/SuperHouse/esp-open-rtos (and all it wants)
2. Build it.

This is temporary, we need to patch in for read/write to get readline interactive on device!

2.2. (temp) patch it for IO read using uart https://github.com/SuperHouse/esp-open-rtos/pull/31
2.5. (temp) instructions on https://help.github.com/articles/checking-out-pull-requests-locally/
2.7. (temp) esp-open-rtos> git fetch origin pull/ID/head:uart
2.9. (temp) esp-open-rtos> git checkout uart
2.9.5 (temp) buid it...

3. Get https://github.com/yesco/esp-lisp

These will now be in the same directory.

4. esp-lisp> ./run

That will compile and run it on your desktop, it will also make the flash for the esp.

5. esp-lisp> make flash

Flashes it to your esp-8266 device.

6. esp-lisp> ./mcu

To connect to it and run it.




