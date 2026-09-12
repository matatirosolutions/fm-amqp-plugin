#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

int main(int argc, char* argv[]) {
    const char* host = argc > 1 ? argv[1] : "vulture.rmq.cloudamqp.com";
    int port = argc > 2 ? atoi(argv[2]) : 5671;

    printf("Connecting to %s:%d\n", host, port);

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    // Match rabbitmq-c: disable AUTO_RETRY
    SSL_CTX_clear_mode(ctx, SSL_MODE_AUTO_RETRY);

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        fprintf(stderr, "TCP connect failed\n");
        return 1;
    }
    freeaddrinfo(res);

    // Set non-blocking like rabbitmq-c does
    int flags = fcntl(sock, F_GETFL);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    printf("TCP connected (non-blocking)\n");

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host);

    int ret;
    do {
        ret = SSL_connect(ssl);
        if (ret != 1) {
            int err = SSL_get_error(ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
                select(sock+1, err==SSL_ERROR_WANT_READ?&fds:nullptr,
                               err==SSL_ERROR_WANT_WRITE?&fds:nullptr, nullptr, nullptr);
                continue;
            }
            fprintf(stderr, "SSL_connect failed, error=%d\n", err);
            ERR_print_errors_fp(stderr);
            return 1;
        }
    } while (ret != 1);
    printf("TLS handshake OK (protocol=%s)\n", SSL_get_version(ssl));

    // Send AMQP 0-9-1 protocol header
    const unsigned char amqp_hdr[] = { 'A','M','Q','P', 0, 0, 9, 1 };
    ret = SSL_write(ssl, amqp_hdr, 8);
    printf("SSL_write(AMQP header): %d\n", ret);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        fprintf(stderr, "SSL_write failed, error=%d\n", err);
        ERR_print_errors_fp(stderr);
        return 1;
    }

    // Read Connection.Start — retry on WANT_READ (non-blocking)
    unsigned char buf[2048] = {};
    do {
        ret = SSL_read(ssl, buf, sizeof(buf));
        if (ret <= 0) {
            int err = SSL_get_error(ssl, ret);
            printf("SSL_read result=%d err=%d\n", ret, err);
            if (err == SSL_ERROR_WANT_READ) {
                fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
                select(sock+1, &fds, nullptr, nullptr, nullptr);
                continue;
            }
            ERR_print_errors_fp(stderr);
            break;
        }
    } while (ret <= 0);
    printf("SSL_read: %d\n", ret);
    if (ret > 0) {
        printf("First 8 bytes: ");
        for (int i = 0; i < 8 && i < ret; i++) printf("%02x ", buf[i]);
        printf("\n");
        // AMQP Connection.Start frame starts with: 01 00 00 ...
        printf("Looks like AMQP frame: %s\n", buf[0] == 0x01 ? "YES" : "NO");
    } else {
        int err = SSL_get_error(ssl, ret);
        fprintf(stderr, "SSL_read failed, error=%d\n", err);
        ERR_print_errors_fp(stderr);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    return 0;
}
