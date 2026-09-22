/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_SHA256_H
#define AG_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint8_t  buflen;
} ag_sha256_t;

void ag_sha256_init(ag_sha256_t *c);
void ag_sha256_update(ag_sha256_t *c, const uint8_t *data, uint16_t len);
void ag_sha256_final(ag_sha256_t *c, uint8_t out[32]);
void ag_sha256(const uint8_t *data, uint16_t len, uint8_t out[32]);

/* HMAC-SHA256. The DICE one-way function: the hardware requirements spec gives
 * HMAC(UDS, H(code)) as the stronger of its two forms, because an HMAC's
 * security is the full width of its key where a bare hash's is half its digest.
 */
void ag_hmac_sha256(const uint8_t *key, uint8_t keylen,
                    const uint8_t *data, uint16_t datalen,
                    uint8_t out[32]);

#endif
