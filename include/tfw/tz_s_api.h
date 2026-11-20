/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure world's API for the non-secure side.
 *
 * tfw_s_version is the #590 monitor's NSC veneer
 * (src/hal/cortex-m/tz/tz_veneer.c), linked through the import library the
 * secure link writes (--cmse-implib); it goes with the mps2-an505 monitor in
 * #608 slice 4.
 *
 * The tfw_s_dice_* calls are the client of the TF-M DICE partition (#608
 * slice 3, ruling 1024 (a); src/tz/dice_partition.h has the call types),
 * implemented in src/tz/tz_s_client.c over TF-M's psa_call to the stateless
 * TFW_DICE_SERVICE. Each answers a tfw_status_t: TFW_OK, the partition's
 * refusal (TFW_ERR_RANGE for a short buffer or an offset past the end,
 * TFW_ERR_NOT_PROVISIONED when the partition did not start), or
 * TFW_ERR_INTERNAL for a PSA framework error or an answer of the wrong length.
 */
#ifndef TFW_TZ_S_API_H
#define TFW_TZ_S_API_H

#include <stdint.h>
#include <stddef.h>
#include "tfw/status.h"

/* The secure API's version, major in the high half. */
#define TFW_S_VERSION 0x00010000u

uint32_t tfw_s_version(void);

/* The DeviceID and alias public keys, X||Y. */
tfw_status_t tfw_s_dice_deviceid_pub(uint8_t pub[64]);
tfw_status_t tfw_s_dice_alias_pub(uint8_t pub[64]);

/* One page of the alias certificate over nonce: bytes [off, off + *got) into
 * page (*got <= min(cap, 255)), and *total its full length. */
tfw_status_t tfw_s_dice_alias_cert(const uint8_t nonce[32], uint32_t off,
                                   uint8_t *page, size_t cap, size_t *got,
                                   uint32_t *total);

/* r||s over a SHA-256 digest with the alias key. */
tfw_status_t tfw_s_dice_alias_sign(const uint8_t digest[32], uint8_t sig[64]);

#endif
