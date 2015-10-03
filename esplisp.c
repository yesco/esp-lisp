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
    lisp env = lispinit();
    lisprun(&env);
    return;

    // TODO: move into a mem info and profile function!

    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    printf("Hello from lispTask!\r\n");
    uint32_t count = 0;
    while(1) {
        //vTaskDelay(300); // 3s

        unsigned int mem = xPortGetFreeHeapSize();
        printf("free=%u\r\n", mem);
        int start = xTaskGetTickCount();

        lisprun(&env);

        int tm = (xTaskGetTickCount() - start) * portTICK_RATE_MS;
        printf("free=%u USED=%u TIME=%d\r\n", xPortGetFreeHeapSize(), (unsigned int)(mem-xPortGetFreeHeapSize()), tm);
        printf("======================================================================\n");
        reportAllocs();

        start = xTaskGetTickCount();
        int i, s = 0;
        for(i=0; i<1000000; i++) { s = s + 1; }
        tm = (xTaskGetTickCount() - start) * portTICK_RATE_MS;

        printf("10,000,000 LOOP (100x lua) TIME=%d\r\n", tm);
        printf("======================================================================\n");

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
int http_get(char* buff, int size, char* url, char* server) {
    int successes = 0, failures = 0;

    printf("HTTP get task starting...\r\n");

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    printf("Running DNS lookup for %s...\r\n", url);
    int err = getaddrinfo(server, "80", &hints, &res);

    if (err != 0 || res == NULL) {
        printf("DNS lookup failed err=%d res=%p\r\n", err, res);
        if (res)
            freeaddrinfo(res);
        failures++;
        return 1;
    }

    /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

    int s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        printf("... Failed to allocate socket.\r\n");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_RATE_MS);
        failures++;
        return 2;
    }

    printf("... allocated socket\r\n");

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        close(s);
        freeaddrinfo(res);
        printf("... socket connect failed.\r\n");
        failures++;
        return 3;
    }

    printf("... connected\r\n");
    freeaddrinfo(res);

    // TODO: not efficient?
    #define WRITE(msg) (write((s), (msg), strlen(msg)) < 0)
    if (WRITE("GET ") ||
        WRITE(url) ||
        WRITE("\r\n") ||
        WRITE("User-Agent: esp-open-rtos/0.1 esp8266\r\n\r\n"))
    #undef WRITE
    {
        printf("... socket send failed\r\n");
        close(s);
        failures++;
        return 4;
    }
    printf("... socket send success\r\n");

    int r;
    char* p = buff;
    size--; // remove one for \0
    do {
        bzero(buff, size);
        r = read(s, p, size);
        if (r > 0) {
            p += r;
            size -= r;
            // printf("%s", recv_buf);
        }
    } while (r > 0);

    printf("... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
    if (r != 0)
        failures++;
    else
        successes++;
    close(s);
    return 0;
}
//  vTaskDelay(1000 / portTICK_RATE_MS);

void user_init(void) {
    sdk_uart_div_modify(0, UART_CLK_FREQ / 115200);

    mainqueue = xQueueCreate(10, sizeof(uint32_t));

    // for now run in a task, in order to allocate a bigger stack
    xTaskCreate(lispTask, (signed char *)"lispTask", 2048, &mainqueue, 2, NULL);
    // xTaskCreate(recvTask, (signed char *)"recvTask", 256, &mainqueue, 2, NULL);
}
