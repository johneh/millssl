typedef struct sslconn_s sslconn;
int ssl_serv_init(const char *cert_file, const char *key_file);
sslconn *ssl_connect(ipaddr addr, int64_t deadline);
sslconn *ssl_accept(tcpsock lsock, int64_t deadline);
int ssl_read(sslconn *c, void *buf, int len, int64_t deadline);
int ssl_write(sslconn *c, const void *buf, int len, int64_t deadline); 
int ssl_handshake(sslconn *c, int64_t deadline);
void ssl_close(sslconn *c);
const char *ssl_errstr(sslconn *c);
