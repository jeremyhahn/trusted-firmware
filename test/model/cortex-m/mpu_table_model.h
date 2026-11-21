/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The M7's MPU region table under the names test/unit/test_cm_mpu.c spells
 * (lint-arch-leak keeps the Cortex-M names in this directory and the HAL's).
 */
#ifndef TFW_TEST_MPU_TABLE_MODEL_H
#define TFW_TEST_MPU_TABLE_MODEL_H

#include <stdint.h>

#include "hal_mpu.h"

typedef tfw_cm_mpu_region_t cm_model_mpu_region_t;

static inline int cm_model_mpu_table(cm_model_mpu_region_t out[TFW_CM_MPU_REGIONS],
                                     uint32_t image_base, uint32_t stack_floor,
                                     uint32_t boot_base, uint32_t boot_size)
{
    return tfw_cm_mpu_table(out, image_base, stack_floor, boot_base, boot_size);
}

#endif /* TFW_TEST_MPU_TABLE_MODEL_H */
