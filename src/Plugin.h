#pragma once

// 4-character plugin identifier registered with Claris.
// Change this to your own unique ID before distribution.
#define PLUGIN_ID     "AMQP"
#define PLUGIN_NAME   "AMQP FM Plugin"
#define PLUGIN_DESC   "Publish messages to an AMQP broker from FileMaker"

// Function IDs — must start at 3, must be stable across releases
enum FunctionID : int {
    kFn_Version      = 3,
    kFn_Connect      = 4,
    kFn_Publish      = 5,
    kFn_Disconnect   = 6,
    kFn_SetProperty  = 7,
    kFn_DeclareQueue    = 8,
    kFn_DeclareExchange = 9,
    kFn_BindQueue       = 10,
    kFn_TLSTest         = 11,
    kFn_Init            = 12,
};
