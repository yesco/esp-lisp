
The code is explained by an example of its use, and then the underlying code is gone through.

NOTE the code below has been tested for buttons/pins 0, 2, and 4. Although the code is designed to support 16 pins (whether or not this is possible for the ESP8266), rarely if ever have other pins been tested. No C code needs to change to support all pins. However the lisp flags, referred to below, only exist for pins 0, 2, and 4 (in part to keep the various defines to a manageable size). It should be clear how to add these lisp vars if they are required.

### NOTE 
I have not studied, nor tested for, any functionality to disable interrupts that have been enabled.

## EXAMPLE USE 

### Lisp Variables
There are two types of lisp vars (the relevant defines are in init_library()). Both types can be reset from the lisp env.

  (xx below stands for 00, 01 etc, representing gpio pins)

*iexx*  - flags that signify interrupt activity
*bcxx* - count of clicks 

Both vars are automatically set by the underlying code when there is a pin/button event. The *iexx* flags remain set until some code in the lisp env resets them. The *bcxx* vars are incremented with each event. If a *bcxx* var is reset in the lisp env, the count begins again from zero with the next event.

## SET UP INTERRUPT FUNCTIONALITY using init_library() defines

In init_library() there is a mix of DEFINEs (ready to be executed in the lisp env) and commented out “defines” (to be pasted into the lisp env and run).

1)	start the lisp env and execute (setupIntGroup)

this can be done at any point. The function both defines the *iexx* and *bcxx* vars for the lisp env and calls the PRIM interruptGroup that initiates interrupt functionality for pins 0, 2 and 4.

### NOTES
Use (setupInterrupt) instead, modified depending on pin choice, to enable pins at different times. This function calls PRIM interrupt() to enable interrupts for pin 4. Later calls to PRIM interrupt will enable other pins as required.

I’ve tried both “define” and setbang here, and both seem to result in an initial var state of nil rather than zero. Define is used as this seems more correct. The first commented out define resolves this behaviour.

The existence of DEFINE setInterrupt in init_library seems to cause the *iexx* and *bcxx* vars to be show in the globals list, even before it is called.

2)	use define ((list (set! *bc00* 0) … ) to set initial value of vars to zero. Otherwise they start at nil, which doesn't affect the code but may throw off lisp eq checks for zero, etc.

NOTE I use “list” instead of progn, a personal style thing (not something to which I’m not heavily attached).

3)	use define ((list (define ies …) to setup helper fns (non-essential, saves typing)

(ies) gives the state of *iexx* and (bcs) the state of *bcxx* 
(ie n) state of pin n and (clks n) click count of pin n

4) use (define ic … ) and (define rc … ). These non-essential functions provide wrappers around intChange and resetClicks primitives, both to save typing and to enable other functions that use the primitives to be shorter.

5) Everything is now ready to use. Lisp functions can now check *iexx* and *bcxx* for interrupt events. I have such functions running on *at* lambdas. Lisp functions can use “ic” and “rc” to reset flags and button counts respectively.

Some basic example code is included directly after the defines above.

(define testInt …) 
this checks the *iexx* flag, prints out the click count, then resets the flag

(at –10000 …)
calls the fn above every 10 secs

## IMPLEMENTATION NOTES

More details on the code in different files are included further below.

### Lisp PRIMs (in lisp.c)

Four PRIM funcs enable interrupt capability to be started and managed from lisp env. 

1) Two primitives cause interrupts to be enabled using interrupt_init(). 

PRIM interrupt 
PRIM interruptGroup 

interrupt_init() (in interrupt.c) receives an array as a parameter. The 16 array elements relate to the 16 pins (no sanity checks are made on pin choices). If an element is set to 1, the corresponding pin has interrupts enabled.

Interrupt() sets the array element to 1 for the pin number passed as a param. InterruptGroup() sets array elements for pins 0,2,4 (mainly because those are the ones tested by me). Naturally, the primitive could be changed to set other elements, or to accept a list parameter and to set those.

Interrupt() can be called multiple times to set interrupts for different pins at different times.

NOTE both primitives accept a change_type parameter (e.g. 3 is any edge change). Currently this is passed to interrupt_init() but the default of falling edge is never changed.
 

2) Two prims (for resetting flags and click count)

PRIM resetButtonClickCount (exposed as resetClicks)
	Resets both lisp (*bcxx*)  and C vars

PRIM intChange(lisp* envp, lisp pin, lisp v) 
	Changes lisp var (*iexx*) only. Sets var to v. Mostly used to set flag to not set (i.e. zero), but 1 or even any non-zero value might be appropriate at times. 


### FILE CHANGES

New file - interrupt.c
This file contains code that deals with RTOS (gpio_* funcs, interrupts and queues)

NOTE interrupt handlers (following RTOS sdk button.c example) do the minimum code possible, using the relevant sdk calls to write information to a queue. They neither read or write to either C or lisp vars.

Minor changes - compat.h includes, tlisp.ccc dummies

lisp.c

Various functions have been added. idle() calls handleButtonEvents(). This checks the RTOS queue to find any communication from the interrupt handlers. This should be the only RTOS code this PR has added to lisp.c. Information from the queue is used to set C vars.

handleButtonEvents() then calls updateButtonEnvVars() to update lisp vars as appropriate. Some new functions exist to abstract out code that is shared by the main functions.


## MISC

Dummy functions and variables enable “./run” to work correctly.

Some fairly arbitrary defaults :

1) The debounce value of 200 in checkInterruptQueue(). 
2) The created queue has a length of 2. My reading of the RTOS docs is that is when a queue is full, any new queue items overwrite existing items. So an interrupt handler should never be waiting on a full queue (an important consideration). idle() seems to run frequently enough that missed button clicks were not noticeable. For some applications though, this might be something to change.

No new RTOS tasks are created. The interrupt functions communicate with the existing lisp task using a queue. Only the lisp task has access to any of the C or lisp vars.

To some degree, with only the interrupts and a single lisp task existing, it may be an overcomplication to have both the C vars and lisp vars. However, this does create a clear divide between any RTOS-related code and the lisp code, which seems worthwhile to me. Further, the C and lisp vars are not a complete duplication. When the interrupt event C flag is set, the code resets this flag automatically after setting the lisp vars. If it did not, the click count would stop being incremented. The lisp interrupt event flag is only reset when lisp some code actively does this.

