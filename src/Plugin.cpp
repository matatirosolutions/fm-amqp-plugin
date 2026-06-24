//
// Plugin.cpp — FileMaker plug-in entry point.
//
// FileMaker calls FMExternCallProc for every plugin lifecycle event and every
// function invocation.  We dispatch on the message type here and hand off to
// Functions.cpp for the actual AMQP work.
//

#include "FMWrapper/FMXExtern.h"
#include "FMWrapper/FMXTypes.h"
#include "FMWrapper/FMXText.h"
#include "FMWrapper/FMXCalcEngine.h"

#include "Plugin.h"
#include "Functions.h"
#include "TextUtil.h"
#include "ConnectionManager.h"


// Required by the SDK — must be set at the top of FMExternCallProc
FMX_ExternCallPtr gFMX_ExternCallPtr(nullptr);

// ── Function registration table ──────────────────────────────────────────────

struct FunctionDef {
    int         id;
    const char* name;
    const char* prototype;  // shown in FileMaker's formula editor
    const char* description;
    int         minArgs;
    int         maxArgs;
    FMX_PROC(fmx::errcode) (*handler)(short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data&);
};

static const FunctionDef kFunctions[] = {
    {
        kFn_Version,
        PLUGIN_ID "_Version",
        PLUGIN_ID "_Version()",
        "Returns the plugin name and version string.",
        0, 0,
        Fn_Version
    },
    {
        kFn_Connect,
        PLUGIN_ID "_Connect",
        PLUGIN_ID "_Connect( host ; port ; vhost ; username ; password )",
        "Opens a connection to an AMQP broker. Returns \"OK\" or an error string.",
        5, 5,
        Fn_Connect
    },
    {
        kFn_Publish,
        PLUGIN_ID "_Publish",
        PLUGIN_ID "_Publish( exchange ; routingKey ; messageBody )",
        "Publishes a message to the connected AMQP broker. Returns \"OK\" or an error string.",
        3, 3,
        Fn_Publish
    },
    {
        kFn_Disconnect,
        PLUGIN_ID "_Disconnect",
        PLUGIN_ID "_Disconnect()",
        "Closes the current AMQP connection.",
        0, 0,
        Fn_Disconnect
    },
    {
        kFn_SetProperty,
        PLUGIN_ID "_SetProperty",
        PLUGIN_ID "_SetProperty( propertyName ; value )",
        "Sets a connection property (e.g. TLS settings) before calling " PLUGIN_ID "_Connect.",
        2, 2,
        Fn_SetProperty
    },
    {
        kFn_DeclareQueue,
        PLUGIN_ID "_DeclareQueue",
        PLUGIN_ID "_DeclareQueue( queueName )",
        "Declares a durable queue on the broker. Safe to call if the queue already exists.",
        1, 1,
        Fn_DeclareQueue
    },
    {
        kFn_DeclareExchange,
        PLUGIN_ID "_DeclareExchange",
        PLUGIN_ID "_DeclareExchange( exchangeName ; exchangeType ; durable )",
        "Declares an exchange. exchangeType: \"direct\", \"fanout\", \"topic\", or \"headers\". durable: \"1\" or \"0\".",
        3, 3,
        Fn_DeclareExchange
    },
    {
        kFn_BindQueue,
        PLUGIN_ID "_BindQueue",
        PLUGIN_ID "_BindQueue( queueName ; exchangeName ; routingKey )",
        "Binds a queue to an exchange with a routing key. Use \"#\" to match all routing keys for fanout-style behaviour.",
        3, 3,
        Fn_BindQueue
    },
};

static constexpr int kFunctionCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

// ── Lifecycle helpers ────────────────────────────────────────────────────────

// Copies a UTF-8 C string into a unichar16 buffer (used by kFMXT_GetString)
static void CopyUTF8ToUnichar16(const char* src, fmx::uint32 bufSize, fmx::unichar16* buf)
{
    fmx::TextUniquePtr txt;
    txt->Assign(src, fmx::Text::kEncoding_UTF8);
    fmx::uint32 len = (bufSize <= txt->GetSize()) ? (bufSize - 1) : txt->GetSize();
    txt->GetUnicode(buf, 0, len);
    buf[len] = 0;
}

