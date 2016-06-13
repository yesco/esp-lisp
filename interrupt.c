#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "espressif/esp_common.h"
#include "espressif/sdk_private.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

  // for interrupt handling
  //#include "esp8266.h"

#include "ssid_config.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <esp/uart.h>

// needed for interrupt code in rtos
#include <stdint.h>

#include "lisp.h"

#include "compat.h"

//typedef enum {
//	GPIO_INTTYPE_NONE       = 0,
//	GPIO_INTTYPE_EDGE_POS   = 1,
//	GPIO_INTTYPE_EDGE_NEG   = 2,
//	GPIO_INTTYPE_EDGE_ANY   = 3,
//	GPIO_INTTYPE_LEVEL_LOW  = 4,
//	GPIO_INTTYPE_LEVEL_HIGH = 5,
//} gpio_inttype_t;

// constant copied from RTOS button interrupt example -
// could include esp/gpio_regs.h instead
//const gpio_inttype_t int_type = GPIO_INTTYPE_EDGE_NEG; // GPIO_INTTYPE_LEVEL_LOW; // GPIO_INTTYPE_EDGE_NEG;
//const gpio_inttype_t int_type = GPIO_INTTYPE_LEVEL_LOW;
const gpio_inttype_t int_type = GPIO_INTTYPE_LEVEL_LOW;


// copied from projdefs.h
typedef void (*pdTASK_CODE)( void * );

const int gpioPinCount = 16;

#define GPIO_HANDLER_00 gpio00_interrupt_handler
#define GPIO_HANDLER_01 gpio01_interrupt_handler
#define GPIO_HANDLER_02 gpio02_interrupt_handler
#define GPIO_HANDLER_03 gpio03_interrupt_handler
#define GPIO_HANDLER_04 gpio04_interrupt_handler
#define GPIO_HANDLER_05 gpio05_interrupt_handler
#define GPIO_HANDLER_06 gpio06_interrupt_handler
#define GPIO_HANDLER_07 gpio07_interrupt_handler
#define GPIO_HANDLER_08 gpio08_interrupt_handler
#define GPIO_HANDLER_09 gpio09_interrupt_handler
#define GPIO_HANDLER_10 gpio10_interrupt_handler
#define GPIO_HANDLER_11 gpio11_interrupt_handler
#define GPIO_HANDLER_12 gpio12_interrupt_handler
#define GPIO_HANDLER_13 gpio13_interrupt_handler
#define GPIO_HANDLER_14 gpio14_interrupt_handler
#define GPIO_HANDLER_15 gpio15_interrupt_handler

static xQueueHandle tsqueue = NULL;

// flags for count change, reset when lisp env var is updated
int button_clicked[16] = {0}; // TODO: use bitmask, or keep old count value last "seen"
int button_last[16] = {0};
int button_count[16] = {0};

struct ButtonMessage {           
        uint32_t now;            
        uint32_t buttonNumber;   
};

void checkInterruptQueue()
{
	if (tsqueue == NULL) return;

	struct ButtonMessage msg;
	if (xQueueReceive(tsqueue, &msg, 0)) {
		uint32_t ms = msg.now * portTICK_RATE_MS;
		int pin = msg.buttonNumber;

		// debounce check (from button.c example code)
		// TODO: need a last per button!
		// TODO: generalize, add button abstraction on top!
		if (button_last[pin] < ms - 200) {
			printf(" [button %d pressed at %dms\r\n", pin, ms);
			button_clicked[pin] = 1;
			button_last[pin] = ms;
			button_count[pin]++;
		}
	 }
}

void interrupt_init(int pin, int changeType)
{
	gpio_enable(pin, GPIO_INPUT);
	gpio_set_interrupt(pin, changeType);

	if (tsqueue == NULL ) {
		// queue size of 2 items is arbitrary, but has been adequate so far
		tsqueue = xQueueCreate(2, sizeof(struct ButtonMessage));
	}
}

void gpio_int_handler(int buttonNumber)
{
	struct ButtonMessage btnMsg;

	btnMsg.now = xTaskGetTickCountFromISR();
	btnMsg.buttonNumber = buttonNumber;

	printf(" [interrupt %d] ", buttonNumber); fflush(stdout);
	if (pdPASS != xQueueSendToBackFromISR(tsqueue, &btnMsg, NULL)) {
		// TODO: fill the queue easy by...
		// GPIO_INTTYPE_LEVEL_LOW  = 4 or GPIO_INTTYPE_LEVEL_HIGH = 5,    
		printf("\n\n%%gpio_int_handler.ERROR: queue is FULL! interrupt ignored!\n\n");
	}
}

// could refactor these 16 functions as instances of a macro
void GPIO_HANDLER_00(void)
{
	return gpio_int_handler(0);
}

void GPIO_HANDLER_01(void)
{
	return gpio_int_handler(1);
}

void GPIO_HANDLER_02(void)
{
	return gpio_int_handler(2);
}

void GPIO_HANDLER_03(void)
{
	return gpio_int_handler(3);
}

void GPIO_HANDLER_04(void)
{
	return gpio_int_handler(4);
}

void GPIO_HANDLER_05(void)
{
	return gpio_int_handler(5);
}

void GPIO_HANDLER_06(void)
{
	return gpio_int_handler(6);
}

void GPIO_HANDLER_07(void)
{
	return gpio_int_handler(7);
}

void GPIO_HANDLER_08(void)
{
	return gpio_int_handler(8);
}

void GPIO_HANDLER_09(void)
{
	return gpio_int_handler(9);
}

void GPIO_HANDLER_10(void)
{
	return gpio_int_handler(10);
}

void GPIO_HANDLER_11(void)
{
	return gpio_int_handler(11);
}

void GPIO_HANDLER_12(void)
{
	return gpio_int_handler(12);
}

void GPIO_HANDLER_13(void)
{
	return gpio_int_handler(13);
}

void GPIO_HANDLER_14(void)
{
	return gpio_int_handler(14);
}

void GPIO_HANDLER_15(void)
{
	return gpio_int_handler(15);
}
