#include "Functions.h"
#include "ConnectionManager.h"
#include "TextUtil.h"
#include "Plugin.h"

#if defined(AMQP_HAS_SSL) && !defined(_WIN32)
#  include "CACert.h"
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/select.h>
#  include <sstream>
#  include <cstdio>

// Custom BIO read/write mimicking rabbitmq-c's amqp_openssl_bio on macOS
static int amqp_test_bio_write(BIO *b, const char *in, int inl)
{
    int fd, flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    BIO_get_fd(b, &fd);
    int res = (int)::send(fd, in, inl, flags);
    BIO_clear_retry_flags(b);
    if (res <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_write(b);
    return res;
}

static int amqp_test_bio_read(BIO *b, char *out, int outl)
{
    int fd, flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    BIO_get_fd(b, &fd);
    int res = (int)::recv(fd, out, outl, flags);
    BIO_clear_retry_flags(b);
    if (res <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_read(b);
    return res;
}

static std::string RunOneTLSTest(const std::string& host, int port, bool useAmqpBio);

static std::string RunRabbitmqStyleTest(const std::string& host, int port);

static std::string RunTLSTest(const std::string& host, int port)
{
    std::string a = std::string("=== Standard BIO (SSL_set_fd) ===\n") + RunOneTLSTest(host, port, false);
    std::string b = std::string("=== AMQP-style custom BIO (like rabbitmq-c) ===\n") + RunOneTLSTest(host, port, true);
    std::string c = std::string("=== rabbitmq-c style (embedded CA cert, no VERIFY_NONE) ===\n")
                  + RunRabbitmqStyleTest(host, port);
    return a + "\n" + b + "\n" + c;
}

// Replicates rabbitmq-c's full SSL_CTX setup: TLS min 1.2, PARTIAL_WRITE,
// no AUTO_RETRY, loads embedded CA cert, no explicit VERIFY_NONE.
// This is what actually runs when AMQP_Connect uses TLS.
static std::string RunRabbitmqStyleTest(const std::string& host, int port)
{
    std::ostringstream out;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return "SSL_CTX_new failed";
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_clear_mode(ctx, SSL_MODE_AUTO_RETRY);
    out << "LoadBundledCACert: " << (LoadBundledCACert(ctx) ? "OK" : "FAILED") << "\n";
    // rabbitmq-c does NOT call SSL_CTX_set_verify — default is SSL_VERIFY_NONE

    struct addrinfo hints{}, *ai = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host.c_str(), port_str, &hints, &ai) != 0) {
        SSL_CTX_free(ctx); return out.str() + "getaddrinfo failed";
    }
    int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock < 0) { freeaddrinfo(ai); SSL_CTX_free(ctx); return out.str() + "socket() failed"; }
    out << "TCP connect... ";
    if (::connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
        freeaddrinfo(ai); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED";
    }
    freeaddrinfo(ai);
    out << "OK\n";
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) { close(sock); SSL_CTX_free(ctx); return out.str() + "SSL_new failed"; }
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str());

    out << "TLS handshake... ";
    for (;;) {
        int ret = SSL_connect(ssl);
        if (ret == 1) break;
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            int sel = select(sock + 1, err==SSL_ERROR_WANT_READ?&fds:nullptr,
                                       err==SSL_ERROR_WANT_WRITE?&fds:nullptr, nullptr, &tv);
            if (sel <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return out.str() + "TIMEOUT"; }
            continue;
        }
        char ebuf[256] = {}; ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED err=" + std::to_string(err) + " " + ebuf;
    }
    out << "OK (" << SSL_get_version(ssl) << ")\n";

    // What rabbitmq-c does after SSL_connect: verify peer cert manually
    out << "SSL_get_verify_result: ";
    long vr = SSL_get_verify_result(ssl);
    out << vr << (vr == X509_V_OK ? " (X509_V_OK)" : " (FAIL)") << "\n";

    const unsigned char amqp_hdr[] = {'A','M','Q','P', 0, 0, 9, 1};
    out << "SSL_write AMQP header... ";
    for (;;) {
        int ret = SSL_write(ssl, amqp_hdr, 8);
        if (ret > 0) { out << "OK (" << ret << " bytes)\n"; break; }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            select(sock+1, err==SSL_ERROR_WANT_READ?&fds:nullptr,
                           err==SSL_ERROR_WANT_WRITE?&fds:nullptr, nullptr, &tv);
            continue;
        }
        char ebuf[256] = {}; ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED err=" + std::to_string(err) + " " + ebuf;
    }

    unsigned char buf[2048] = {};
    int attempts = 0;
    out << "SSL_read Connection.Start... ";
    for (;;) {
        int ret = SSL_read(ssl, buf, (int)sizeof(buf));
        if (ret > 0) {
            char hex[128];
            snprintf(hex, sizeof(hex),
                     "OK (%d bytes, %d WANT_READ, first: %02x %02x %02x %02x, AMQP=%s)",
                     ret, attempts, buf[0], buf[1], buf[2], buf[3], buf[0]==0x01?"YES":"NO");
            out << hex << "\n"; break;
        }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            int sel = select(sock+1, err==SSL_ERROR_WANT_READ?&fds:nullptr,
                                     err==SSL_ERROR_WANT_WRITE?&fds:nullptr, nullptr, &tv);
            if (sel <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return out.str() + "TIMEOUT"; }
            attempts++; continue;
        }
        char ebuf[256] = {}; ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED err=" + std::to_string(err) + " " + ebuf;
    }

    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
    return out.str();
}

