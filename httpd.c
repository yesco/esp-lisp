// simplified from: http://blog.manula.org/2011/05/writing-simple-web-server-in-c.html
//#include <netinet/in.h> // missing on esp-open-rtos
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
//#include <fcntl.h> // duplicate def on esp-open-rtos

#define BUFSIZE 1024

char* httpd(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return "socket";

    // non-block on listen and accept
    fcntl(s, F_SETFL, O_NONBLOCK);

    int enable = 1;
    int o = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (o < 0) return "socket";

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(s, (struct sockaddr*) &address, sizeof(address)) != 0) return "bind";

    while (1) {
        //putchar('.');
        int l = listen(s, 10);
        if (l < 0) return "listen";

        socklen_t addrlen = sizeof(address);
        int req = accept(s, (struct sockaddr*) &address, &addrlen);

        // TODO: non-block - loop, this allows polling... or use thread...
        // http://stackoverflow.com/questions/735249/blocking-socket-returns-eagain
        if (req < 0) continue; // TODO: detect E_AGAIN?
        //if (req < 0) return "accept";

        char *buffer = malloc(BUFSIZE + 1);
        recv(req, buffer, BUFSIZE, 0);
        printf("%s\n", buffer);

        write(req, "HEELKJLKERhello world\n", 12);
        close(req);
    }
    close(s);

    return NULL;
}

int main() {
    char* err = httpd(1111);
    if (err) printf("ERROR: %s\n", err);
}
