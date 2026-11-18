/* sha256_port.h - SHA-256 on either ESP-IDF (2026-11-18): IDF 5.x's mbedTLS 3 has mbedtls/sha256.h; IDF 6's mbedTLS 4
 * keeps it private and hashes through PSA Crypto. One shape for both: a streaming context and a one-shot. */
#ifndef SHA256_PORT_H
#define SHA256_PORT_H
#include <stddef.h>
#include <stdint.h>
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 6
#include "psa/crypto.h"
typedef psa_hash_operation_t pt_sha256_t;
static inline void pt_sha256_begin(pt_sha256_t *c) { psa_crypto_init(); *c = psa_hash_operation_init(); psa_hash_setup(c, PSA_ALG_SHA_256); }
static inline void pt_sha256_update(pt_sha256_t *c, const void *p, size_t n) { psa_hash_update(c, (const uint8_t *)p, n); }
static inline void pt_sha256_finish(pt_sha256_t *c, uint8_t out[32]) { size_t n = 0; if (psa_hash_finish(c, out, 32, &n) != PSA_SUCCESS) psa_hash_abort(c); }
#else
#include "mbedtls/sha256.h"
typedef mbedtls_sha256_context pt_sha256_t;
static inline void pt_sha256_begin(pt_sha256_t *c) { mbedtls_sha256_init(c); mbedtls_sha256_starts(c, 0); }
static inline void pt_sha256_update(pt_sha256_t *c, const void *p, size_t n) { mbedtls_sha256_update(c, (const uint8_t *)p, n); }
static inline void pt_sha256_finish(pt_sha256_t *c, uint8_t out[32]) { mbedtls_sha256_finish(c, out); mbedtls_sha256_free(c); }
#endif
static inline void pt_sha256(const void *p, size_t n, uint8_t out[32]) { pt_sha256_t c; pt_sha256_begin(&c); pt_sha256_update(&c, p, n); pt_sha256_finish(&c, out); }
#endif
