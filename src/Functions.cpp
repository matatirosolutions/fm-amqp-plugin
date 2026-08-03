#include "Functions.h"
#include "ConnectionManager.h"
#include "TextUtil.h"
#include "Plugin.h"

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

// ── AMQP_IsConnected( { performActiveCheck } ) ───────────────────────────────
// No argument: cheap check of internal state only (no network activity).
// Any argument: also round-trips a request to the broker to confirm it's actually
// responsive, not just that we hold a connection handle.

FMX_PROC(fmx::errcode) Fn_IsConnected(
    short, const fmx::ExprEnv&, const fmx::DataVect& args, fmx::Data& result)
{
    try {
        bool connected = (args.Size() > 0)
            ? ConnectionManager::Instance().Ping()
            : ConnectionManager::Instance().IsConnected();
        SetResultString(connected ? "1" : "0", result);
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
