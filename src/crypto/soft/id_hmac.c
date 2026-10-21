/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft root's symmetric identity: HMAC-SHA256 with the CDI. Kept only for
 * the ATmega328P soft row, which is the owner's (m467, m522) and must not link
 * P-256 code; every other soft image links id_p256.c (mk/firmware.mk SOFT_ID).
 */

#include <string.h>
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "../sha256.h"
#include "../../core/secure_zero.h"
#include "id.h"

uint16_t tfw_soft_id_algorithms(void)
{
    return TFW_ALGBIT_HMAC_SHA256;
}

tfw_status_t tfw_soft_id_sign(const uint8_t cdi[32], const uint8_t digest[32],
                              uint8_t sig[64], uint8_t *alg_id)
{
    uint8_t mac[32];

    tfw_hmac_sha256(cdi, 32u, digest, 32u, mac);
    /* A 32-byte MAC in a 64-byte field, the rest zeroed: uninitialised padding
     * would leak stack contents onto the wire. */
    memcpy(sig, mac, 32u);
    memset(sig + 32u, 0, 32u);
    tfw_secure_zero(mac, sizeof mac);
    *alg_id = TFW_ALG_HMAC_SHA256;
    return TFW_OK;
}

tfw_status_t tfw_soft_id_pubkey(uint8_t out[64])
{
    (void)out;
    /* A symmetric identity has no public half. */
    return TFW_ERR_UNSUPPORTED;
}
