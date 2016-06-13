#include "FreeRTOS.h"
#include "lwip/sys.h"

//typedef enum {
//	GPIO_INTTYPE_NONE       = 0,
//	GPIO_INTTYPE_EDGE_POS   = 1,
//	GPIO_INTTYPE_EDGE_NEG   = 2,
//	GPIO_INTTYPE_EDGE_ANY   = 3,
//	GPIO_INTTYPE_LEVEL_LOW  = 4,
//	GPIO_INTTYPE_LEVEL_HIGH = 5,
//} gpio_inttype_t;
const gpio_inttype_t int_type = GPIO_INTTYPE_LEVEL_LOW;

#define GPIO_PINS 16

// flags for count change, reset when lisp env var is updated
int button_clicked[GPIO_PINS] = {0}; // TODO: use bitmask, or keep old count value last "seen"
int button_last[GPIO_PINS] = {0};
int button_count[GPIO_PINS] = {0};

void checkInterrupts(int (*cb)(int pin, uint32_t clicked, uint32_t count, uint32_t last)) {
	int pin;
	for (pin = 0; pin < GPIO_PINS; pin++) {
		if (button_clicked[pin]) {
			int mode = cb(pin, button_clicked[pin], button_count[pin], button_last[pin]);
			// if no handler, then don't clear
			if (mode != -666) button_clicked[pin] = 0;
		}
	}
}

// if clear ==  0 COUNT: just return current click count
// if clear == -1 STATUS: if clicked since last clear, +clicks, otherwise negative: -clicks
// if clear == -2 DELTA: return +clicks since last call with clear == -2
// if clear == -3 MS: return last ms time when clicked
// don't mix clear == -1 and -2 calls to same pin
uint32_t getCount(int pin, int mode) {
	if (pin < 0 || pin >= GPIO_PINS) return -1;
	uint32_t r = button_count[pin];
	if (mode == -1) {
		if (!button_clicked[pin]) r = -r;
		button_clicked[pin] = 0;
	} else if (mode == -2) {
		r = button_clicked[pin];
		button_clicked[pin] = 0;
	} else if (mode == -3) {
		r = button_last[pin];
	}
	return r;
}

void interrupt_init(int pin, int changeType)
{
	gpio_enable(pin, GPIO_INPUT);
	gpio_set_interrupt(pin, changeType);
}

void gpio_interrupt_handler() {
	uint32_t status_reg = GPIO.STATUS;
	GPIO.STATUS_CLEAR = status_reg;
	uint8_t pin;
	while ((pin = __builtin_ffs(status_reg))) {
		pin--;
		status_reg &= ~BIT(pin);
		if (FIELD2VAL(GPIO_CONF_INTTYPE, GPIO.CONF[pin])) {
			uint32_t ms = xTaskGetTickCountFromISR() * portTICK_RATE_MS;
			// debounce check (from button.c example code)
			// TODO: need a last per button!
			// TODO: generalize, add button abstraction on top!
			printf(" [interrupt %d] ", pin); fflush(stdout);
			if (button_last[pin] < ms - 200) {
				printf(" [button %d pressed at %dms\r\n", pin, ms);
				button_clicked[pin]++;
				button_last[pin] = ms;
				button_count[pin]++;
			} else {
				printf(" [BOUNCE! %d at %dms]\r\n", pin, ms);
			}
		}
	}
}
