/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_ACTIVATE's Sig_structure (boot_verify.h). Its own file so the AVR store
 * row and the ESP32 layer 0 link the one encoding.
 */

#include <string.h>
#include "boot_verify.h"

static const uint8_t SIG1[] = { 0x84u, 0x6Au, 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1',
                                0x43u, 0xA1u, 0x01u };
static const uint8_t ACT[]  = { 0x84u, 0x70u, 't', 'f', 'w', '-', 'o', 't', 'a', '-', 'a', 'c',
                                't', 'i', 'v', 'a', 't', 'e', 0x58u, 0x20u };

uint8_t tfw_boot_cose_activate(uint8_t *b, const uint8_t hash[TFW_BOOT_MEASURE_LEN],
                               uint32_t version, uint8_t mode)
{
    uint8_t n = (uint8_t)sizeof SIG1;
    uint8_t at;
    uint8_t k;

    memcpy(b, SIG1, sizeof SIG1);
    b[n++] = tfw_boot_cose_alg;
    b[n++] = 0x40u;                           /* external_aad: empty */
    b[n++] = 0x58u;                           /* the payload's bstr ... */
    at     = n++;                             /* ... its length, below */
    memcpy(&b[n], ACT, sizeof ACT);
    n = (uint8_t)(n + sizeof ACT);
    memcpy(&b[n], hash, TFW_BOOT_MEASURE_LEN);
    n = (uint8_t)(n + TFW_BOOT_MEASURE_LEN);
    /* The version as CBOR's shortest unsigned integer (RFC 8949 §4.2.1). */
    k = (version < 24u) ? 0u : (version < 0x100u) ? 1u : (version < 0x10000uL) ? 2u : 4u;
    b[n++] = (uint8_t)((k == 0u) ? version : (k == 1u) ? 0x18u : (k == 2u) ? 0x19u : 0x1Au);
    while (k != 0u) {
        k--;
        b[n++] = (uint8_t)(version >> (8u * k));
    }
    b[n++] = mode;
    b[at]  = (uint8_t)(n - at - 1u);
    return n;
}