static std::string RunOneTLSTest(const std::string& host, int port, bool useAmqpBio)
{
    std::ostringstream out;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return "SSL_CTX_new failed";
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_clear_mode(ctx, SSL_MODE_AUTO_RETRY);

    struct addrinfo hints{}, *ai = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host.c_str(), port_str, &hints, &ai) != 0) {
        SSL_CTX_free(ctx);
        return "getaddrinfo failed for " + host;
    }

    int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock < 0) { freeaddrinfo(ai); SSL_CTX_free(ctx); return "socket() failed"; }

    out << "TCP connect... ";
    if (::connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
        freeaddrinfo(ai); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED";
    }
    freeaddrinfo(ai);
    out << "OK\n";

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) { close(sock); SSL_CTX_free(ctx); return out.str() + "SSL_new failed"; }

    if (useAmqpBio) {
        // Replicate rabbitmq-c's amqp_openssl_bio setup exactly
        BIO_METHOD* amqp_bio_meth = BIO_meth_new(BIO_TYPE_SOCKET, "amqp_test_bio");
        BIO_meth_set_create(amqp_bio_meth, BIO_meth_get_create(BIO_s_socket()));
        BIO_meth_set_destroy(amqp_bio_meth, BIO_meth_get_destroy(BIO_s_socket()));
        BIO_meth_set_ctrl(amqp_bio_meth, BIO_meth_get_ctrl(BIO_s_socket()));
        BIO_meth_set_callback_ctrl(amqp_bio_meth, BIO_meth_get_callback_ctrl(BIO_s_socket()));
        BIO_meth_set_read(amqp_bio_meth, BIO_meth_get_read(BIO_s_socket()));
        BIO_meth_set_write(amqp_bio_meth, BIO_meth_get_write(BIO_s_socket()));
        BIO_meth_set_gets(amqp_bio_meth, BIO_meth_get_gets(BIO_s_socket()));
        BIO_meth_set_puts(amqp_bio_meth, BIO_meth_get_puts(BIO_s_socket()));
        BIO_meth_set_write(amqp_bio_meth, amqp_test_bio_write);
        BIO_meth_set_read(amqp_bio_meth, amqp_test_bio_read);

        BIO* bio = BIO_new(amqp_bio_meth);
        BIO_set_fd(bio, sock, BIO_NOCLOSE);
        SSL_set_bio(ssl, bio, bio);
        // amqp_bio_meth leaks here (diagnostic only) — acceptable for a test function
    } else {
        SSL_set_fd(ssl, sock);
    }
    SSL_set_tlsext_host_name(ssl, host.c_str());

    out << "TLS handshake... ";
    for (;;) {
        int ret = SSL_connect(ssl);
        if (ret == 1) break;
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            int sel = select(sock + 1,
                             err == SSL_ERROR_WANT_READ  ? &fds : nullptr,
                             err == SSL_ERROR_WANT_WRITE ? &fds : nullptr,
                             nullptr, &tv);
            if (sel <= 0) {
                SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
                return out.str() + "TIMEOUT";
            }
            continue;
        }
        char ebuf[256] = {};
        ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED ssl_err=" + std::to_string(err) + " " + ebuf;
    }
    out << "OK (" << SSL_get_version(ssl) << ")\n";

    const unsigned char amqp_hdr[] = {'A','M','Q','P', 0, 0, 9, 1};
    out << "SSL_write AMQP header... ";
    for (;;) {
        int ret = SSL_write(ssl, amqp_hdr, 8);
        if (ret > 0) { out << "OK (" << ret << " bytes)\n"; break; }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            int sel = select(sock + 1,
                             err == SSL_ERROR_WANT_READ  ? &fds : nullptr,
                             err == SSL_ERROR_WANT_WRITE ? &fds : nullptr,
                             nullptr, &tv);
            if (sel <= 0) {
                SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
                return out.str() + "TIMEOUT";
            }
            continue;
        }
        char ebuf[256] = {};
        ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED ssl_err=" + std::to_string(err) + " " + ebuf;
    }

    unsigned char buf[2048] = {};
    int attempts = 0;
    out << "SSL_read Connection.Start... ";
    for (;;) {
        int ret = SSL_read(ssl, buf, (int)sizeof(buf));
        if (ret > 0) {
            char hex[128];
            snprintf(hex, sizeof(hex),
                     "OK (%d bytes, %d WANT_READ, first: %02x %02x %02x %02x %02x %02x %02x %02x, AMQP=%s)",
                     ret, attempts,
                     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
                     buf[0] == 0x01 ? "YES" : "NO");
            out << hex << "\n";
            break;
        }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv = {5, 0};
            int sel = select(sock + 1,
                             err == SSL_ERROR_WANT_READ  ? &fds : nullptr,
                             err == SSL_ERROR_WANT_WRITE ? &fds : nullptr,
                             nullptr, &tv);
            if (sel <= 0) {
                SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
                return out.str() + "TIMEOUT";
            }
            attempts++;
            continue;
        }
        char ebuf[256] = {};
        ERR_error_string_n(ERR_get_error(), ebuf, sizeof(ebuf));
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return out.str() + "FAILED ssl_err=" + std::to_string(err) + " " + ebuf;
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    return out.str();
}
#endif

