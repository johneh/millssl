#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "libmill.h"
#include "assert.h"
#include "mssl.h"

static SSL_CTX *ssl_cli_ctx;
static SSL_CTX *ssl_serv_ctx;

struct sslconn_s {
    unsigned long sslerr; 
    int fd;
    BIO *bio;
};

static int ssl_wait(sslconn *c, int64_t deadline) {
    if (! BIO_should_retry(c->bio)) {
        c->sslerr = ERR_get_error();
        /* XXX: Ref. openssl/source/ssl/ssl_lib.c .. */
        if (ERR_GET_LIB(c->sslerr) != ERR_LIB_SYS)
            errno = EIO;
        return -1;
    }

    if (BIO_should_read(c->bio)) {
        int rc = fdwait(c->fd, FDW_IN, deadline);
        if (rc == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        return rc;
    }
    if (BIO_should_write(c->bio)) {
        int rc = fdwait(c->fd, FDW_OUT, deadline);
        if (rc == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        return rc;
    }
    /* assert(!BIO_should_io_special(c->bio)); */
    errno = EIO;
    return -1;  /* should not happen ? */
}


/* optional: call after ssl_connect()/ssl_accept() */
int ssl_handshake(sslconn *c, int64_t deadline) {
    while (BIO_do_handshake(c->bio) <= 0) {
        if (ssl_wait(c, deadline) < 0)
            return -1;
    }

#if 0
    SSL *ssl;
    BIO_get_ssl(c->bio, &ssl);
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (cert) {
        X509_NAME *certname = X509_get_subject_name(cert);
        X509_NAME_print_ex_fp(stderr, certname, 0, 0);
        X509_free(cert);

        /* verify cert, requires loading known certs.
         * See SSL_CTX_load_verify_locations() call in ssl_init() */
        if (SSL_get_verify_result(ssl) != X509_V_OK) {
            ...
        }
    }
#endif
    return 0;
}

void ssl_close(sslconn *c) {
    BIO_ssl_shutdown(c->bio);
    BIO_free_all(c->bio);
    free(c);
}

const char *ssl_errstr(sslconn *c) {
    static const char unknown_err[] = "Unknown error";
    if (c->sslerr)
        return ERR_error_string(c->sslerr, NULL);
    if (errno)
        return strerror(errno);
    return unknown_err;
}

static sslconn *ssl_conn_new(tcpsock s, SSL_CTX *ctx, int client) {
    assert(ctx);
    SSL *ssl = NULL;
    BIO *sbio = BIO_new_ssl(ctx, client);
    if (! sbio)
        return NULL;
    BIO_get_ssl(sbio, & ssl);
    if (!ssl) {
        BIO_free(sbio);
        return NULL;
    }

    /* set .._PARTIAL_WRITE for non-blocking operation */
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    int fd = tcpdetach(s);
    BIO *cbio = BIO_new_socket(fd, BIO_NOCLOSE);
    if (! cbio) {
        BIO_free(sbio);
        return NULL;
    } 
    BIO_push(sbio, cbio);
    sslconn *c = malloc(sizeof (sslconn));
    if (! c) {
        BIO_free_all(sbio);
        return NULL;
    }

    /*  assert(BIO_get_fd(sbio, NULL) == fd); */

    c->bio = sbio;
    c->sslerr = 0;
    c->fd = fd;
    /* OPTIONAL: call ssl_handshake() to check/verify peer certificate */
    return c;
}

int ssl_read(sslconn *c, void *buf, int len, int64_t deadline) {
    int rc;
    if (len < 0) {
        errno = EINVAL;
        return -1;
    }
    do {
        rc = BIO_read(c->bio, buf, len);
        if (rc >= 0)
            break;
        if (ssl_wait(c, deadline) < 0)
            return -1;
    } while (1);
    return rc;
}

int ssl_write(sslconn *c, const void *buf, int len, int64_t deadline) {
    int rc;
    if (len < 0) {
        errno = EINVAL;
        return -1;
    }
    do {
        rc = BIO_write(c->bio, buf, len);
        if (rc >= 0)
            break;
        if (ssl_wait(c, deadline) < 0)
            return -1;
    } while (1);
    return rc;
}

static int load_certificates(SSL_CTX *ctx,
                const char *cert_file, const char *key_file) {

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0
        || SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0
    )
        return 0;
    if (SSL_CTX_check_private_key(ctx) <= 0) /* inconsistent private key */
        return 0;
    return 1;
}

/* use ERR_print_errors(_fp) for SSL error */
static int ssl_init(void) {
    ERR_load_crypto_strings();
    ERR_load_SSL_strings();
    OpenSSL_add_all_algorithms();

    SSL_library_init();

    /* seed the PRNG .. */

    ssl_cli_ctx = SSL_CTX_new(SSLv23_client_method());
    if (ssl_cli_ctx == NULL)
        return 0;
#if 0
    /* XXX: if verifying cert using SSL_get_verify_result(), see ssl_handshake.
     *
     */
    if (!SSL_CTX_load_verify_locations(ssl_cli_ctx, NULL, "/etc/ssl/certs")) {
        ...
    }
#endif
    return 1;
}

/* use ERR_print_errors(_fp) for SSL error */
int ssl_serv_init(const char *cert_file, const char *key_file) {
    if (! ssl_cli_ctx) {
        if (! ssl_init())
            return 0;
    }
    ssl_serv_ctx = SSL_CTX_new(SSLv23_server_method());
    if (! ssl_serv_ctx)
        return 0;
    return load_certificates(ssl_serv_ctx, cert_file, key_file);
}

sslconn *ssl_connect(ipaddr addr, int64_t deadline) {
    if (! ssl_cli_ctx) {
        if (! ssl_init()) {
            errno = EPROTO;
            return NULL;
        }
    }
    tcpsock sock = tcpconnect(addr, deadline);
    if (! sock)
        return NULL;
    sslconn *c = ssl_conn_new(sock, ssl_cli_ctx, 1);
    if (! c) {
        tcpclose(sock);
        errno = ENOMEM;
        return NULL;
    }
    return c;
}

sslconn *ssl_accept(tcpsock lsock, int64_t deadline) {
    if (! ssl_serv_ctx) {
        errno = EPROTO;
        return NULL;
    }
    tcpsock sock = tcpaccept(lsock, deadline);
    if (!sock)
        return NULL;
    sslconn *c = ssl_conn_new(sock, ssl_serv_ctx, 0);
    if (! c) {
        tcpclose(sock);
        errno = ENOMEM;
        return NULL;
    }
    return c;
}
