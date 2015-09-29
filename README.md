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

## how to build

In a directory:

1. Get https://github.com/SuperHouse/esp-open-rtos (and all it wants)
2. Build it.
3. Get https://github.com/yesco/esp-lisp

These will now be in the same directory.

4. esp-lisp> ./run

That will compile and run it on your desktop, it will also make the flash for the esp.

5. esp-lisp> make flash

Flashes it to your esp-8266 device.

6. esp-lisp> ./mcu

To connect to it and run it.




