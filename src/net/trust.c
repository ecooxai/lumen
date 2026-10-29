#include "trust.h"
#include <limits.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

bool (*net_trust_roots_loader)(SSL_CTX *context);

bool net_trust_add_der(SSL_CTX *context, const unsigned char *encoded, size_t length) {
    if (!context || !encoded || !length || length > LONG_MAX) return false;
    const unsigned char *cursor = encoded;
    X509 *certificate = d2i_X509(NULL, &cursor, (long)length);
    bool valid = certificate && cursor == encoded + length && X509_check_ca(certificate) > 0;
    if (valid) valid = X509_STORE_add_cert(SSL_CTX_get_cert_store(context), certificate) == 1;
    X509_free(certificate);
    ERR_clear_error();
    return valid;
}
