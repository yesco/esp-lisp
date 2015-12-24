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

#include <esp/uart.h>

#include "lisp.h"

int startTask, afterInit;

#include "compat.h"

void lispTask(void *pvParameters)
{
    startTask = xPortGetFreeHeapSize();

    lisp env = lisp_init();

    afterInit = xPortGetFreeHeapSize();

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


unsigned int lastTick = 0;
int lastMem = 0;
int startMem = 0;

void print_memory_info(int verbose) {
    report_allocs(verbose);

    int tick = xTaskGetTickCount();
    int ms = (tick - lastTick) / portTICK_RATE_MS;
    int mem = xPortGetFreeHeapSize();
    if (verbose == 2)
        printf("=== free=%u USED=%u bytes TIME=%d ms, startMem=%u ===\n", mem, lastMem-mem, ms, startMem);
    else if (verbose == 1) {
        if (mem) printf("free=%u ", mem);
        if (lastMem-mem) printf("USED=%u bytes ", lastMem-mem);
        if (ms) printf("TIME=%d ms ", ms);
        // http://www.freertos.org/uxTaskGetStackHighWaterMark.html
        //   uxTaskGetStackHighWaterMark( NULL );
        // The value returned is the high water mark in words (for example,
        // on a 32 bit machine a return value of 1 would indicate that
        // 4 bytes of stack were unused)
        printf("stackUsed=%lu ", uxTaskGetStackHighWaterMark(NULL));
        if (startMem) printf("startMem=%u ", startMem);
        if (startTask) printf("startTask=%u ", startTask);
        if (afterInit) printf("afterInit=%u ", afterInit);
        printf("\n");
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

    // TODO: wifi_get_ip_info
    // https://github.com/SuperHouse/esp-open-rtos/blob/master/lib/allsymbols.rename
    // wifi_get_sleep_type, wifi_station_scan
    // wifi_softap_stop, wifi_station_disconnect
}

// want callback for tasks
// However, how to handle multiple gets at same time?
// TODO: keep as task as maybe it's blocking? 
//void http_get_task(void *pvParameters) {
//  vTaskDelay(1000 / portTICK_RATE_MS);

// fcntl doesn't seem to work correctly on EPS8266 files only sockets...

int nonblock_getch() {
    return uart_getc_nowait(0);
}

int clock_ms() {
    // return xTaskGetTickCount() / portTICK_RATE_MS;
    return xTaskGetTickCount() * 10;
}

// //#define configMINIMAL_STACK_SIZE	( ( unsigned short )256 )
// #define configMINIMAL_STACK_SIZE	( ( unsigned short )2048 )
// === TOTAL: 5192
//
// used_count=72 cons_count=354 free=21468 USED=12 bytes startMem=27412 startTask=27276 startTask=21520 
//
// lisp> (- 27276 21520)
//    5756
//
// lisp> (setq a (lambda (n) (princ n) (terpri) (+ 1 (a (+ 1 n
// ... 37 crash

// #define configMINIMAL_STACK_SIZE	( ( unsigned short )256 )
// === TOTAL: 5192
//
// used_count=72 cons_count=354 free=28636 USED=12 bytes startMem=34580 startTask=34444 startTask=28688 
//
// lisp> (- 34580 28688)
//    5892
//
// lisp> (setq a (lambda (n) (princ n) (terpri) (+ 1 (a (+ 1 n
// ... 37 crash
//
// (/ (- 28688 21520) (- 2048 256))

// //#define configTOTAL_HEAP_SIZE		( ( size_t ) ( 32 * 1024 ) )
// #define configTOTAL_HEAP_SIZE		( ( size_t ) ( 64 * 1024 ) )
//
// used_count=72 cons_count=354 free=28636 USED=12 bytes startMem=34580 startTask=34444 startTask=28688 

// // #define configMINIMAL_STACK_SIZE	( ( unsigned short )256 )
// #define configMINIMAL_STACK_SIZE	( ( unsigned short )128 )
// === TOTAL: 5192
// used_count=72 cons_count=355 free=29148 USED=12 bytes startMem=35092 startTask=34956 startTask=29200 

// #define configMINIMAL_STACK_SIZE	( ( unsigned short )0 )
// used_count=72 cons_count=354 free=29656 USED=12 bytes startMem=35600 startTask=35464 startTask=29708
// a recurse only 7 levels, (fib 40) no problem...
//
// used_count=72 cons_count=355 free=31700 USED=12 bytes startMem=37644 startTask=37508 startTask=31752
//
// used_count=72 cons_count=355 free=31716 USED=12 bytes startMem=37660 startTask=37524 startTask=31768

// use a single task 2048 space
// used_count=72 cons_count=354 free=22372 USED=16 bytes startMem=37628 startTask=28216 startTask=22460

// removed mainqueue
// used_count=72 cons_count=355 free=22516 USED=16 bytes startMem=37636 startTask=28360 startTask=22604 

// (- 31768 22460)

// stack = 2048
// used_count=73 cons_count=333 free=21896 USED=16 bytes startMem=37636 startTask=28360 startTask=22604 

// stack = 1024
// used_count=72 cons_count=354 free=26612 USED=16 bytes startMem=37636 startTask=32456 startTask=26700 

// stack = 512
// used_count=72 cons_count=354 free=28660 USED=16 bytes startMem=37636 startTask=34504 startTask=28748
// a recurse -> 52 deep

//(- 31768 28748) = 3020 cost of a task of 512 bytes
//(- 31768 22604) = 9164 cost of a task with 2048 bytes
//
//(- 37636 31768) = 5668
//(- 37636 34504) = 3132 for  512
//(- 37636 32456) = 5180 for 1024 + (- 5180 3132) = 2048
//(- 37636 28360) = 9276 for 2048 + (- 9276 5180) = 4096

// stack = 0
// crash!

// esp-open-rtos/FreeRTOS/Source/include/FreeRTOSConfig.h
//
// issue:
//   https://github.com/SuperHouse/esp-open-rtos/issues/75

void user_init(void) {
    lastTick = xTaskGetTickCount();
    startMem = lastMem = xPortGetFreeHeapSize();

    sdk_uart_div_modify(0, UART_CLK_FREQ / 115200);
    
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    // this doesn't have enough stack!
    //lispTask(NULL); return;

    // for now run in a task, in order to allocate a bigger stack
    // 1024 --> (fibo 13)
    // 2048 --> (fibo 30) ???
    xTaskCreate(lispTask, (signed char *)"lispTask", 2048, NULL, 2, NULL);
}

int process_file(char* filename, process_input process) {
    error("process_file: not implemented!");
    return -1;
}

void exit(int e) {
    printf("\n\n=============== EXIT=%d ===============\n", e);
    while(1);
}
