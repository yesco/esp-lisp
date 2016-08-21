#ifndef COMPAT_H
#define COMPAT_H

typedef unsigned int uint32;
typedef unsigned int uint32_t;

int clock_ms();
int delay_ms(int ms);

unsigned int randomized(); // return pseudorandom seeed

//////////////////////////////////////////////////////////////////////
// IO

int nonblock_getch();

void clear();

char* readline(char* prompt, int maxlen);
char* readline_int(char* prompt, int maxlen, int (*myreadchar)(char*));

typedef int (*process_input)(void* envp, char* input, char* filename, int startno, int endno, int verbosity);
int process_file(void* envp, char* filename, process_input process, int verbosity);

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
// interrupt stuff
void interrupt_init (int pin, int changeType);
int getInterruptCount(int pin, int mode);
void checkInterrupts(int (*cb)(int pin, uint32 clicked, uint32 count, uint32 last));

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

// http://www.esp8266.com/wiki/doku.php?id=esp8266_memory_map
// http://esp8266-re.foogod.com/wiki/Memory_Map
// essentially this is after 512K ROM flash, probably safe to use from here for storage!
#define FS_ADDRESS 0x60000
// http://richard.burtons.org/2015/05/24/memory-map-limitation-for-rboot/

//////////////////////////////////////////////////////////////////////
// gpio/esp8266 hardware stuff

#ifdef UNIX

  #define dht_read(a,b,c) (1)

  #include <dirent.h>

  // dummy
  #define GPIO_OUTPUT 1
  #define GPIO_INPUT 0
  
  void gpio_enable(int pin, int state);
  void gpio_write(int pin, int value);
  int gpio_read(int pin);

  // flash simulation in RAM
  #define SPI_FLASH_RESULT_OK 0
  #define SPI_FLASH_ERROR -1

  #define SPI_FLASH_SEC_SIZE 128 // TODO: is this right?
  extern unsigned char flash_memory[];

  int sdk_spi_flash_erase_sector(int sec);
  int sdk_spi_flash_write(int addr, uint32* data, int len);
  int sdk_spi_flash_read(int addr, uint32* data, int len);

#else
  int dht_read(int pin, int* temp, int* humid);

  #define flash_memory ((unsigned char*)(0x40200000 + FS_ADDRESS))

  #include "esp_spiffs.h"
  #include "spiffs.h"

  struct dirent {
      struct spiffs_dirent e;
      char* d_name;
      size_t fileSize;
  } dirent;

  typedef struct DIR {
      spiffs_DIR d;
      struct dirent dirent;
  } DIR;


  DIR* opendir(char* path);
  struct dirent* readdir(DIR* dp);
  int closedir(DIR* dp);

#endif

#define SPI_FLASH_SIZE_MB (4)
#define SPI_FLASH_SIZE_BYTES (SPI_FLASH_SIZE_MB * 1024U * 1024U)

#endif // COMPAT_H
