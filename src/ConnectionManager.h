#pragma once

#include <string>
#include <mutex>
#include <optional>

// Forward-declare the rabbitmq-c opaque types so we don't pull the C headers
// into every translation unit.
struct amqp_connection_state_t_;
typedef struct amqp_connection_state_t_ *amqp_connection_state_t;

struct ConnectionConfig {
    std::string host     = "localhost";
    int         port     = 5672;
    std::string vhost    = "/";
    std::string username = "guest";
    std::string password = "guest";

    // TLS — leave caCertPath empty to use plain TCP
    bool        useTLS         = false;
    bool        verifyPeer     = true;
    bool        verifyHostname = true;
    std::string tlsVersion;   // "1.2" to cap at TLS 1.2; empty = allow all
    std::string caCertPath;
    std::string clientCertPath;
    std::string clientKeyPath;
};

//
// ConnectionManager — process-lifetime singleton holding the AMQP connection.
//
// FileMaker invokes plugin functions stateless between calls, so we persist
// the connection here across Connect / Publish / Disconnect calls.
// All public methods are thread-safe.
//
class ConnectionManager {
public:
    static ConnectionManager& Instance();

    // Open a new connection, replacing any existing one.
    // Returns nullopt on success, or an error message string.
    std::optional<std::string> Connect(const ConnectionConfig& config);

    // Publish a message. Reconnects once if the connection is stale.
    // Returns nullopt on success, or an error message string.
    std::optional<std::string> Publish(
        const std::string& exchange,
        const std::string& routingKey,
        const std::string& body
    );

    void Disconnect();
    void Shutdown() noexcept;   // called on plugin unload; swallows exceptions

    // Cheap check: true if a connection handle exists. Does NOT verify the socket is
    // still alive — a silently-dropped connection (broker restart, network blip) is
    // only reflected here once the next Publish/RPC call fails and reconnects.
    bool IsConnected() const;

    // Round-trips a harmless, idempotent RPC (Basic.Qos) against the broker to confirm
    // the connection is genuinely responsive, not just that we hold a handle for it.
    // Returns false if there's no connection or the broker doesn't reply successfully.
    bool Ping();

    // Declare a durable queue (idempotent — safe to call even if queue exists).
    // Returns nullopt on success, or an error message string.
    std::optional<std::string> DeclareQueue(const std::string& queueName);

    // Bind a queue to an exchange with a routing key.
    // Returns nullopt on success, or an error message string.
    std::optional<std::string> BindQueue(
        const std::string& queueName,
        const std::string& exchangeName,
        const std::string& routingKey);

    // Declare an exchange. type = "direct" | "fanout" | "topic" | "headers".
    // durable = 1 survives broker restart.
    // Returns nullopt on success, or an error message string.
    std::optional<std::string> DeclareExchange(
        const std::string& exchangeName,
        const std::string& exchangeType,
        bool durable);

    // Disconnects and resets all properties to defaults.
    // Call at the top of any connection setup script for a clean slate.
    void Reset();

    // Per-property configuration (call before Connect):
    //   TLS.Enabled, TLS.CACert, TLS.ClientCert, TLS.ClientKey
    std::optional<std::string> SetProperty(const std::string& key, const std::string& value);

    // Returns the current config (TLS properties set via SetProperty).
    ConnectionConfig GetConfig() const;

private:
    ConnectionManager() = default;
    ~ConnectionManager() { Shutdown(); }
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    std::optional<std::string> ConnectInternal(const ConnectionConfig& config);
    void                       DisconnectInternal() noexcept;

    // Check an amqp_rpc_reply_t and return an error string if it failed.
    static std::optional<std::string> CheckReply(const struct amqp_rpc_reply_t_& reply,
                                                  const char* context);

    mutable std::mutex      mutex_;
    amqp_connection_state_t conn_    = nullptr;
    int                     channel_ = 0;
    ConnectionConfig        config_;
};
