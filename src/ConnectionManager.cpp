#include "ConnectionManager.h"
#include "CACert.h"
#include "Plugin.h"

#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
// ssl_socket.h included only when ENABLE_SSL_SUPPORT is on
#if defined(AMQP_HAS_SSL)
#  include <rabbitmq-c/ssl_socket.h>
#  include <openssl/ssl.h>
#endif

#include <cstring>
#include <sstream>

// ── helpers ──────────────────────────────────────────────────────────────────

static amqp_bytes_t BytesFromString(const std::string& s)
{
    return amqp_bytes_t{ s.size(), const_cast<void*>(static_cast<const void*>(s.c_str())) };
}

std::optional<std::string> ConnectionManager::CheckReply(
    const amqp_rpc_reply_t_& reply, const char* context)
{
    switch (reply.reply_type) {
    case AMQP_RESPONSE_NORMAL:
        return std::nullopt;

    case AMQP_RESPONSE_NONE:
        return std::string(context) + ": missing RPC reply";

    case AMQP_RESPONSE_LIBRARY_EXCEPTION:
        return std::string(context) + ": " + amqp_error_string2(reply.library_error);

    case AMQP_RESPONSE_SERVER_EXCEPTION: {
        std::ostringstream oss;
        oss << context << ": server exception";
        if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            auto* m = static_cast<amqp_connection_close_t*>(reply.reply.decoded);
            oss << " " << m->reply_code << " "
                << std::string(static_cast<char*>(m->reply_text.bytes), m->reply_text.len);
        } else if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
            auto* m = static_cast<amqp_channel_close_t*>(reply.reply.decoded);
            oss << " " << m->reply_code << " "
                << std::string(static_cast<char*>(m->reply_text.bytes), m->reply_text.len);
        }
        return oss.str();
    }
    }
    return std::string(context) + ": unknown error";
}

// ── singleton ────────────────────────────────────────────────────────────────

ConnectionManager& ConnectionManager::Instance()
{
    static ConnectionManager instance;
    return instance;
}

// ── connect ──────────────────────────────────────────────────────────────────

void ConnectionManager::DisconnectInternal() noexcept
{
    if (!conn_) return;
    amqp_channel_close(conn_, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn_);
    conn_    = nullptr;
    channel_ = 0;
}

std::optional<std::string> ConnectionManager::ConnectInternal(const ConnectionConfig& cfg)
{
    DisconnectInternal();

    conn_ = amqp_new_connection();
    if (!conn_)
        return "amqp_new_connection failed";

    amqp_socket_t* socket = nullptr;

#if defined(AMQP_HAS_SSL)
    if (cfg.useTLS) {
        socket = amqp_ssl_socket_new(conn_);
        if (!socket) { DisconnectInternal(); return "amqp_ssl_socket_new failed"; }

        amqp_ssl_socket_set_verify_peer(socket, cfg.verifyPeer ? 1 : 0);
        amqp_ssl_socket_set_verify_hostname(socket, cfg.verifyHostname ? 1 : 0);

        if (!cfg.tlsVersion.empty()) {
            SSL_CTX* ssl_ctx = static_cast<SSL_CTX*>(amqp_ssl_socket_get_context(socket));
            if (cfg.tlsVersion == "1.2") {
                SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);
            } else if (cfg.tlsVersion == "1.3") {
                SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
            }
        }

        if (cfg.verifyPeer) {
            // Use explicit CA path, fall back to bundled Mozilla CA bundle
            const std::string caPath = !cfg.caCertPath.empty()
                ? cfg.caCertPath
                : GetBundledCACertPath();

            if (!caPath.empty()) {
                int rc = amqp_ssl_socket_set_cacert(socket, caPath.c_str());
                if (rc != AMQP_STATUS_OK) {
                    DisconnectInternal();
                    return std::string("Failed to load CA cert: ") + amqp_error_string2(rc);
                }
            }
        }

        if (!cfg.clientCertPath.empty() && !cfg.clientKeyPath.empty()) {
            int rc = amqp_ssl_socket_set_key(socket,
                cfg.clientCertPath.c_str(), cfg.clientKeyPath.c_str());
            if (rc != AMQP_STATUS_OK) {
                DisconnectInternal();
                return std::string("Failed to load client cert/key: ") + amqp_error_string2(rc);
            }
        }
    } else {
#endif
        socket = amqp_tcp_socket_new(conn_);
        if (!socket) { DisconnectInternal(); return "amqp_tcp_socket_new failed"; }
#if defined(AMQP_HAS_SSL)
    }
#endif

    int rc = amqp_socket_open(socket, cfg.host.c_str(), cfg.port);
    if (rc != AMQP_STATUS_OK) {
        DisconnectInternal();
        return std::string("Failed to open socket to ") + cfg.host + ":" +
               std::to_string(cfg.port) + " — " + amqp_error_string2(rc);
    }

    auto loginReply = amqp_login(conn_,
        cfg.vhost.c_str(),
        AMQP_DEFAULT_MAX_CHANNELS,
        AMQP_DEFAULT_FRAME_SIZE,
        0,                          // heartbeat (seconds; 0 = disabled)
        AMQP_SASL_METHOD_PLAIN,
        cfg.username.c_str(),
        cfg.password.c_str());

    if (auto err = CheckReply(loginReply, "Login")) {
        DisconnectInternal();
        return err;
    }

    amqp_channel_open(conn_, 1);
    if (auto err = CheckReply(amqp_get_rpc_reply(conn_), "Channel open")) {
        DisconnectInternal();
        return err;
    }

    channel_ = 1;
    config_  = cfg;
    return std::nullopt;
}

