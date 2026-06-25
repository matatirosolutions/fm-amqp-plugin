# Using the AMQP FM Plugin from FileMaker

## Installing the plugin

| Platform | Path |
|---|---|
| macOS (user) | `~/Library/Application Support/FileMaker/Extensions/` |
| macOS (all users) | `/Library/Application Support/FileMaker/Extensions/` |
| Windows | `C:\Program Files\FileMaker\FileMaker Pro\Extensions\` |
| FileMaker Server (Linux) | `/opt/FileMaker/FileMaker Server/Database Server/Extensions/` |

Restart FileMaker (or restart fmse on Server) after copying the plugin binary.

## Available functions

All functions return `"OK"` on success or `"ERROR: <message>"` on failure.

### Connection

```
AMQP_Version()
AMQP_SetProperty( propertyName ; value )
AMQP_Connect( host ; port ; vhost ; username ; password )
AMQP_Disconnect()
```

### Messaging

```
AMQP_Publish( exchange ; routingKey ; messageBody )
```

### Broker topology

```
AMQP_DeclareQueue( queueName )
AMQP_DeclareExchange( exchangeName ; exchangeType ; durable )
AMQP_BindQueue( queueName ; exchangeName ; routingKey )
```

## TLS property reference

Set properties **before** calling `AMQP_Connect`.

| Property | Values | Default | Notes |
|---|---|---|---|
| `TLS.Enabled` | `"1"` / `"0"` | `"0"` | Enable TLS. Use port 5671. |
| `TLS.VerifyPeer` | `"1"` / `"0"` | `"1"` | Verify the server's certificate against the CA bundle. |
| `TLS.VerifyHostname` | `"1"` / `"0"` | `"1"` | Verify the server hostname matches the certificate. |
| `TLS.Version` | `"1.2"` / `"1.3"` | _(any)_ | Restrict to a specific TLS version. Leave unset to allow TLS 1.2 or 1.3. |
| `TLS.CACert` | file path | _(bundled)_ | Path to a CA bundle (PEM). The plugin includes a Mozilla CA bundle and uses it by default — only set this if you need a custom or private CA. |
| `TLS.ClientCert` | file path | | Client certificate for mutual TLS. |
| `TLS.ClientKey` | file path | | Client private key for mutual TLS. |

## Example script — plain publish

```
# Connect
Set Variable [ $r ; AMQP_Connect( "rabbitmq.example.com" ; "5672" ; "/" ; "myuser" ; "mypassword" ) ]
If [ Left( $r ; 5 ) = "ERROR" ]
    Show Custom Dialog [ "Connection failed" ; $r ]
    Exit Script []
End If

# Publish to the default exchange using a routing key that matches a queue name
Set Variable [ $body ; JSONSetElement( "" ;
    [ "event" ; "RecordSaved" ; JSONString ] ;
    [ "id"    ; CustomerID    ; JSONNumber ]
) ]
Set Variable [ $r ; AMQP_Publish( "" ; "customer.events" ; $body ) ]
If [ Left( $r ; 5 ) = "ERROR" ]
    Show Custom Dialog [ "Publish failed" ; $r ]
End If

Set Variable [ $r ; AMQP_Disconnect ]
```

## Example script — TLS with topology setup

```
# Enable TLS (bundled CA cert is used automatically)
Set Variable [ $r ; AMQP_SetProperty( "TLS.Enabled" ; "1" ) ]

# Connect
Set Variable [ $r ; AMQP_Connect( "rabbitmq.example.com" ; "5671" ; "/" ; "myuser" ; "mypassword" ) ]
If [ Left( $r ; 5 ) = "ERROR" ]
    Show Custom Dialog [ "Connection failed" ; $r ]
    Exit Script []
End If

# Declare a topic exchange
Set Variable [ $r ; AMQP_DeclareExchange( "events" ; "topic" ; "1" ) ]

# Declare a durable queue and bind it
Set Variable [ $r ; AMQP_DeclareQueue( "customer.events" ) ]
Set Variable [ $r ; AMQP_BindQueue( "customer.events" ; "events" ; "customer.*" ) ]

# Publish
Set Variable [ $body ; JSONSetElement( "" ;
    [ "event" ; "RecordSaved" ; JSONString ] ;
    [ "id"    ; CustomerID    ; JSONNumber ]
) ]
Set Variable [ $r ; AMQP_Publish( "events" ; "customer.saved" ; $body ) ]

Set Variable [ $r ; AMQP_Disconnect ]
```

## AMQP_DeclareExchange — exchangeType values

| Value | Description |
|---|---|
| `"direct"` | Routes by exact routing key match |
| `"topic"` | Routes by routing key pattern (`*` one word, `#` zero or more) |
| `"fanout"` | Broadcasts to all bound queues, routing key ignored |
| `"headers"` | Routes by message header attributes |
