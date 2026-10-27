/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The A/B slot geometry of a two-slot Cortex-M board (BOOT_AB=1, mk/boot.mk's
 * MPS2 A/B rows), boot-HAL data (OTA ADR, "Cortex-M tfw-boot ports"; #52).
 * boot/boot_swap.c and boot_factory.c read the slots here and nowhere else.
 *
 * The primary is the application's own region: it links and runs there
 * (profile.mk's APP_BASE, TFW_BOOT_APP_BASE in include/tfw/boot.h), so the
 * board profile names it once and this does not. The secondary follows the
 * primary one move unit past its end, as swap-move needs (boot/boot_swap.c):
 *
 *   primary     TFW_BOOT_APP_BASE           slot + unit
 *   secondary   primary + slot + unit       slot, TFW_OTA_STAGE_DEST
 *   write end   secondary + slot            TFW_APP_WRITE_END
 *
 * The board profile's TFW_OTA_STAGE_DEST and TFW_APP_WRITE_END on these rows
 * are the two names below, so a tfw-boot unit that expands either includes
 * this header. The application expands neither.
 */
#ifndef TFW_BOOT_SLOTS_H
#define TFW_BOOT_SLOTS_H

#include "tfw/boot.h"
#include "tfw/config.h"

/* 512 bytes, 256 units to a 128 KiB slot: the swap record's step is a byte. */
#define TFW_BOOT_SLOT_UNIT      512u
#define TFW_BOOT_SLOT_LEN       ((uint32_t)TFW_APP_REGION_SIZE)
#define TFW_BOOT_SLOT_PRIMARY   TFW_BOOT_APP_BASE
#define TFW_BOOT_SLOT_SECONDARY (TFW_BOOT_SLOT_PRIMARY + TFW_BOOT_SLOT_LEN + TFW_BOOT_SLOT_UNIT)
#define TFW_BOOT_SLOT_END       (TFW_BOOT_SLOT_SECONDARY + TFW_BOOT_SLOT_LEN)

#endif /* TFW_BOOT_SLOTS_H */
