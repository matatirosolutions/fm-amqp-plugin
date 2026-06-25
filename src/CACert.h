#pragma once

#if defined(AMQP_HAS_SSL)
#include <openssl/ssl.h>

// Loads the bundled Mozilla CA certificate data (embedded at build time)
// into the given SSL_CTX's trust store. Returns true on success.
bool LoadBundledCACert(SSL_CTX* ctx);

#endif
