#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>

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

void clear() { // vt100
     printf("[H[2J"); fflush(stdout);
}

char* readline_int(char* prompt, int maxlen, int (*myreadchar)(char*)) {
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    char buffer[maxlen+1];
    int i = 0;
    char ch;
    while (myreadchar(&ch) == 1) {
        if (ch == '\b' || ch == 0x7f) {
            if (i > 0) {
                i--;
                printf("\b \b"); fflush(stdout);
            }
            continue;
        }
        //printf("[%d]", ch); fflush(stdout);
        if (ch == 3) { // ctrl-c, restart
            i = 0;
            ch = 12; // force a redraw of prompt
        }
        if (ch == 12) { // ctrl-l, redraw
            buffer[i] = 0;
            printf("\n%s%s", prompt, &buffer[0]);
            fflush(stdout);
            continue;
        }
        if (ch == 4) { // ctrl-d eof only at beginning of line
            if (i)
                continue;
            else
                return NULL;
        }
        int eol = (ch == '\n' || ch == '\r');
        if (!eol) {
            putchar(ch); fflush(stdout);
            buffer[i++] = ch;
        }
        if (i == maxlen) printf("\nWarning, result truncated at maxlen=%d!\n", maxlen);
        if (i == maxlen || eol) {
            buffer[i] = 0;
            printf("\n");
            return strdup(buffer);
        }
    }
    return NULL;
}

// default blocking getchar
int readline_getchar(char * chp) {
    return read(0, (void*) chp, 1); // 0 is stdin
}

char* readline(char* prompt, int maxlen) {
    return readline_int(prompt, maxlen, &readline_getchar);
}

// simplistic XML parser:
//   xml_out -> xml_char -> xml_tag/xml_tag_name/xml_attr_value
//

void f_xml_emit_text(void* userdata, char* path[], char c) {
    //printf("%c", c);
}

void f_xml_emit_tag(void* userdata, char* path[], char* tag) {
    printf("TAG=%s\n", tag);
}

void f_xml_emit_attr(void* userdata, char* path[], char* tag, char* attr, char* value) {
    printf("TAG=%s ATTR=%s VALUE=%s\n", tag, attr, value);
}

int http_get(char* url, char* server) {
    wget_data data;
    memset(&data, 0, sizeof(data));
    data.xml_emit_text = f_xml_emit_text;
    data.xml_emit_tag = f_xml_emit_tag;
    data.xml_emit_attr = f_xml_emit_attr;

    wget(&data, url, server);
    return 1;
}

void xml_tag(wget_data* data, char c) {
    data->tag[data->tag_pos++] = c;
    if (data->tag_pos > TAG_SIZE) data->tag_pos--; // overwrite
    data->tag[data->tag_pos] = 0;
}

void xml_tag_name(wget_data* data, char c) {
    // print path
    if (0) {
        int i = 0;
        for(i = 0; i < data->path_pos; i++) printf("%s -> ", data->path[i]);
        printf("\n");
    }

    if (c == '/' || data->tag[data->tag_pos-1] == '/') {
        if (data->xml_emit_tag)
            data->xml_emit_tag(data->userdata, data->path, data->tag);
        data->tag[data->tag_pos--] = 0; // remove '/'
    } else if (data->tag[0] == '/') { // </TAG>
        if (data->xml_emit_tag)
            data->xml_emit_tag(data->userdata, data->path, data->tag);
        //printf("STRCMP(%s, %s)\n", xml_path[xml_pos-1], &current_tag[1]);
        if (strcmp(data->path[data->path_pos - 1], &(data->tag[1])) == 0) {
            data->path[data->path_pos] = NULL;
            data->path_pos--;
        } else {
            // mismatch
            // TODO:????
        }
    } else { // <TAG>
        if (data->xml_emit_tag)
            data->xml_emit_tag(data->userdata, data->path, data->tag);
        data->path[data->path_pos++] = strdup(data->tag);
        if (data->path_pos > TAG_SIZE-1) data->path_pos--;
    }

    data->tag[0] = 0;
    data->tag_pos = 0;
}

