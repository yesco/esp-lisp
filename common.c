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

//
#define TAG_NORMAL 0
#define TAG_START 1
#define TAG_ATTR 2

// TODO: abstract this?
static char tag[TAG_SIZE + 1] = {0};
static int tag_pos = 0;

static char attr[TAG_SIZE + 1] = {0};
static int attr_pos = 0;

static char* path[TAG_SIZE + 1] = {0};
static int path_pos = 0;

static char value[VALUE_SIZE + 1] = {0};
static int value_pos = 0;

void xml_emit_text(char* path[], char c) {
    //printf("%c", c);
}

void xml_emit_tag(char* path[], char* tag) {
    printf("TAG=%s\n", tag);
}

void xml_emit_attr(char* path[], char* tag, char* attr, char* value) {
    printf("TAG=%s ATTR=%s VALUE=%s\n", tag, attr, value);
}

void xml_tag(char c) {
    tag[tag_pos++] = c;
    if (tag_pos > TAG_SIZE) tag_pos--; // overwrite
    tag[tag_pos] = 0;
}

void xml_tag_name(char c) {
    // print path
    if (0) {
        int i = 0;
        for(i = 0; i < path_pos; i++) printf("%s -> ", path[i]);
        printf("\n");
    }

    if (c == '/' || tag[tag_pos-1] == '/') {
        xml_emit_tag(path, tag);
        tag[tag_pos--] = 0; // remove '/'
    } else if (tag[0] == '/') { // </TAG>
        xml_emit_tag(path, tag);
        //printf("STRCMP(%s, %s)\n", xml_path[xml_pos-1], &current_tag[1]);
        if (strcmp(path[path_pos - 1], &tag[1]) == 0) {
            path[path_pos] = NULL;
            path_pos--;
        } else {
            // mismatch
            // TODO:????
        }
    } else { // <TAG>
        xml_emit_tag(path, tag);
        path[path_pos++] = strdup(tag);
        if (path_pos > TAG_SIZE-1) path_pos--;
    }

    tag[0] = 0;
    tag_pos = 0;
}

void xml_attr(char c) {
    attr[attr_pos++] = c;
    if (attr_pos > TAG_SIZE) attr_pos--;
    attr[attr_pos] = 0;
}

void xml_attr_value(char c) {
    value[value_pos++] = c;
    if (value_pos > VALUE_SIZE) value_pos--;
    value[value_pos] = 0;
}

void xml_char(int c) {
    static int state = TAG_NORMAL;

    // reset at beginning and end
    if (c < 0) {
        state = TAG_NORMAL;
        return;
    }

    switch (state) {
    case TAG_NORMAL: 
        if (c == '<') state = TAG_START;
        else xml_emit_text(path, c);

        break;
    case TAG_START:
        if (c == '>') state = TAG_NORMAL;
        else if (c == ' ' || c == '\n' || c == '\r') state = TAG_ATTR;
        else xml_tag(c);

        if (state == TAG_NORMAL) xml_tag_name(c);
        break;
    case TAG_ATTR:
        if (c == '>') state = TAG_NORMAL;
        else if (c == ' ' || c == '\n' || c == '\r') state = TAG_ATTR;
        else if (c == '/') { xml_tag(c); xml_tag_name(c); state = TAG_NORMAL; }
        else if (c == '"' || c == '\'') state = c;
        else if (c == '=') ; // ignore // TODO: fix
        else xml_attr(c);

        if (state == TAG_NORMAL) xml_tag_name(c);
        break;
    case '"': case '\'':
        if (c == state) state = TAG_ATTR;
        // TODO: \' \" ???
        else xml_attr_value(c);

        if (state == TAG_ATTR) {
            xml_emit_attr(path, tag, attr, value);

            attr_pos = 0;
            attr[0] = 0;

            value_pos = 0;
            value[0] = 0;
        }

        break;
    }
}

int xml_out(char* buff, int bytes) {
    int i = 0;
    for (i = 0; i < bytes; i++) xml_char(buff[i]);
    
    return 1; // continue
}

int http_get(char* url, char* server) {
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
    xml_char(-1);
    do {
        r = read(s, buff, sizeof(buff));
        if (r > 0 && !xml_out(buff, r)) break;
    } while (r > 0);

    // mark end
    xml_char(-2);

    //printf("... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
    if (r != 0)
        failures++;
    else
        successes++;

    close(s);

    return 0;
}

