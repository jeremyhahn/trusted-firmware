/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for the PSA Crypto API header TF-M ships (interface/include/psa/
 * crypto.h, v2.3.1), only the key derivation calls
 * src/tz/tfm/tfw_dice_partition.c makes, for test_tfw_dice_glue (#608). The
 * constants are the PSA Crypto API's; the test file implements the calls.
 */
#ifndef TFW_STUB_PSA_CRYPTO_H
#define TFW_STUB_PSA_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifndef TFW_STUB_PSA_STATUS_T
#define TFW_STUB_PSA_STATUS_T
typedef int32_t psa_status_t;
#define PSA_SUCCESS ((psa_status_t)0)
#endif

#define PSA_ERROR_GENERIC_ERROR     ((psa_status_t)-132)
#define PSA_ERROR_NOT_PERMITTED     ((psa_status_t)-133)
#define PSA_ERROR_INVALID_HANDLE    ((psa_status_t)-136)

typedef uint32_t psa_key_id_t;
typedef uint32_t psa_algorithm_t;
typedef uint16_t psa_key_derivation_step_t;

#define PSA_ALG_SHA_256 ((psa_algorithm_t)0x02000009)
#define PSA_ALG_HKDF(hash_alg) \
    ((psa_algorithm_t)(0x08000100 | ((hash_alg) & 0x000000ff)))

#define PSA_KEY_DERIVATION_INPUT_SECRET ((psa_key_derivation_step_t)0x0101)
#define PSA_KEY_DERIVATION_INPUT_INFO   ((psa_key_derivation_step_t)0x0203)

typedef struct psa_key_derivation_s {
    uint32_t handle;
} psa_key_derivation_operation_t;

#define PSA_KEY_DERIVATION_OPERATION_INIT {0}

psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *operation,
                                      psa_algorithm_t alg);
psa_status_t psa_key_derivation_input_key(psa_key_derivation_operation_t *operation,
                                          psa_key_derivation_step_t step,
                                          psa_key_id_t key);
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *operation,
                                            psa_key_derivation_step_t step,
                                            const uint8_t *data,
                                            size_t data_length);
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *operation,
                                             uint8_t *output,
                                             size_t output_length);
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *operation);
psa_status_t psa_generate_random(uint8_t *output, size_t output_size);

#endif
