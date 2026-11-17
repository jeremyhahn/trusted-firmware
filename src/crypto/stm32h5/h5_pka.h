/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ECDSA P-256 on the STM32H5's PKA (#544). The caller has the PKA's bus clock
 * on (RCC AHB2ENR PKAEN, and RNGEN, which the PKA's RAM erase may need) and
 * passes its base. Every value is big-endian, 32 bytes a coordinate or a
 * scalar, as include/tfw/crypto.h's are: a signature is r then s, a public
 * key x then y.
 */
#ifndef TFW_CRYPTO_H5_PKA_H
#define TFW_CRYPTO_H5_PKA_H

#include <stdint.h>

#include "tfw/status.h"

/* Enables the PKA and waits for its RAM erase (INITOK). TFW_ERR_TIMEOUT if
 * it does not finish within TFW_H5_POLL_LIMIT reads. */
tfw_status_t tfw_h5_pka_init(uintptr_t base);

/* TFW_OK when sig is a valid signature of digest under pub; TFW_ERR_CRYPTO
 * when it is not; TFW_ERR_TIMEOUT when the PKA did not answer, or flagged a
 * RAM, address or operation error, in which case the caller decides from
 * the software verify. */
tfw_status_t tfw_h5_pka_ecdsa_verify(uintptr_t base, const uint8_t digest[32],
                                     const uint8_t sig[64], const uint8_t pub[64]);

/* Signs digest with the private scalar d and the nonce k, which the caller
 * draws fresh for every signature. TFW_ERR_CRYPTO when the PKA refuses the
 * pair (r or s zero: draw another k); TFW_ERR_TIMEOUT as for verify. sig is
 * written only on TFW_OK. */
tfw_status_t tfw_h5_pka_ecdsa_sign(uintptr_t base, const uint8_t digest[32],
                                   const uint8_t d[32], const uint8_t k[32],
                                   uint8_t sig[64]);

#endif /* TFW_CRYPTO_H5_PKA_H */
