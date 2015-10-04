/* 2015-09-22 (C) Jonas S Karlsson, jsk@yesco.org */
/* Distributed under Mozilla Public Licence 2.0   */
/* https://www.mozilla.org/en-US/MPL/2.0/         */
/* "driver" for esp-open-rtos put in examples/lisp */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "espressif/esp_common.h"
#include "espressif/sdk_private.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "ssid_config.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "lisp.h"

void lispTask(void *pvParameters)
{
    lisp env = lisp_init();
    lisp_run(&env);
    return;

    // TODO: move into a mem info and profile function!

    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    printf("Hello from lispTask!\r\n");
    uint32_t count = 0;
    while(1) {
        //vTaskDelay(300); // 3s

        lisp_run(&env);

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
            //putchar('.');
        } else {
            //printf("No msg :(\n");
        }
    }
}

static xQueueHandle mainqueue;

unsigned int lastTick = 0;
int lastMem = 0;

void print_memory_info(int verbose) {
    report_allocs(verbose);

    int tick = xTaskGetTickCount();
    int ms = (tick - lastTick) / portTICK_RATE_MS;
    int mem = xPortGetFreeHeapSize();
    if (verbose)
        printf("=== free=%u USED=%u bytes TIME=%d ms ===\n", mem, lastMem-mem, ms);
    else {
        if (mem) printf("free=%u ", mem);
        if (lastMem-mem) printf("USED=%u bytes ", lastMem-mem);
        if (ms) printf("TIME=%d ms ", ms);
    }
    lastTick = tick;
    lastMem = mem;
}

#define max(a,b) \
    ({ __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b; })

// can call with NULLs get the default config
void connect_wifi(char* ssid, char* password) {
    ssid = ssid ? ssid : WIFI_SSID;
    password = password ? password : WIFI_PASS;
        
    struct sdk_station_config config;
    memset(config.ssid, 0, sizeof(config.ssid));
    memset(config.password, 0, sizeof(config.password));
    memcpy(config.ssid, ssid, max(strlen(ssid), sizeof(config.ssid)));
    memcpy(config.password, password, sizeof(config.password));

    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
}

// want callback for tasks
// However, how to handle multiple gets at same time?
// TODO: keep as task as maybe it's blocking? 
//void http_get_task(void *pvParameters) {
//  vTaskDelay(1000 / portTICK_RATE_MS);

void user_init(void) {
    lastTick = xTaskGetTickCount();
    lastMem = xPortGetFreeHeapSize();

    sdk_uart_div_modify(0, UART_CLK_FREQ / 115200);

    mainqueue = xQueueCreate(10, sizeof(uint32_t));

    // for now run in a task, in order to allocate a bigger stack
    xTaskCreate(lispTask, (signed char *)"lispTask", 2048, &mainqueue, 2, NULL);
    // xTaskCreate(recvTask, (signed char *)"recvTask", 256, &mainqueue, 2, NULL);
}
