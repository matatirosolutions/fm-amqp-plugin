#include "CACert.h"

#if defined(AMQP_HAS_SSL)

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "cacert_data.h"  // generated: kCACertData, kCACertSize

bool LoadBundledCACert(SSL_CTX* ctx)
{
    BIO* bio = BIO_new_mem_buf(kCACertData, (int)kCACertSize);
    if (!bio) return false;

    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    int loaded = 0;
    X509* cert = nullptr;
    while ((cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        X509_STORE_add_cert(store, cert);
        X509_free(cert);
        ++loaded;
    }

    BIO_free(bio);
    return loaded > 0;
}

#endif