void xml_attr(wget_data* data, char c) {
    data->attr[data->attr_pos++] = c;
    if (data->attr_pos > TAG_SIZE) data->attr_pos--;
    data->attr[data->attr_pos] = 0;
}

void xml_attr_value(wget_data* data, char c) {
    data->value[data->value_pos++] = c;
    if (data->value_pos > VALUE_SIZE) data->value_pos--;
    data->value[data->value_pos] = 0;
}

// states
#define TAG_NORMAL 0
#define TAG_START 1
#define TAG_ATTR 2

void xml_char(wget_data* data, int c) {
    // reset at beginning and end
    if (c < 0) {
        data->state = TAG_NORMAL;
        return;
    }

    switch (data->state) {
    case TAG_NORMAL:
        if (c == '<') data->state = TAG_START;
        else if (data->xml_emit_text)
            data->xml_emit_text(data->userdata, data->path, c);

        break;
    case TAG_START:
        if (c == '>') data->state = TAG_NORMAL;
        else if (c == ' ' || c == '\n' || c == '\r') data->state = TAG_ATTR;
        else xml_tag(data, c);

        if (data->state == TAG_NORMAL) xml_tag_name(data, c);
        break;
    case TAG_ATTR:
        if (c == '>') data->state = TAG_NORMAL;
        else if (c == ' ' || c == '\n' || c == '\r') data->state = TAG_ATTR;
        else if (c == '/') { xml_tag(data, c); xml_tag_name(data, c); data->state = TAG_NORMAL; }
        else if (c == '"' || c == '\'') data->state = c;
        else if (c == '=') ; // ignore // TODO: fix
        else xml_attr(data, c);

        if (data->state == TAG_NORMAL) xml_tag_name(data, c);
        break;
    case '"': case '\'':
        if (c == data->state) data->state = TAG_ATTR;
        // TODO: \' \" ???
        else xml_attr_value(data, c);

        if (data->state == TAG_ATTR) {
            if (data->xml_emit_attr)
                data->xml_emit_attr(data->userdata, data->path, data->tag, data->attr, data->value);

            data->attr_pos = 0;
            data->attr[0] = 0;

            data->value_pos = 0;
            data->value[0] = 0;
        }

        break;
    }
}

int xml_out(wget_data* data, char* buff, int bytes) {
    int i = 0;
    for (i = 0; i < bytes; i++) xml_char(data, buff[i]);
    
    return 1; // continue
}

int wget(wget_data* data, char* url, char* server) {
    int successes = 0, failures = 0;

    //printf("HTTP get task starting...\r\n");

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    //printf("Running DNS lookup for %s...\r\n", url);
    int err = getaddrinfo(server, "80", &hints, &res);

    if (err != 0 || res == NULL) {
        printf("DNS lookup failed err=%d res=%p server=%s\r\n", err, res, server);
        if (res)
            freeaddrinfo(res);
        failures++;
        return 1;
    }

    /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    //struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    //printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        printf("... Failed to allocate socket.\r\n");
        freeaddrinfo(res);
        failures++;
        return 2;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        close(s);
        freeaddrinfo(res);
        printf("... socket connect failed.\r\n");
        failures++;
        return 3;
    }

    //printf("... connected\r\n");
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
    //printf("... socket send success\r\n");

    char buff[MAX_BUFF] = {0};

    int r = 0;
    xml_char(data, -1);
    do {
        r = read(s, buff, sizeof(buff));
        if (r > 0 && !xml_out(data, buff, r)) break;
    } while (r > 0);

    // mark end
    xml_char(data, -2);

    //printf("... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
    if (r != 0)
        failures++;
    else
        successes++;

    close(s);

    return 0;
}