std::optional<std::string> ConnectionManager::Connect(const ConnectionConfig& config)
{
    std::lock_guard lock(mutex_);
    return ConnectInternal(config);
}

// ── publish ──────────────────────────────────────────────────────────────────

std::optional<std::string> ConnectionManager::Publish(
    const std::string& exchange,
    const std::string& routingKey,
    const std::string& body)
{
    std::lock_guard lock(mutex_);

    if (!conn_)
        return "Not connected. Call " PLUGIN_ID "_Connect first.";

    auto tryPublish = [&]() -> std::optional<std::string> {
        amqp_basic_properties_t props{};
        props._flags       = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
        props.content_type = amqp_cstring_bytes("application/octet-stream");
        props.delivery_mode = 2; // persistent

        int rc = amqp_basic_publish(
            conn_, channel_,
            BytesFromString(exchange),
            BytesFromString(routingKey),
            0, 0,           // mandatory, immediate
            &props,
            BytesFromString(body));

        if (rc != AMQP_STATUS_OK)
            return std::string("Publish failed: ") + amqp_error_string2(rc);

        return std::nullopt;
    };

    auto result = tryPublish();

    // Single automatic reconnect on failure (handles stale connections)
    if (result.has_value()) {
        auto reconnErr = ConnectInternal(config_);
        if (reconnErr.has_value())
            return "Publish failed and reconnect failed: " + *reconnErr;
        result = tryPublish();
    }

    return result;
}

// ── declare queue ────────────────────────────────────────────────────────────

std::optional<std::string> ConnectionManager::DeclareQueue(const std::string& queueName)
{
    std::lock_guard lock(mutex_);

    if (!conn_)
        return "Not connected. Call " PLUGIN_ID "_Connect first.";

    amqp_queue_declare(
        conn_, channel_,
        BytesFromString(queueName),
        /*passive*/  0,
        /*durable*/  1,
        /*exclusive*/0,
        /*auto_delete*/0,
        amqp_empty_table);

    return CheckReply(amqp_get_rpc_reply(conn_), "Queue declare");
}

// ── bind queue ───────────────────────────────────────────────────────────────

std::optional<std::string> ConnectionManager::BindQueue(
    const std::string& queueName,
    const std::string& exchangeName,
    const std::string& routingKey)
{
    std::lock_guard lock(mutex_);

    if (!conn_)
        return "Not connected. Call " PLUGIN_ID "_Connect first.";

    amqp_queue_bind(
        conn_, channel_,
        BytesFromString(queueName),
        BytesFromString(exchangeName),
        BytesFromString(routingKey),
        amqp_empty_table);

    return CheckReply(amqp_get_rpc_reply(conn_), "Queue bind");
}

// ── declare exchange ─────────────────────────────────────────────────────────

std::optional<std::string> ConnectionManager::DeclareExchange(
    const std::string& exchangeName,
    const std::string& exchangeType,
    bool durable)
{
    std::lock_guard lock(mutex_);

    if (!conn_)
        return "Not connected. Call " PLUGIN_ID "_Connect first.";

    amqp_exchange_declare(
        conn_, channel_,
        BytesFromString(exchangeName),
        BytesFromString(exchangeType),
        /*passive*/    0,
        /*durable*/    durable ? 1 : 0,
        /*auto_delete*/0,
        /*internal*/   0,
        amqp_empty_table);

    return CheckReply(amqp_get_rpc_reply(conn_), "Exchange declare");
}

// ── disconnect / shutdown ────────────────────────────────────────────────────

void ConnectionManager::Disconnect()
{
    std::lock_guard lock(mutex_);
    DisconnectInternal();
}

void ConnectionManager::Shutdown() noexcept
{
    try { Disconnect(); } catch (...) {}
}

bool ConnectionManager::IsConnected() const
{
    std::lock_guard lock(mutex_);
    return conn_ != nullptr;
}

// ── properties ───────────────────────────────────────────────────────────────

ConnectionConfig ConnectionManager::GetConfig() const
{
    std::lock_guard lock(mutex_);
    return config_;
}

std::optional<std::string> ConnectionManager::SetProperty(
    const std::string& key, const std::string& value)
{
    std::lock_guard lock(mutex_);

    if      (key == "TLS.Enabled")       config_.useTLS         = (value == "1" || value == "true");
    else if (key == "TLS.VerifyPeer")    config_.verifyPeer     = (value != "0" && value != "false");
    else if (key == "TLS.VerifyHostname")config_.verifyHostname = (value != "0" && value != "false");
    else if (key == "TLS.Version")       config_.tlsVersion     = value;
    else if (key == "TLS.CACert")        config_.caCertPath     = value;
    else if (key == "TLS.ClientCert")    config_.clientCertPath = value;
    else if (key == "TLS.ClientKey")     config_.clientKeyPath  = value;
    else return "Unknown property: " + key;

    return std::nullopt;
}
