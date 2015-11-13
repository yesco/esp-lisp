#ifndef COMPAT_H
#define COMPAT_H

//////////////////////////////////////////////////////////////////////
// IO

int nonblock_getch();

char* readline(char* prompt, int maxlen);
char* readline_int(char* prompt, int maxlen, int (*myreadchar)(char*));

//////////////////////////////////////////////////////////////////////
// xml

// limits
#define MAX_BUFF 128 // efficency
#define TAG_SIZE 32 // tag name size, attr name size
#define VALUE_SIZE 128 // value size

// These are called to emit result:
// - xml_emit_text(char c) // for each textual/content char
// - xml_emit_tag(char* tag) // for each tag as it's enters it, finalizes it: TAG /TAG or TAG/
// - xml_emit_attr(char* tag, char* attr, char* value) // for each <TAG ATTR="VALUE"

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

    void* userdata;

    void (*xml_emit_text)(void* userdata, char* path[], char c);
    void (*xml_emit_tag)(void* userdata, char* path[], char* tag);
    void (*xml_emit_attr)(void* userdata, char* path[], char* tag, char* attr, char* value);
} wget_data;

//////////////////////////////////////////////////////////////////////
// system stuff
void print_memory_info(int verbose);

//////////////////////////////////////////////////////////////////////
// wifi stuff
void connect_wifi(char* ssid, char* password);

int http_get(char* url, char* server);

int wget(wget_data* data, char* url, char* server);

//////////////////////////////////////////////////////////////////////
// web stuff
typedef void (*httpd_header)(char* buffer, char* method, char* path); // will be called for each header line, last time NULL
typedef void (*httpd_body)(char* buffer, char* method, char* path); // may be called several times, last time NULL
typedef void (*httpd_response)(int req, char* method, char* path); // you can write(req, ... don't close it, it'll be closed for you

int httpd_init(int port);

// all callbacks are optional
int httpd_next(int s, httpd_header emit_header, httpd_body emit_body, httpd_response emit_response);

// call default printer for testing, never returns
void httpd_loop(int s);

//////////////////////////////////////////////////////////////////////
// gpio/esp8266 hardware stuff
#ifdef UNIX
  // dummy
  #define GPIO_OUTPUT 1
  #define GPIO_INPUT 0
  
  void gpio_enable(int pin, int state);
  void gpio_write(int pin, int value);
  int gpio_read(int pin);
#endif

#endif // COMPAT_H
