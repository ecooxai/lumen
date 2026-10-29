#include "../src/net/trust.h"
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static X509 *certificate(EVP_PKEY *key, X509 *issuer, EVP_PKEY *signer, bool root) {
    X509 *cert = X509_new();
    CHECK(cert);
    CHECK(X509_set_version(cert, 2));
    CHECK(ASN1_INTEGER_set(X509_get_serialNumber(cert), root ? 1 : 2));
    CHECK(X509_gmtime_adj(X509_getm_notBefore(cert), -60));
    CHECK(X509_gmtime_adj(X509_getm_notAfter(cert), 3600));
    CHECK(X509_set_pubkey(cert, key));
    X509_NAME *name = X509_get_subject_name(cert);
    CHECK(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
          (const unsigned char *)(root ? "Codex Android test root" : "fixture.invalid"), -1, -1, 0));
    CHECK(X509_set_issuer_name(cert, issuer ? X509_get_subject_name(issuer) : name));
    X509_EXTENSION *extension = X509V3_EXT_conf_nid(NULL, NULL, NID_basic_constraints, root ? "critical,CA:TRUE" : "critical,CA:FALSE");
    CHECK(extension && X509_add_ext(cert, extension, -1));
    X509_EXTENSION_free(extension);
    CHECK(X509_sign(cert, signer, NULL));
    return cert;
}

static bool verifies(SSL_CTX *context, X509 *leaf, const char *host) {
    X509_STORE_CTX *verification = X509_STORE_CTX_new();
    CHECK(verification && X509_STORE_CTX_init(verification, SSL_CTX_get_cert_store(context), leaf, NULL));
    CHECK(X509_VERIFY_PARAM_set1_host(X509_STORE_CTX_get0_param(verification), host, 0));
    bool valid = X509_verify_cert(verification) == 1;
    X509_STORE_CTX_free(verification);
    return valid;
}

int main(void) {
    EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "ED25519");
    EVP_PKEY *other_key = EVP_PKEY_Q_keygen(NULL, NULL, "ED25519");
    CHECK(key && other_key);
    X509 *root = certificate(key, NULL, key, true);
    X509 *leaf = certificate(other_key, root, key, false);
    X509 *untrusted = certificate(other_key, NULL, other_key, true);
    X509 *untrusted_leaf = certificate(key, untrusted, other_key, false);
    unsigned char *encoded = NULL;
    int length = i2d_X509(root, &encoded);
    CHECK(length > 0);
    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    CHECK(context);
    CHECK(!verifies(context, leaf, "fixture.invalid"));
    CHECK(!net_trust_add_der(NULL, encoded, (size_t)length));
    CHECK(!net_trust_add_der(context, NULL, 0));
    CHECK(!net_trust_add_der(context, encoded, (size_t)length - 1));
    CHECK(net_trust_add_der(context, encoded, (size_t)length));
    CHECK(net_trust_add_der(context, encoded, (size_t)length));
    CHECK(verifies(context, leaf, "fixture.invalid"));
    CHECK(!verifies(context, leaf, "wrong.invalid"));
    CHECK(!verifies(context, untrusted_leaf, "fixture.invalid"));
    unsigned char *trailing = malloc((size_t)length + 1);
    CHECK(trailing);
    memcpy(trailing, encoded, (size_t)length);
    trailing[length] = 0;
    CHECK(!net_trust_add_der(context, trailing, (size_t)length + 1));
    free(trailing);
    OPENSSL_free(encoded);
    encoded = NULL;
    length = i2d_X509(leaf, &encoded);
    CHECK(length > 0 && !net_trust_add_der(context, encoded, (size_t)length));
    OPENSSL_free(encoded);
    SSL_CTX_free(context);
    X509_free(root);
    X509_free(leaf);
    X509_free(untrusted);
    X509_free(untrusted_leaf);
    EVP_PKEY_free(key);
    EVP_PKEY_free(other_key);
    puts("Android trust import tests ok");
    return 0;
}
