/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The factory copies of a two-slot MPS2 board (BOOT_AB=1, mk/boot.mk's A/B
 * rows), what a programmer does once on a part with flash.
 *
 * QEMU copies every image it loaded back in on each system reset
 * (hw/core/loader.c, rom_reset), so a slot or a platform block it loaded could
 * never keep a swap, a trial or a confirm. It loads them at TFW_CM_LOAD_APP_AT
 * and TFW_CM_LOAD_PLATFORM_AT instead (board_profile.h), and this copies each
 * to its home while the home is blank: QEMU starts every RAM zeroed, and no
 * slot or block tfw-boot wrote is. From then on the home is tfw-boot's, as
 * after a programmer, and a reset puts back only the factory copies, which
 * nothing reads again.
 *
 * Nothing here is trusted: the primary is measured and verified as any image
 * is, and a platform block that does not check is recovery.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_soc.h"
#include "cm_regs.h"

_Static_assert(TFW_CM_LOAD_APP_AT + TFW_SWAP_SLOT_LEN <= TFW_SWAP_PRIMARY_BASE
               || TFW_CM_LOAD_APP_AT >= TFW_APP_WRITE_END,
               "the factory application must be clear of both slots");
_Static_assert(TFW_CM_LOAD_PLATFORM_AT + TFW_CM_PLATFORM_SIZE <= TFW_CM_PLATFORM_BASE
               || TFW_CM_LOAD_PLATFORM_AT >= TFW_CM_PLATFORM_BASE + TFW_CM_PLATFORM_SIZE,
               "the factory platform block must be clear of the platform block");

static uint8_t blank(const uint8_t *p, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (p[i] != 0u) {
            return 0u;
        }
    }
    return 1u;
}

void tfw_cm_boot_factory(void)
{
    uint8_t       *slot = (uint8_t *)(uintptr_t)TFW_SWAP_PRIMARY_BASE;
    uint8_t       *plat = (uint8_t *)(uintptr_t)(TFW_CM_PLATFORM_BASE + TFW_BOOT_EE_BASE);
    const uint8_t *fapp = (const uint8_t *)(uintptr_t)TFW_CM_LOAD_APP_AT;
    const uint8_t *fplt = (const uint8_t *)(uintptr_t)(TFW_CM_LOAD_PLATFORM_AT + TFW_BOOT_EE_BASE);

    /* The vector table's first word, the stack top, is never zero in an
     * image; the platform block is blank only when every byte is. */
    if (blank(slot, 4u) != 0u) {
        memcpy(slot, fapp, TFW_SWAP_SLOT_LEN);
    }
    if (blank(plat, TFW_BOOT_EE_LEN) != 0u) {
        memcpy(plat, fplt, TFW_BOOT_EE_LEN);
    }
    tfw_cm_dsb();
}
