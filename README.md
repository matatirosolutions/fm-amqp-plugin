# AMQP FM Plugin

A FileMaker Pro plugin that publishes messages to an AMQP broker (RabbitMQ). Supports TLS, exchange/queue topology management, and runs on macOS, Windows, and Linux (FileMaker Server).

Built on [rabbitmq-c](https://github.com/alanxz/rabbitmq-c) with bundled static OpenSSL — no external runtime dependencies.

---

## Supported platforms

| Platform | File | Architecture |
|---|---|---|
| macOS 11+ | `AMQPFMPlugin.fmplugin` | Universal (arm64 + x86_64) |
| Windows 10/11 | `AMQPFMPlugin.fmx64` | x64 |
| Ubuntu 22.04 (FileMaker Server) | `AMQPFMPlugin.fmx` | x64, arm64 |
| Ubuntu 24.04 (FileMaker Server) | `AMQPFMPlugin.fmx` | x64, arm64 |

---

## FileMaker functions

All functions return `"OK"` on success or `"ERROR: <message>"` on failure.

| Function | Description |
|---|---|
| `AMQP_Version()` | Returns the plugin version string |
| `AMQP_SetProperty( name ; value )` | Sets a connection property (see TLS reference below) |
| `AMQP_Connect( host ; port ; vhost ; user ; password )` | Opens a connection and channel |
| `AMQP_Publish( exchange ; routingKey ; body )` | Publishes a message |
| `AMQP_DeclareQueue( queueName )` | Declares a durable queue |
| `AMQP_DeclareExchange( name ; type ; durable )` | Declares an exchange (`"direct"`, `"topic"`, `"fanout"`, `"headers"`) |
| `AMQP_BindQueue( queue ; exchange ; routingKey )` | Binds a queue to an exchange |
| `AMQP_Disconnect()` | Closes the connection |

See [filemaker/USAGE.md](filemaker/USAGE.md) for detailed examples and the TLS property reference.

---

## Installation

| Platform | Extension folder |
|---|---|
| macOS (current user) | `~/Library/Application Support/FileMaker/Extensions/` |
| macOS (all users) | `/Library/Application Support/FileMaker/Extensions/` |
| Windows | `C:\Program Files\FileMaker\FileMaker Pro\Extensions\` |
| FileMaker Server (Linux) | `/opt/FileMaker/FileMaker Server/Database Server/Extensions/` |

Quit FileMaker (or stop fmse on Server) before installing, then restart.

---

## Building from source

### Prerequisites for all platforms

- CMake 3.21+
- Git (used by FetchContent to pull rabbitmq-c)
- FileMaker Plugin SDK 26 — place it in `sdk/` (see `sdk/README.md`)

### macOS

```bash
# Build static OpenSSL (first time only)
scripts/build-openssl-mac.sh

# Build universal plugin, sign with Developer ID, verify signature
scripts/build-mac.sh

# Notarize and staple (requires App Store Connect credentials stored via notarytool)
scripts/notarize-mac.sh
```

Output: `build/mac/AMQPFMPlugin.fmplugin`

The script defaults to the Matatiro Solutions Developer ID certificate. To use your own:

```bash
# Find your Developer ID certificate
security find-identity -v -p codesigning

# Build signed with your certificate
CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" scripts/build-mac.sh

# Ad-hoc signature for local testing only (not distributable)
CODESIGN_IDENTITY="-" scripts/build-mac.sh
```

To notarize with your own Apple ID, first store your credentials once:

```bash
xcrun notarytool store-credentials "AC_PASSWORD" \
    --apple-id you@example.com \
    --team-id YOURTEAMID \
    --password <app-specific-password>   # generated at appleid.apple.com
```

Then run `scripts/notarize-mac.sh` as normal.

### Linux (via Docker — runs from macOS or Linux)

```bash
# Build for both Ubuntu 22.04 and 24.04, x64
scripts/build-linux-docker.sh

# Single target
scripts/build-linux-docker.sh u22        # Ubuntu 22.04, x64
scripts/build-linux-docker.sh u24 arm64  # Ubuntu 24.04, arm64
```

Output: `build/linux/U22/x64/AMQPFMPlugin.fmx` etc.

Docker builds static OpenSSL automatically on first run and caches it in `third_party/`.

### Windows

Run from a **VS 2022 x64 Native Tools Command Prompt**:

```bat
rem Build static OpenSSL (first time only — needs Strawberry Perl)
scripts\build-openssl-win.bat

rem Build plugin
scripts\build-win.bat
```

Output: `build\win\Release\AMQPFMPlugin.fmx64`

---

## Project structure

```
src/                  Plugin source (C++17)
  Plugin.cpp/.h       FMExternCallProc entry point, function registration
  ConnectionManager   rabbitmq-c wrapper (connection, channel, publish, topology)
  Functions           FileMaker function implementations
  TextUtil            fmx::Text ↔ std::string helpers
cmake/
  Platform.cmake      Platform detection, SDK paths, OpenSSL detection
  FetchDependencies   FetchContent for rabbitmq-c
scripts/              Build scripts for each platform
docker/               Dockerfile for Linux builds
sdk/                  FileMaker Plugin SDK (not redistributed)
third_party/openssl/  Pre-built static OpenSSL per platform (committed)
resources/
  mac/Info.plist      Bundle metadata
  win/Plugin.rc       Version resource
filemaker/USAGE.md    FileMaker scripting guide and function reference
```

---

## TLS

TLS is enabled at build time when static OpenSSL is present in `third_party/openssl/`. All pre-built binaries include TLS support.

Set properties before calling `AMQP_Connect`:

```
AMQP_SetProperty( "TLS.Enabled" ; "1" )
AMQP_SetProperty( "TLS.CACert"  ; "/path/to/ca.pem" )
```

Default AMQP port is `5672` (plain) or `5671` (TLS).

---

## Dependencies

| Library | Version | How included |
|---|---|---|
| [rabbitmq-c](https://github.com/alanxz/rabbitmq-c) | 0.14.0 | FetchContent at build time |
| [OpenSSL](https://www.openssl.org) | 3.3.2 | Static, pre-built in `third_party/` |
| FileMaker Plugin SDK | 26.0.1.51 | `sdk/` (not redistributed) |
