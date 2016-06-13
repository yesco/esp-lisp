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

// call cb for each pin that gotten interrupts since last time
void checkInterrupts(void* (*cb)(uint32_t pin, uint32_t count, uint32_t last)) {
	int pin;
	for (pin = 0; pin < GPIO_PINS; pin++) {
		if (button_clicked[pin]) {
			// TODO: check race conditions?
			button_clicked[pin] = 0;
			cb(button_clicked[pin], button_count[pin], button_last[pin]);
		}
	}
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
				button_clicked[pin] = 1;
				button_last[pin] = ms;
				button_count[pin]++;
			} else {
				printf(" [BOUNCE! %d at %dms]\r\n", pin, ms);
			}
		}
	}
}
