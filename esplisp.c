/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* "driver" for esp-open-rtos put in examples/lisp */

#include "espressif/esp_common.h"
#include "espressif/sdk_private.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>

#include "lisp.h"

void lispTask(void *pvParameters)
{
    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    printf("Hello from lispTask!\r\n");
    uint32_t count = 0;
    while(1) {
        //vTaskDelay(300); // 3s

        unsigned int mem = xPortGetFreeHeapSize();
        printf("free=%u\r\n", mem);
        lisptest();
        printf("free=%u USED=%u\r\n", xPortGetFreeHeapSize(), (unsigned int)(mem-xPortGetFreeHeapSize()));

        xQueueSend(*queue, &count, 0);
        count++;
    }
}

void recvTask(void *pvParameters)
{
    printf("Hello from recvTask!\r\n");
    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    while(1) {
        uint32_t count;
        if(xQueueReceive(*queue, &count, 1000)) {
            //printf("Got %u\n", count);
            putchar('.');
        } else {
            printf("No msg :(\n");
        }
    }
}

static xQueueHandle mainqueue;

void user_init(void)
{
    sdk_uart_div_modify(0, UART_CLK_FREQ / 115200);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());
    
    lispinit();

    mainqueue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(lispTask, (signed char *)"lispTask", 2048, &mainqueue, 2, NULL);
    xTaskCreate(recvTask, (signed char *)"recvTask", 256, &mainqueue, 2, NULL);
}
