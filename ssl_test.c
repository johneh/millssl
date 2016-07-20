#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "libmill.h"
#include "mssl.h"

#define PORT 5555 
#define NCLIENT 10

int read_a_line(sslconn *c, char *buf, int64_t deadline) {
    int total = 0;
    for(;;) {
        assert(total < 256);
        int len = ssl_read(c, buf + total, 1, deadline);
        if (len <= 0)
            return -1;
        total += len;
        if (buf[total-1] == '\n')
            break;
    }
    return total;
}

coroutine void client(int num, ipaddr raddr) {
    msleep(now()+ 50);

    int64_t deadline = now() + 1000;
    sslconn *c = ssl_connect(raddr, deadline);
    assert(c);  /* TODO: check for error (errno) */

    /* ssl_handshake(cs, -1); */

    char req[256];
    sprintf(req, "%d: This is a test.\n", num);
    int reqlen = strlen(req);
    int rc = ssl_write(c, req, reqlen, deadline);
    assert(rc == reqlen); /* TODO: check for error or partial write */
    char resp[256];
    int resplen = read_a_line(c, resp, deadline);
    if (resplen >= 0)
        fprintf(stderr, "Response: %.*s", resplen, resp);
    ssl_close(c);
}
 
coroutine void serve_client(sslconn *c) {
    char buf[256];
    int64_t deadline = now()+1000;
    int total = read_a_line(c, buf, deadline);
    if (total >= 0) {
        fprintf(stderr, "Request: %.*s", total, buf);
        int rc = ssl_write(c, buf, total, deadline);
        assert(rc == total);   /* TODO: check error and partial write */
    }
    ssl_close(c);
}


int main(void) {
    /* Only needed for a server */
    int rc = ssl_serv_init("./cert.pem", "./key.pem");
    if (rc == 0) {
        /* ERR_print_errors_fp(stderr); */
        fprintf(stderr, "ssl_serv_init(): initialization failed.\n");
        fprintf(stderr, "Use the following command to create a self-signed certificate:\n");
        fprintf(stderr, "openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 100 -nodes\n");
        exit(1);
    }

    ipaddr laddr, raddr;
    laddr = iplocal(NULL, PORT, 0);
    raddr = ipremote("127.0.0.1", PORT, 0, -1);
    assert(errno == 0);
    tcpsock lsock = tcplisten(laddr, 32);
    assert(errno == 0);
    int i;
    for (i = 1; i <= NCLIENT; i++)
        go(client(i, raddr));
    while (1) {
        sslconn *c = ssl_accept(lsock, now() + 1000);
        if (! c) /* ETIMEDOUT */
            break;
        go(serve_client(c));
    }

    msleep(now() + 500);
    return 0;
}
