/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * MPU-only isolation on an ARMv7-M core with no TrustZone (the M7 of
 * mps2-an500; docs/cortex-m/engineering.md, ruling 632 item 208 a). The PMSAv7
 * MPU holds three no-access, execute-never regions:
 *
 *   TFW_CM_MPU_NULL   32 bytes at address 0, so a null dereference faults
 *   TFW_CM_MPU_STACK  2 KiB at the stack's floor, so an overflow faults
 *                     before it reaches .noinit, .bss or .data
 *   TFW_CM_MPU_BOOT   tfw-boot's region, hidden from the application
 *
 * PRIVDEFENA keeps the default map everywhere else. MemManage is not enabled
 * in SHCSR, so a violation escalates to HardFault, tfw_cm_fault, and ends in
 * PANIC; HFNMIENA is clear, so the MPU is off while the fault handler stacks
 * and saves its record, even after a stack-guard hit.
 *
 * tfw_cm_mpu_table is pure (test/unit/test_cm_mpu.c); tfw_cm_mpu_arm writes it
 * on a board that sets TFW_CM_MPU and does nothing on any other.
 */
#ifndef TFW_HAL_CM_MPU_H
#define TFW_HAL_CM_MPU_H

#include <stdint.h>

#define TFW_CM_MPU_NULL     0u
#define TFW_CM_MPU_STACK    1u
#define TFW_CM_MPU_BOOT     2u
#define TFW_CM_MPU_REGIONS  3u

#define TFW_CM_MPU_GUARD    32u            /* the smallest PMSAv7 region */

/*
 * The stack guard's guarantee. A function's frame, the space its prologue
 * takes below the caller's SP, is at most TFW_CM_FRAME_MAX bytes: mk/arch/
 * cortex-m.mk builds every Cortex-M object and LTO link with
 * -Werror=frame-larger-than=$(CM_FRAME_MAX), -Werror=vla and -Werror=alloca,
 * and hal_mpu.c fails to compile when the two numbers differ. The guard is
 * larger than any frame plus the margin GCC's frame figure may leave out (the
 * callee-saved push, 36 bytes, and the 32-byte soft-float exception frame with
 * its 4-byte realignment), so SP cannot step from above the guard to below it
 * in one frame: the first push or store below the guard's top lands in the
 * guard and faults. A stack that grows below the guard without crossing it is
 * impossible: every frame starts at an SP still at or above the guard's base.
 *
 * The final figures: a frame bound of 1024 bytes and a 2 KiB guard. They come
 * from the LTO-link frames GCC 14 reports with the bound at 512 on every
 * Cortex-M boot and firmware row: the largest is tfw-boot's boot_policy.c
 * (744 bytes), then boot_main.c (up to 688), the firmware's 664 and
 * verify_p256_soft.c (616). 1024 leaves room over 744; the guard is the next
 * power of two over 1024 + TFW_CM_FRAME_MARGIN. Only mps2-an500 (4 MiB of
 * SRAM) arms the guard, and it takes stack, not linked RAM, so no row's RAM
 * budget changes.
 */
#define TFW_CM_FRAME_MAX        1024u
#define TFW_CM_FRAME_MARGIN     72u
#define TFW_CM_MPU_STACK_GUARD  2048u      /* a power of two, aligned to itself */
_Static_assert(TFW_CM_FRAME_MAX + TFW_CM_FRAME_MARGIN <= TFW_CM_MPU_STACK_GUARD,
               "the stack guard must be larger than any frame");

/* MPU_RBAR: the base, VALID and the region number. */
#define TFW_CM_MPU_RBAR_VALID   (1u << 4)
/* MPU_RASR: XN, AP (000 = no access), SIZE (2^(SIZE+1) bytes), ENABLE. */
#define TFW_CM_MPU_RASR_XN      (1u << 28)
#define TFW_CM_MPU_RASR_AP(x)   ((uint32_t)(x) << 24)
#define TFW_CM_MPU_RASR_SIZE(x) ((uint32_t)(x) << 1)
#define TFW_CM_MPU_RASR_ENABLE  1u

typedef struct {
    uint32_t rbar;   /* RBAR as written: base | VALID | region */
    uint32_t rasr;   /* RASR as written; 0 leaves the region disabled */
} tfw_cm_mpu_region_t;

/*
 * The table for an image whose vector table is at image_base, whose stack
 * floor (the linker's __heap_start) is stack_floor, on a board whose tfw-boot
 * region is boot_base, boot_size (a power of two, aligned to itself):
 *
 *   - the null guard unless the image's vectors sit at 0 (tfw-boot at 0);
 *   - the TFW_CM_MPU_STACK_GUARD-byte stack guard at the first boundary of
 *     its size at or above stack_floor,
 *     none when stack_floor is 0;
 *   - tfw-boot hidden unless image_base is inside tfw-boot's region.
 *
 * Returns 0, or -1 when boot_size is not a power of two of at least 32 bytes
 * or boot_base is not aligned to it (out is then all disabled).
 */
int tfw_cm_mpu_table(tfw_cm_mpu_region_t out[TFW_CM_MPU_REGIONS],
                     uint32_t image_base, uint32_t stack_floor,
                     uint32_t boot_base, uint32_t boot_size);

/* Write this image's table and enable the MPU (TFW_CM_MPU boards only). */
void tfw_cm_mpu_arm(void);

/* Disable the MPU: tfw-boot, before it jumps to the application, whose own
 * tfw_cm_reset arms the application's table. */
void tfw_cm_mpu_off(void);

#endif /* TFW_HAL_CM_MPU_H */
