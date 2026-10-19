/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SHA-256 src/core/suit.c asks tfw/crypto.h for, in layer 0 (#48): the
 * bootloader's one hash, boot_measure.h's, so the manifest gate adds no second
 * SHA-256 to the bootloader.
 */
#include "boot_measure.h"
#include "tfw/crypto.h"

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len,
                               uint8_t out[TFW_SHA256_LEN])
{
    tfw_boot_hash(data, len, out);
    return TFW_OK;
}