// ── AMQP_Version() ──────────────────────────────────────────────────────────

FMX_PROC(fmx::errcode) Fn_Version(
    short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& result)
{
    SetResultString(PLUGIN_NAME " " PLUGIN_VERSION_STRING, result);
    return 0;
}

// ── AMQP_Connect( host ; port ; vhost ; username ; password ) ────────────────

FMX_PROC(fmx::errcode) Fn_Connect(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        // Start from the stored config so SetProperty values (TLS etc.) are kept
        ConnectionConfig cfg = ConnectionManager::Instance().GetConfig();
        cfg.host     = StringFromData(args.At(0));
        cfg.port     = std::stoi(StringFromData(args.At(1)));
        cfg.vhost    = StringFromData(args.At(2));
        cfg.username = StringFromData(args.At(3));
        cfg.password = StringFromData(args.At(4));

        auto err = ConnectionManager::Instance().Connect(cfg);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_Publish( exchange ; routingKey ; messageBody ) ─────────────────────

FMX_PROC(fmx::errcode) Fn_Publish(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        std::string exchange   = StringFromData(args.At(0));
        std::string routingKey = StringFromData(args.At(1));
        std::string body       = StringFromData(args.At(2));

        auto err = ConnectionManager::Instance().Publish(exchange, routingKey, body);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_Disconnect() ───────────────────────────────────────────────────────

FMX_PROC(fmx::errcode) Fn_Disconnect(
    short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& result)
{
    try {
        ConnectionManager::Instance().Disconnect();
        SetResultOK(result);
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_Init() ─────────────────────────────────────────────────────────────

FMX_PROC(fmx::errcode) Fn_Init(
    short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& result)
{
    try {
        ConnectionManager::Instance().Reset();
        SetResultOK(result);
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_BindQueue( queueName ; exchangeName ; routingKey ) ─────────────────

FMX_PROC(fmx::errcode) Fn_BindQueue(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        std::string queueName    = StringFromData(args.At(0));
        std::string exchangeName = StringFromData(args.At(1));
        std::string routingKey   = StringFromData(args.At(2));

        auto err = ConnectionManager::Instance().BindQueue(queueName, exchangeName, routingKey);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_DeclareExchange( exchangeName ; exchangeType ; durable ) ────────────

FMX_PROC(fmx::errcode) Fn_DeclareExchange(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        std::string exchangeName = StringFromData(args.At(0));
        std::string exchangeType = StringFromData(args.At(1));
        bool        durable      = StringFromData(args.At(2)) == "1";

        auto err = ConnectionManager::Instance().DeclareExchange(exchangeName, exchangeType, durable);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_DeclareQueue( queueName ) ──────────────────────────────────────────

FMX_PROC(fmx::errcode) Fn_DeclareQueue(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        std::string queueName = StringFromData(args.At(0));
        auto err = ConnectionManager::Instance().DeclareQueue(queueName);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_TLSTest( host ; port ) ─────────────────────────────────────────────

FMX_PROC(fmx::errcode) Fn_TLSTest(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
#if defined(AMQP_HAS_SSL) && !defined(_WIN32)
        std::string host = StringFromData(args.At(0));
        int         port = std::stoi(StringFromData(args.At(1)));
        SetResultString(RunTLSTest(host, port), result);
#else
        SetResultString("TLSTest not available on this platform", result);
#endif
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}

// ── AMQP_SetProperty( propertyName ; value ) ────────────────────────────────
//
// Supported properties:
//   TLS.Enabled    — "1" / "0"
//   TLS.CACert     — path to CA certificate file
//   TLS.ClientCert — path to client certificate file
//   TLS.ClientKey  — path to client private key file

FMX_PROC(fmx::errcode) Fn_SetProperty(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        std::string key   = StringFromData(args.At(0));
        std::string value = StringFromData(args.At(1));

        auto err = ConnectionManager::Instance().SetProperty(key, value);
        if (err) {
            SetResultError(*err, result);
        } else {
            SetResultOK(result);
        }
    }
    catch (const std::exception& e) {
        SetResultError(e.what(), result);
    }
    return 0;
}
