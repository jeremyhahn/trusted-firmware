/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for tf-m-extras' measured_boot_api.h
 * (partitions/measured_boot/interface/include, at scripts/tfm-pins.sh
 * EXTRAS_COMMIT), only the read call and its buffer sizes, for
 * test_tfw_dice_glue (#608). The test file implements the call.
 */
#ifndef TFW_STUB_MEASURED_BOOT_API_H
#define TFW_STUB_MEASURED_BOOT_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "psa/crypto.h"

#define SIGNER_ID_MAX_SIZE         64u
#define VERSION_MAX_SIZE           14u
#define SW_TYPE_MAX_SIZE           20u
#define MEASUREMENT_VALUE_MAX_SIZE 64u

psa_status_t tfm_measured_boot_read_measurement(uint8_t index,
                                                uint8_t *signer_id,
                                                size_t signer_id_size,
                                                size_t *signer_id_len,
                                                uint8_t *version,
                                                size_t version_size,
                                                size_t *version_len,
                                                uint32_t *measurement_algo,
                                                uint8_t *sw_type,
                                                size_t sw_type_size,
                                                size_t *sw_type_len,
                                                uint8_t *measurement_value,
                                                size_t measurement_value_size,
                                                size_t *measurement_value_len,
                                                bool *is_locked);

#endif
