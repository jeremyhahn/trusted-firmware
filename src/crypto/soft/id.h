/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft provider's identity: id_p256.c (ES256 with the P-256 DeviceID) or
 * id_hmac.c (HMAC with the CDI, the 328P soft row only), chosen by object list.
 */
#ifndef TFW_SOFT_ID_H
#define TFW_SOFT_ID_H

#include <stdint.h>
#include "tfw/crypto.h"

uint16_t     tfw_soft_id_algorithms(void);
tfw_status_t tfw_soft_id_sign(const uint8_t cdi[32], const uint8_t digest[32],
                              uint8_t sig[64], uint8_t *alg_id);
tfw_status_t tfw_soft_id_pubkey(uint8_t out[64]);

#endif /* TFW_SOFT_ID_H */
