Test libmill with openssl.

You will need to patch libmill/tcp.c to include the following function:

int tcpdetach(struct mill_tcpsock *s) {
    int fd;
    mill_assert(s->type == MILL_TCPCONN);
    fd = ((struct mill_tcpconn*)s)->fd;
    struct mill_tcpconn *c = (struct mill_tcpconn*)s;
    fdclean(c->fd);
    free(c);
    return fd;
}

Also, make sure to export it in libmill.h.

For the test program, create a self-signed certificate
using the following command:
    openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 100 -nodes
