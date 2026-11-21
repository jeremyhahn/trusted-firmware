/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SHA-256 on the STM32H5's HASH peripheral (#544). The caller has the
 * peripheral's bus clock on (RCC AHB2ENR HASHEN) and passes its base.
 */
#ifndef TFW_CRYPTO_H5_HASH_H
#define TFW_CRYPTO_H5_HASH_H

#include <stdint.h>

#include "tfw/status.h"

/* The digest of len bytes at data into out. TFW_ERR_TIMEOUT when the
 * peripheral does not finish within TFW_H5_POLL_LIMIT status reads; out is
 * then untouched and the caller answers from the software SHA-256. */
tfw_status_t tfw_h5_hash_sha256(uintptr_t base, const uint8_t *data, uint16_t len,
                                uint8_t out[32]);

#endif /* TFW_CRYPTO_H5_HASH_H */
