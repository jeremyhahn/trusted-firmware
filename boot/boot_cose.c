/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_ACTIVATE's and OTA_CONFIRM's Sig_structures (boot_verify.h). Its own
 * file so the AVR store row and the ESP32 layer 0 link the one encoding.
 */

#include <string.h>
#include "boot_verify.h"

static const uint8_t SIG1[] = { 0x84u, 0x6Au, 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1',
                                0x43u, 0xA1u, 0x01u };
static const uint8_t ACT[]  = { 0x84u, 0x70u, 't', 'f', 'w', '-', 'o', 't', 'a', '-', 'a', 'c',
                                't', 'i', 'v', 'a', 't', 'e', 0x58u, 0x20u };
static const uint8_t CFM[]  = { 0x83u, 0x6Fu, 't', 'f', 'w', '-', 'o', 't', 'a', '-', 'c', 'o',
                                'n', 'f', 'i', 'r', 'm', 0x58u, 0x20u };

/* Where the payload's bstr length goes: after SIG1, the algorithm, the empty
 * external_aad and the bstr head. */
#define AT ((uint8_t)(sizeof SIG1 + 3u))

/* Everything up to and including the version; the caller appends what
 * follows it and sets the payload's length at AT. */
static uint8_t build(uint8_t *b, const uint8_t *tag, uint8_t tag_len,
                     const uint8_t hash[TFW_BOOT_MEASURE_LEN], uint32_t version)
{
    uint8_t n = (uint8_t)sizeof SIG1;
    uint8_t k;

    memcpy(b, SIG1, sizeof SIG1);
    b[n++] = tfw_boot_cose_alg;
    b[n++] = 0x40u;                           /* external_aad: empty */
    b[n++] = 0x58u;                           /* the payload's bstr ... */
    n++;                                      /* ... its length, at AT */
    memcpy(&b[n], tag, tag_len);
    n = (uint8_t)(n + tag_len);
    memcpy(&b[n], hash, TFW_BOOT_MEASURE_LEN);
    n = (uint8_t)(n + TFW_BOOT_MEASURE_LEN);
    /* The version as CBOR's shortest unsigned integer (RFC 8949 §4.2.1). */
    k = (version < 24u) ? 0u : (version < 0x100u) ? 1u : (version < 0x10000uL) ? 2u : 4u;
    b[n++] = (uint8_t)((k == 0u) ? version : (k == 1u) ? 0x18u : (k == 2u) ? 0x19u : 0x1Au);
    while (k != 0u) {
        k--;
        b[n++] = (uint8_t)(version >> (8u * k));
    }
    return n;
}

uint8_t tfw_boot_cose_activate(uint8_t *b, const uint8_t hash[TFW_BOOT_MEASURE_LEN],
                               uint32_t version, uint8_t mode)
{
    uint8_t n = build(b, ACT, (uint8_t)sizeof ACT, hash, version);

    b[n++] = mode;
    b[AT]  = (uint8_t)(n - AT - 1u);
    return n;
}

uint8_t tfw_boot_cose_confirm(uint8_t *b, const uint8_t hash[TFW_BOOT_MEASURE_LEN],
                              uint32_t version)
{
    uint8_t n = build(b, CFM, (uint8_t)sizeof CFM, hash, version);

    b[AT] = (uint8_t)(n - AT - 1u);
    return n;
}
