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

// simplistic XML parser:
//   xml_out -> xml_char -> xml_tag/xml_tag_name/xml_attr_value
//
// These are called to emit result:
// - xml_emit_text(char c) // for each textual/content char
// - xml_emit_tag(char* tag) // for each tag as it's enters it, finalizes it: TAG /TAG or TAG/
// - xml_emit_attr(char* tag, char* attr, char* value) // for each <TAG ATTR="VALUE"

// limits
#define MAX_BUFF 128 // efficency
#define TAG_SIZE 32 // tag name size, attr name size
#define VALUE_SIZE 128 // value size

typedef struct {
    int state;

    char tag[TAG_SIZE + 1];
    int tag_pos;

    char attr[TAG_SIZE + 1];
    int attr_pos;

    char* path[TAG_SIZE + 1];
    int path_pos;

    char value[VALUE_SIZE + 1];
    int value_pos;

    void (*xml_emit_text)(char* path[], char c);
    void (*xml_emit_tag)(char* path[], char* tag);
    void (*xml_emit_attr)(char* path[], char* tag, char* attr, char* value);
} wget_data;

// TODO: abstract this?

void f_xml_emit_text(char* path[], char c) {
    //printf("%c", c);
}

void f_xml_emit_tag(char* path[], char* tag) {
    printf("TAG=%s\n", tag);
}

void f_xml_emit_attr(char* path[], char* tag, char* attr, char* value) {
    printf("TAG=%s ATTR=%s VALUE=%s\n", tag, attr, value);
}

int http_get(char* url, char* server) {
    wget_data data;
    memset(&data, 0, sizeof(data));
    data.xml_emit_text = f_xml_emit_text;
    data.xml_emit_tag = f_xml_emit_tag;
    data.xml_emit_attr = f_xml_emit_attr;

    wget(&data, url, server);
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
        data->xml_emit_tag(data->path, data->tag);
        data->tag[data->tag_pos--] = 0; // remove '/'
    } else if (data->tag[0] == '/') { // </TAG>
        data->xml_emit_tag(data->path, data->tag);
        //printf("STRCMP(%s, %s)\n", xml_path[xml_pos-1], &current_tag[1]);
        if (strcmp(data->path[data->path_pos - 1], &(data->tag[1])) == 0) {
            data->path[data->path_pos] = NULL;
            data->path_pos--;
        } else {
            // mismatch
            // TODO:????
        }
    } else { // <TAG>
        data->xml_emit_tag(data->path, data->tag);
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
        else data->xml_emit_text(data->path, c);

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
            data->xml_emit_attr(data->path, data->tag, data->attr, data->value);

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
        printf("DNS lookup failed err=%d res=%p\r\n", err, res);
        if (res)
            freeaddrinfo(res);
        failures++;
        return 1;
    }

    /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    //struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    //printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

    int s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
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

