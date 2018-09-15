# esp-lisp
BETA: A small fast lisp interpeter for a ESP8266 as alternative to lua on the nodemcu.

## why

Who doesn't need a lisp? I always wanted my own "lispmachine" anyway ;) It's ideally small and interactive for experimentation, very little typing, compared to lua for example, easy to extend with FFI to add new functions.

## what is lisp?

Lisp is syntactically a very simple language. It has in principle just function calls:

    lisp> (+ 3 4)
    => 7
    
I'll teach you lisp in 15 minutes ;-) - [teach you lisp video](https://www.youtube.com/watch?v=_X_rx9SjDfw), from my talk at Chiang Mai Makerfaire, Thailand 2016 [slides](https://rawgit.com/yesco/hangmai-makerfair-esp-lisp-2016/master/index.html).

## design goals

- embedded small lisp - OK
- memory efficient - OK
- full closures ala scheme - DONE
- tail recursion to allow for actors/coroutines - "OK"
- interactive development instead of compile/upload/run - OK
- readline or better terminal interface - DONE
- easy to add functions by registering, good FFI - DONE
- no macros use NLAMBDA concept instead - "OK" 

## internals presentations

You can learn some about this lisp internals in my talk and the discussion at [Hackware: Holiday 2015 Special](https://engineers.sg/video/esp-lisp-hackware--467), which took place in Singapore end of last year.

A more detailed presentation takes place/took place in Hong Kong Functional Programming meetup at HKU. [slides](http://1drv.ms/1RiVNzq), [video](https://www.youtube.com/watch?v=jtrkCbiQVjM&feature=youtu.be)

## status

The core is about 1000 lines of code. Total around 4000 with extentions functions and xml/web server support.

It is now at it's 4th release [relase](https://github.com/yesco/esp-lisp/releases), it now even got a full screen editor functions (ala emacs), got a editable readline interface! See the docs in [wiki](https://github.com/yesco/esp-lisp/wiki) for simple examples.

The esp-lisp has been used to build some smaller game devices by other persons, still things being added.

## features

- small (~ n*1000 lines of code)
- full scheme style closures (make your own objects)
- lisp reader/printer
- datatypes: string, atom, int, cons, prim, thunk, immediate, func
- simple mark/sweep GC
- efficient storage of conses, with no overhead per cell, no tag word needed
- inline (no overhead at all!) small ints, short symbols (&lt;=6 chars) stored INSIDE POINTER!
- tail recursion optimization using "immediate" thunks, handles mutual/self recursion
- eval/neval LAMBDA/NLAMBDA primitive functions, no need for macros, no code explosion [reddit](https://www.reddit.com/r/lisp/comments/4u19sf/esp_lisp/)
- readline, with limited editing (backspace), similar to nodemcu lua
- full screen editor function, like emacs implemented in ~500 lines single file [imacs](https://github.com/yesco/imacs).
- interpreted
- in/out/dht functions
- interrupt (counting) api and callback functions

## performace

The esp-lisp is interpreted, to keep the code small and simple. Compared to lua from the NodeMCU it's about 2x slower, but lua is compiled and uses lots of memory for functions. Lua uses about 600 bytes for a simple function, whereas esp-lisp about 100 bytes for a function printing hello.

Comparing it to guile by running (fib 33) takes about 10s on guile, but only 5s on esp-lisp!

## advanced terminal interaction

In the read-eval loop:
- CTRL-C will terminate input and start over on new line
- CTRL-H/DEL will delete last character
- CTRL-L will reprint line cleanly
- CTRL-T print current status time/load

### Debugging

When your code is running you can press CTRL-T to get a compressed stackview.

	lisp> (define (fib n) (if (< n 1) 1 (+ (fib (- n 1)) (fib (- n 2)))))
	#fib
	lisp> (fib 30)
       each time you press CTRL-T it'll print the stack: 21 nested fib
	[% 0:07 load: 0.99  @ 21 #fib]
	[% 0:07 load: 0.99  @ 21 #fib]
	[% 0:07 load: 0.99  @ 20 #fib]
	[% 0:07 load: 0.98  @ 23 #fib]
	[% 0:07 load: 0.98  @ 23 #fib]
	[% 0:08 load: 0.99  @ 19 #fib]
	[% 0:08 load: 0.99  @ 20 #fib]
	[% 0:08 load: 0.99  @ 23 #fib]
	[% 0:08 load: 0.98  @ 20 #fib]
	[% 0:08 load: 0.98  @ 21 #fib]
	[% 0:08 load: 0.97  @ 19 #fib]
	[% 0:08 load: 0.96  @ 22 #fib]
	[% 0:11 load: 0.99  @ 20 #fib]
	2178309
	lisp> 

CTRL-C to break the execution and do some debugging:

	lisp> (define (fib n) (if (< n 1) 1 (+ (fib (- n 1)) (fib (- n 2)))))
	(define (fib n) (if (< n 1) 1 (+ (fib (- n 1)) (fib (- n 2)))))
	#fib
	lisp> (fib 30)
	(fib 30)
	
	^C

	 @ 22 #fib

	(#< n 1) 
	debug 22] 
	---------
	nil
	debug 22] p
	p
	---------
	  STACK:  @ 22 #fib
	CURRENT: (#< n 1)
		ENV: ((n . 0) (nil))
	debug 22] bt
	bt
	---------

	   0 : (#fib 30) ENV: [#fib n=30]
	   1 : (#fib (#- n 1)) ENV: [#fib n=29]
	   2 : (#fib (#- n 1)) ENV: [#fib n=28]
	   3 : (#fib (#- n 2)) ENV: [#fib n=26]
	   4 : (#fib (#- n 1)) ENV: [#fib n=25]
	   5 : (#fib (#- n 2)) ENV: [#fib n=23]
	   6 : (#fib (#- n 1)) ENV: [#fib n=22]
	   7 : (#fib (#- n 2)) ENV: [#fib n=20]
	   8 : (#fib (#- n 1)) ENV: [#fib n=19]
	   9 : (#fib (#- n 2)) ENV: [#fib n=17]
	  10 : (#fib (#- n 2)) ENV: [#fib n=15]
	  11 : (#fib (#- n 1)) ENV: [#fib n=14]
	  12 : (#fib (#- n 2)) ENV: [#fib n=12]
	  13 : (#fib (#- n 1)) ENV: [#fib n=11]
	  14 : (#fib (#- n 1)) ENV: [#fib n=10]
	  15 : (#fib (#- n 1)) ENV: [#fib n=9]
	  16 : (#fib (#- n 1)) ENV: [#fib n=8]
	  17 : (#fib (#- n 1)) ENV: [#fib n=7]
	  18 : (#fib (#- n 2)) ENV: [#fib n=5]
	  19 : (#fib (#- n 1)) ENV: [#fib n=4]
	  20 : (#fib (#- n 1)) ENV: [#fib n=3]
	  21 : (#fib (#- n 1)) ENV: [#fib n=2]
	==>  22 : (#fib (#- n 2)) ENV: [#fib n=0]
	  23 : (#< n 1) ENV: [#< ... ] 
	debug 22] h
	h
	---------
	Debug help: q(uit) h(elp) p(rint env) u(p) d(own) b(ack)t(race) EXPR
	debug 22] u
	u
	---------
	  STACK:  @ 22 #fib
	CURRENT: (#fib (#- n 2))
		ENV: ((n . 2) (nil))
	debug 22] u
	u
	---------
	  STACK:  @ 22 #fib
	CURRENT: (#fib (#- n 2))
		ENV: ((n . 2) (nil))
	debug 21] u
	u
	---------
	  STACK:  @ 22 #fib
	CURRENT: (#fib (#- n 1))
		ENV: ((n . 3) (nil))

### Full screen emacs-style editing

Uses the [imacs](https://github.com/yesco/imacs) minimal editor implementation as a sub-project.

    lisp> (define txt "foobar")
    lisp> (set! txt (edit txt))
    ... edit in fullscreen! ...

    (+ 3 4) ctrl-x ctrl-e  ==> 7

### During evalation
- CTRL-T print current status time/load and a compressed stack (names of functions only)
- CTRL-C will "break" the code and print the stack, cleanup and go to top-level
- 'kill -20 <pid>' in another will clear screen and print the current stack
- 'watch -n 0 kill -20 '<pid>' will continously print the stack; try it on '(fibo 90)' - quite facinating!

## how to build

In the example below I "assume" you have a directory GIT where you checkout all your projects.

### I want to run it on my linux/cygwin, I have GCC

- unix:GIT> git clone http://github.com/yesco/esp-lisp.git --recursive
- unix:GIT> cd esp-lisp
- unix:GIT/esp-lisp> ./run

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

Create a file path-add-esp (one level up from esp-lisp).
It should contain something like:

          unix:GIT> cat path-add-esp
          export PATH=/home/USER/...GIT/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
          unix:GIT> source path-add-esp

- unix:esp-lisp> ./run

That will compile and run it on your desktop.

- unix:esp-lisp> make flash

Flashes it to your esp-8266 device.

- unix:esp-lisp> ./mcu

To connect to it and run it. Requires screen.

## Memory optimizations

- integers stored free inside the pointer (only 32-3-29 bits) - DONE (increased speed 30%!)
- a-z up to 6 letter symbol names stored for free inside pointer - DONE (saved 1600 bytes!)
- 3 ascii symbol names stored for free inside pointer - DONE
- global bindings stored in a hashed "symbol table" - DONE (increase speed 15%, saved 150 cons! ~ 1KB)
- primtive functions are global and stored inside the global binding (4 bytes overhead) - DONE (saved 600 bytes!)
- long name/non-alpha symbols allocated in global symbol table using "hash pointer" (for use in flash!)
- cons stored in preallocated chunk with one bit overhead per cons, size about 5K - DONE (saved 8 bytes per cons)

## TODO

- regression tests - in progress
- more tests - working on it
- simple web server interface (ala http://john.freml.in/teepeedee2-vs-picolisp) - DONE
- speed regression test suite (tail recursion and http://software-lab.de/radical.pdf)
- add functionality ala nodemcu see http://www.nodemcu.com/docs/
- want nodemcu spiffs style filesystem on flash - depends on esp-open-rtos/issue
- hardware io functions: spi i2c
- put lisp functions in ROM at compile time, see below for inspiriation - in progress
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
- http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.109.6660&rep=rep1&type=pdf
 
## Flash based filesystems or flash database log systems

- http://research.microsoft.com/en-us/um/people/moscitho/Publications/USENIX_ATC_2015.pdf
  (normal filesystem that concentrate on checkpointing/logging, for staleness and power usage)
- http://www.yaffs.net/documents/how-yaffs-works
- https://en.wikipedia.org/wiki/Write_amplification
