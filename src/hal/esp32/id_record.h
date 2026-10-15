/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DeviceID record: what the `tfw_id` partition holds.
 *
 * The DS peripheral keeps no key of its own. It takes the RSA private key as
 * ciphertext -- ESP-IDF's esp_ds_data_t, AES-encrypted under a key DS derives
 * in hardware from an eFuse HMAC key -- and that ciphertext has to live
 * somewhere. It lives here, beside the public half, which a DS signature is
 * checked against and which names the key.
 *
 * Little-endian throughout, fixed offsets, the ciphertext field sized for the
 * largest DS (4096-bit, the P4's) and used to its target's ESP_DS_C_LEN. The
 * test provisioner (test/integration/esp32/provision.py) writes the same
 * layout. Nothing in it is secret: the private key is only in `c`, and `c`
 * opens only inside this part's DS.
 */
#ifndef TFW_ESP32_ID_RECORD_H
#define TFW_ESP32_ID_RECORD_H

#include <stdint.h>
#include "crypto/sha256.h"

#define TFW_ID_MAGIC      0x44494654uL   /* 'T','F','I','D' little-endian */
#define TFW_ID_VERSION    1u
#define TFW_ID_MAX_BYTES  512u           /* a 4096-bit modulus */
#define TFW_ID_C_MAX      1584u          /* ESP_DS_C_LEN at 4096 bits */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  ds_key_block;               /* eFuse KEY0..KEY5 as 0..5 */
    uint16_t rsa_bits;
    uint32_t e;
    uint8_t  n_le[TFW_ID_MAX_BYTES];     /* the modulus, little-endian, zero above rsa_bits */
    uint8_t  iv[16];
    uint8_t  c[TFW_ID_C_MAX];
} tfw_id_record_t;

_Static_assert(sizeof(tfw_id_record_t) == 12u + TFW_ID_MAX_BYTES + 16u + TFW_ID_C_MAX,
               "the record layout is fixed: the provisioner writes these offsets");

/* SHA-256 of the modulus big-endian and e as four big-endian bytes: the name
 * the logs give the DeviceID, which a host computes from the public key alone. */
static inline void tfw_id_fingerprint(const tfw_id_record_t *r, uint8_t out[32])
{
    tfw_sha256_t c;
    uint8_t      e_be[4];
    uint16_t     i;

    tfw_sha256_init(&c);
    for (i = (uint16_t)(r->rsa_bits / 8u); i > 0u; i--) {
        tfw_sha256_update(&c, &r->n_le[i - 1u], 1u);
    }
    e_be[0] = (uint8_t)(r->e >> 24);
    e_be[1] = (uint8_t)(r->e >> 16);
    e_be[2] = (uint8_t)(r->e >> 8);
    e_be[3] = (uint8_t)r->e;
    tfw_sha256_update(&c, e_be, 4u);
    tfw_sha256_final(&c, out);
}

#endif /* TFW_ESP32_ID_RECORD_H */
