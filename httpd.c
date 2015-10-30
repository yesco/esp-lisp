// simplified from: http://blog.manula.org/2011/05/writing-simple-web-server-in-c.html
#ifdef UNIX
  #include <netinet/in.h> // missing on esp-open-rtos
  #include <fcntl.h> // duplicate def on esp-open-rtos
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define BUFSIZE 1024
#define MAXQUEUE 10

// > 0, good socket: httpd_next(s)... close(s)
//
// < 0 error:
//   == -1, socket()
//   == -2, setsockopt()
//   == -3, bind()
//   == -4, listen()
//   ==
int httpd_init(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) printf("SOCKET=%d\n", s);
    if (s < 0) return -1;

    // non-block on listen and accept
    fcntl(s, F_SETFL, O_NONBLOCK);

    int enable = 1;
    int o = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (o < 0) printf("SETSOCKOPT=%d\n", o);
    if (o < 0) return -2;

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(s, (struct sockaddr*) &address, sizeof(address)) != 0) return -3;

    int l = listen(s, MAXQUEUE);
    if (l < 0) return -4;

    return s;
}

typedef int (*httpd_header)(char* buffer);
typedef int (*httpd_body)(char* buffer);
typedef int (*httpd_response)(int req);

int header(char* buff) {
    printf("HEADER: %s\n", buff);
    return 0;
}
int body(char* buff) {
    printf("BODY: %s\n", buff);
    printf("strlen=%d\n", strlen(buff));
    return 0;
}
int response(int req) {
    char *buffer = malloc(BUFSIZE + 1);
    sprintf(buffer, "HELLO DUDE!\n");
    write(req, buffer, strlen(buffer));
    free(buffer);
    return 0;
}

void httpd_next(int s, httpd_header emit_header, httpd_body emit_body, httpd_response emit_response) {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    int req = accept(s, (struct sockaddr*) &address, &addrlen);
    //if (req < 0) printf("ACCEPT=%d, errno=%d\n", req, errno);
    if (req < 0) return;

    char *buffer = malloc(BUFSIZE + 1);
    int n = recv(req, buffer, BUFSIZE, 0);
    buffer[n] = 0;
    printf("%s\n", buffer);

    char *b = buffer;
    char *p = b;
    int nls = 0; // number of empty lines in a row
    while (n) {
        while (*b) {
            if (*p == '\n' || *p == '\r') {
                *p = 0;
                nls = (p == b) ? nls+1 : 0;
                if (nls == 2) break;
                if (b != p) emit_header(b);
                b = p + 1;
            }
            p++;
        }
        n = recv(req, buffer, BUFSIZE, 0);
        buffer[n] = 0;
    }

    emit_body(b);

    while (n > 0) {
        n = recv(req, buffer, BUFSIZE, 0);
        buffer[n] = 0;
        emit_body(buffer);
    }
    free(buffer);

    emit_response(req);

    close(req);
}

void httpd_loop(int s) {
    while (1) {
        httpd_next(s, header, body, response);
    }
}

int main() {
    int s = httpd_init(1111);
    if (s < 0 ) { printf("ERROR.errno=%d\n", errno); return -1; }
}
