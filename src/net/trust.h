#ifndef LUMEN_TRUST_H
#define LUMEN_TRUST_H
#include <stdbool.h>
#include <stddef.h>
#include <openssl/ssl.h>

extern bool (*net_trust_roots_loader)(SSL_CTX *context);
bool net_trust_add_der(SSL_CTX *context, const unsigned char *encoded, size_t length);
#endif