static fmx::ptrtype Do_PluginInit(fmx::int16 version)
{
    fmx::ptrtype result = static_cast<fmx::ptrtype>(kDoNotEnable);

    if (version < k140ExtnVersion)
        return result;

    const fmx::QuadCharUniquePtr pluginID(PLUGIN_ID[0], PLUGIN_ID[1], PLUGIN_ID[2], PLUGIN_ID[3]);
    const fmx::uint32 flags = fmx::ExprEnv::kDisplayInAllDialogs | fmx::ExprEnv::kFutureCompatible;

    for (int i = 0; i < kFunctionCount; ++i) {
        const FunctionDef& fn = kFunctions[i];

        fmx::TextUniquePtr name;
        name->Assign(fn.name, fmx::Text::kEncoding_UTF8);

        fmx::TextUniquePtr proto;
        proto->Assign(fn.prototype, fmx::Text::kEncoding_UTF8);

        fmx::errcode err;

        if (version >= k150ExtnVersion) {
            fmx::TextUniquePtr desc;
            desc->Assign(fn.description, fmx::Text::kEncoding_UTF8);
            err = fmx::ExprEnv::RegisterExternalFunctionEx(
                *pluginID, fn.id, *name, *proto, *desc,
                fn.minArgs, fn.maxArgs, flags, fn.handler);
        } else {
            err = fmx::ExprEnv::RegisterExternalFunction(
                *pluginID, fn.id, *name, *proto,
                fn.minArgs, fn.maxArgs, flags, fn.handler);
        }

        if (err == 0) {
            result = kCurrentExtnVersion;
        }
    }

    return result;
}

static void Do_PluginShutdown(fmx::int16 version)
{
    if (version < k140ExtnVersion)
        return;

    const fmx::QuadCharUniquePtr pluginID(PLUGIN_ID[0], PLUGIN_ID[1], PLUGIN_ID[2], PLUGIN_ID[3]);

    for (int i = 0; i < kFunctionCount; ++i) {
        fmx::ExprEnv::UnRegisterExternalFunction(*pluginID, kFunctions[i].id);
    }

    ConnectionManager::Instance().Shutdown();
}

static void Do_GetString(fmx::uint32 which, fmx::uint32 /*langID*/, fmx::uint32 bufSize, fmx::unichar16* buf)
{
    switch (which) {
    case kFMXT_NameStr:
        CopyUTF8ToUnichar16(PLUGIN_NAME, bufSize, buf);
        break;

    case kFMXT_AppConfigStr:
        CopyUTF8ToUnichar16(PLUGIN_DESC, bufSize, buf);
        break;

    case kFMXT_OptionsStr:
        // Positions 0-3: plugin ID characters
        buf[0] = PLUGIN_ID[0];
        buf[1] = PLUGIN_ID[1];
        buf[2] = PLUGIN_ID[2];
        buf[3] = PLUGIN_ID[3];
        buf[4] = '1';   // always '1'
        buf[5] = 'n';   // 'Y' = show Configure button in preferences
        buf[6] = 'n';   // always 'n'
        buf[7] = 'Y';   // 'Y' = receive kFMXT_Init / kFMXT_Shutdown
        buf[8] = 'n';   // 'Y' = receive kFMXT_Idle
        buf[9] = 'n';   // 'Y' = receive session/file shutdown messages
        buf[10] = 'n';  // always 'n'
        buf[11] = 0;
        break;

    default:
        buf[0] = 0;
        break;
    }
}

// ── FMExternCallProc — the single entry point FileMaker calls ────────────────

void FMX_ENTRYPT FMExternCallProc(FMX_ExternCallPtr pb)
{
    gFMX_ExternCallPtr = pb;

    switch (pb->whichCall) {
    case kFMXT_Init:
        pb->result = Do_PluginInit(pb->extnVersion);
        break;

    case kFMXT_Idle:
        break;

    case kFMXT_Shutdown:
        Do_PluginShutdown(pb->extnVersion);
        break;

    case kFMXT_DoAppPreferences:
        break;

    case kFMXT_GetString:
        Do_GetString(
            static_cast<fmx::uint32>(pb->parm1),
            static_cast<fmx::uint32>(pb->parm2),
            static_cast<fmx::uint32>(pb->parm3),
            reinterpret_cast<fmx::unichar16*>(pb->result));
        break;

    default:
        break;
    }
}
