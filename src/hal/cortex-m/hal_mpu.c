/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * MPU-only isolation on the M7 (hal_mpu.h). The table is pure, so the host
 * test builds this file with TFW_CM_MPU_TABLE_ONLY and no registers.
 */
#include <stdint.h>

#include "hal_mpu.h"

/* The build's frame bound (mk/arch/cortex-m.mk, CM_FRAME_MAX) is the one the
 * stack guard is sized for; the host test build passes none. */
#if defined(TFW_CM_FRAME_MAX_BUILD) && (TFW_CM_FRAME_MAX_BUILD != TFW_CM_FRAME_MAX)
#error "CM_FRAME_MAX in mk/arch/cortex-m.mk differs from TFW_CM_FRAME_MAX in hal_mpu.h"
#endif

#if !defined(TFW_CM_MPU_TABLE_ONLY)
#include "tfw/config.h"
#include "cm_regs.h"
#endif

/* RASR.SIZE for a power-of-two size of at least 32 bytes: log2(size) - 1. */
static uint32_t mpu_size_field(uint32_t size)
{
    uint32_t log2 = 0u;

    while ((size >>= 1) != 0u) {
        log2++;
    }
    return log2 - 1u;
}

static void mpu_noaccess(tfw_cm_mpu_region_t *r, uint32_t region,
                         uint32_t base, uint32_t size)
{
    r->rbar = base | TFW_CM_MPU_RBAR_VALID | region;
    r->rasr = TFW_CM_MPU_RASR_XN | TFW_CM_MPU_RASR_AP(0u) |
              TFW_CM_MPU_RASR_SIZE(mpu_size_field(size)) | TFW_CM_MPU_RASR_ENABLE;
}

int tfw_cm_mpu_table(tfw_cm_mpu_region_t out[TFW_CM_MPU_REGIONS],
                     uint32_t image_base, uint32_t stack_floor,
                     uint32_t boot_base, uint32_t boot_size)
{
    uint32_t i;

    for (i = 0u; i < TFW_CM_MPU_REGIONS; i++) {
        out[i].rbar = TFW_CM_MPU_RBAR_VALID | i;
        out[i].rasr = 0u;
    }
    if (boot_size < TFW_CM_MPU_GUARD || (boot_size & (boot_size - 1u)) != 0u ||
        (boot_base & (boot_size - 1u)) != 0u) {
        return -1;
    }

    if (image_base >= TFW_CM_MPU_GUARD) {
        mpu_noaccess(&out[TFW_CM_MPU_NULL], TFW_CM_MPU_NULL, 0u, TFW_CM_MPU_GUARD);
    }
    if (stack_floor != 0u) {
        uint32_t base = (stack_floor + (TFW_CM_MPU_STACK_GUARD - 1u)) &
                        ~(TFW_CM_MPU_STACK_GUARD - 1u);

        mpu_noaccess(&out[TFW_CM_MPU_STACK], TFW_CM_MPU_STACK, base, TFW_CM_MPU_STACK_GUARD);
    }
    if (image_base - boot_base >= boot_size) {
        mpu_noaccess(&out[TFW_CM_MPU_BOOT], TFW_CM_MPU_BOOT, boot_base, boot_size);
    }
    return 0;
}

#if !defined(TFW_CM_MPU_TABLE_ONLY)

#if defined(TFW_CM_MPU)
_Static_assert((TFW_BOOT_REGION_SIZE & (TFW_BOOT_REGION_SIZE - 1u)) == 0u &&
               (TFW_BOOT_REGION_BASE & (TFW_BOOT_REGION_SIZE - 1u)) == 0u,
               "tfw-boot's region must be a PMSAv7 region: a power of two, aligned to itself");

extern uint32_t __tfw_text_base;   /* the image's vector table */
extern uint32_t __heap_start;      /* the stack's floor (mk/arch/cortex-m/image.ld) */
#endif

void tfw_cm_mpu_arm(void)
{
#if defined(TFW_CM_MPU)
    tfw_cm_mpu_region_t table[TFW_CM_MPU_REGIONS];
    uint32_t i;

    if (tfw_cm_mpu_table(table, (uint32_t)(uintptr_t)&__tfw_text_base,
                         (uint32_t)(uintptr_t)&__heap_start,
                         (uint32_t)TFW_BOOT_REGION_BASE, (uint32_t)TFW_BOOT_REGION_SIZE) != 0) {
        return;
    }
    TFW_CM_MPU_CTRL = 0u;
    tfw_cm_dsb();
    tfw_cm_isb();
    for (i = 0u; i < TFW_CM_MPU_REGIONS; i++) {
        TFW_CM_MPU_RNR  = i;
        TFW_CM_MPU_RASR = 0u;
        TFW_CM_MPU_RBAR = table[i].rbar;
        TFW_CM_MPU_RASR = table[i].rasr;
    }
    TFW_CM_MPU_CTRL = TFW_CM_MPU_CTRL_ENABLE | TFW_CM_MPU_CTRL_PRIVDEFENA;
    tfw_cm_dsb();
    tfw_cm_isb();
#endif
}

void tfw_cm_mpu_off(void)
{
#if defined(TFW_CM_MPU)
    TFW_CM_MPU_CTRL = 0u;
    tfw_cm_dsb();
    tfw_cm_isb();
#endif
}

#endif /* !TFW_CM_MPU_TABLE_ONLY */
