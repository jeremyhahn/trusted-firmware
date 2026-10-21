/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft root's identity as the P-256 DeviceID (#48, #130; owner m522):
 * ES256 with the key src/crypto/deviceid_p256.c derives from the seed
 * HMAC-SHA256(UDS, TFW_DEVICEID_LABEL). tfw-boot holds the UDS and derives the
 * seed; it reaches this file through the CDI handoff (include/tfw/boot.h,
 * version 4), never the UDS. Copied per call and zeroed before return; the
 * scalar is zeroed inside deviceid_p256.c. Linked in place of id_hmac.c by the
 * object list (mk/firmware.mk SOFT_ID).
 */

#include <string.h>
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "../sha256.h"
#include "../deviceid_p256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"
#include "id.h"

;

static int seed_from_handoff(uint8_t seed[32])
{
    return tfw_boot_handoff_device_seed(seed) != 0u;
}

uint16_t tfw_soft_id_algorithms(void)
{
    return TFW_ALGBIT_ECDSA_P256_SHA256;
}

tfw_status_t tfw_soft_id_sign(const uint8_t cdi[32], const uint8_t digest[32],
                              uint8_t sig[64], uint8_t *alg_id)
{
    uint8_t seed[32];

    (void)cdi;
    if (!seed_from_handoff(seed)) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    tfw_deviceid_p256_sign(seed, digest, sig);
    tfw_secure_zero(seed, sizeof seed);
    *alg_id = TFW_ALG_ECDSA_P256_SHA256;
    return TFW_OK;
}

tfw_status_t tfw_soft_id_pubkey(uint8_t out[64])
{
    uint8_t seed[32];

    if (!seed_from_handoff(seed)) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    tfw_deviceid_p256_public(seed, out);
    tfw_secure_zero(seed, sizeof seed);
    return TFW_OK;
}
