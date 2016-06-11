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

// constant copied from RTOS button interrupt example -
// could include esp/gpio_regs.h instead
const gpio_inttype_t int_type = GPIO_INTTYPE_EDGE_NEG; // GPIO_INTTYPE_LEVEL_LOW; // GPIO_INTTYPE_EDGE_NEG;
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
int buttonCountChanged[16] = {0};
int buttonClickCount  [16] = {0};

struct ButtonMessage {           
        uint32_t now;            
        uint32_t buttonNumber;   
};

static int checkCount = 0;
static uint32_t last = 0;

void checkInterruptQueue()
{
	uint32_t button_ts;

	struct ButtonMessage btnMsg;

	if (tsqueue == NULL) {
		return;
	}

	 if (xQueueReceive(tsqueue, &btnMsg, 0)) {
		 checkCount = 0;

		button_ts = btnMsg.now;
		button_ts *= portTICK_RATE_MS;

		// debounce check (from button.c example code)
		if(last < button_ts-200) {
			//printf("interrupt %d fired at %dms\r\n", btnMsg.buttonNumber, button_ts);
			last = button_ts;

			buttonCountChanged[btnMsg.buttonNumber] = 1;

			buttonClickCount[btnMsg.buttonNumber]   =
				buttonClickCount[btnMsg.buttonNumber] + 1;
		}
	 }
}

void interrupt_init(int pins[gpioPinCount], int changeType)
{
	// in effect, this is a no-op - the default cannot be changed,
    // but may expand on this code later
	if (changeType == 2) {
		// int_type = GPIO_INTTYPE_EDGE_NEG;
	}

	for (int pin = 0; pin < gpioPinCount; pin++) {
		if (pins[pin] != 0) {
			gpio_enable(pin, GPIO_INPUT);
			gpio_set_interrupt(pin, int_type);
		}
	}

    if (tsqueue == NULL ) {
    	// queue size of 2 items is arbitrary,
    	// but has been adequate so far
	    tsqueue = xQueueCreate(2, sizeof(struct ButtonMessage));
	}
}

void gpio_int_handler(int buttonNumber)
{
	uint32_t now = xTaskGetTickCountFromISR();

	struct ButtonMessage btnMsg;

	btnMsg.now 			= now;
	btnMsg.buttonNumber = buttonNumber;

	xQueueSendToBackFromISR(tsqueue, &btnMsg, NULL);
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
