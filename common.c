#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>

#ifndef UNIX
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
#endif

#ifdef UNIX
  #include <sys/socket.h>
#endif

#include "compat.h"

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
