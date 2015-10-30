void print_memory_info(int verbose);

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

void connect_wifi(char* ssid, char* password);
int http_get(char* url, char* server);
int wget(wget_data* data, char* url, char* server);

typedef int (*httpd_header)(char* buffer);
typedef int (*httpd_body)(char* buffer);
typedef int (*httpd_response)(int req);

int httpd_init(int port);
void httpd_next(int s, httpd_header emit_header, httpd_body emit_body, httpd_response emit_response);
void httpd_loop(int s);

