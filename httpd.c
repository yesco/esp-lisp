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

#include "compat.h"

#define BUFSIZE 1024
#define MAXQUEUE 10

// > 0, good socket: httpd_next(s)... close(s)
//
// < 0 error:
//   == -1, socket()
//   == -2, setsockopt()
//   == -3, bind()
//   == -4, listen()
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

// simple implementation of getline for fd instead of FILE*
// NOTE: this one does NOT include the \n at end
int fdgetline(char** b, int* len, int fd) {
    int n = 0;
    while (1) {
        if (n + 1 > *len) {
            *len *= 1.3;
            *b = realloc(*b, *len);
        }

        if (read(fd, *b + n, 1) < 1) { // eof
            if (!n) 
                return -1; // no trailing line
            else
                break; // have trailing line (no \n)
        }

        char c = *(*b + n);
        if (c == '\n') break; // new line
        if (c != '\r') n++; // store if not cr
    }
    *(*b + n) = 0;
    return n;
}

int httpd_next(int s, httpd_header emit_header, httpd_body emit_body, httpd_response emit_response) {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    int req = accept(s, (struct sockaddr*) &address, &addrlen);
    //if (req < 0) printf("ACCEPT=%d, errno=%d\n", req, errno);
    if (req < 0) return 0;

    int len = BUFSIZE;
    char *buffer = malloc(len);

    // -- process request line
    if (fdgetline(&buffer, &len, req) <= 0) return 0;
    char method[8];
    strncpy(method, strtok(buffer, " "), 8);
    method[7] = 0;
    char *path = strdup(strtok(NULL, " "));

    // -- process headers
    int expectedsize = -1;
    while (fdgetline(&buffer, &len, req) > 0) {
        if (emit_header) emit_header(buffer, method, path);
        if (strcmp(buffer, "Content-Length: ") == 0) {
            // http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
            // http://stackoverflow.com/questions/2773396/whats-the-content-length-field-in-http-header
            strtok(buffer, " ");
            expectedsize = atoi(strtok(NULL, " "));
        }
    }
    if (emit_header) emit_header(NULL, method, path);

    // TODO: handle POST get parameters, also add paramter getter for path
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html#sec9.5
    // may need to handle different encodings...?
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec7.html#sec7

    // -- process body
    int bodycount = 0;

    // stop reading when got expectedsize bytes,
    // otherwise it'll block as browser keepalive doesn't close, thus no EOF
    int n = 1;
    len--; // remove capacity for 0 termination
    while (n > 0 && bodycount < expectedsize) {
        int remaining = expectedsize - bodycount;
        int n = (len > remaining) ? remaining : len;
        n = read(req, buffer, n);
        if (n < 0) break; // error
        buffer[n] = 0;
        bodycount += n;
        if (n > 0 && emit_body) emit_body(buffer, method, path);
    }
    free(buffer);
    if (emit_body) emit_body(NULL, method, path);

    if (emit_response) emit_response(req, method, path);

    if (path) free(path);
    close(req);
    return 1;
}

// simple for test
static void header(char* buff, char* method, char* path) {
    printf("HEADER: %s\n", buff);
}

static void body(char* buff, char* method, char* path) {
    printf("BODY: %s\n", buff);
}

static void response(int req, char* method, char* path) {
    static char* hello = "HELLO DUDE!\n";
    // TODO: loop until all of the string written?
    int ignore = write(req, hello, strlen(hello));
    printf("------------------------------ %s %s\n\n", method, path);
}

void httpd_loop(int s) {
    while (1) {
        httpd_next(s, header, body, response);
    }
}

#ifdef HTTPD_MAIN
  int main() {
      int s = httpd_init(1111);
      if (s < 0 ) { printf("ERROR.errno=%d\n", errno); return -1; }
  }
#endif
