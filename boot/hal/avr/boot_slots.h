/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The A/B slot geometry of the 2560 (BOOT_AB=1), boot-HAL data as on the
 * Cortex-M port (boot/hal/cortex-m/boot_slots.h). boot/boot_swap.c reads the
 * slots here and nowhere else.
 *
 * The values stay the 2560 profile's TFW_SWAP_* (boards/atmega2560/
 * board_profile.h): its application region and write end are sized from them,
 * so the profile is where they are defined and this only names them. A board
 * without a second slot never expands these.
 */
#ifndef TFW_BOOT_SLOTS_H
#define TFW_BOOT_SLOTS_H

#include "tfw/config.h"

#define TFW_BOOT_SLOT_UNIT      TFW_SWAP_UNIT
#define TFW_BOOT_SLOT_LEN       TFW_SWAP_SLOT_LEN
#define TFW_BOOT_SLOT_PRIMARY   TFW_SWAP_PRIMARY_BASE
#define TFW_BOOT_SLOT_SECONDARY TFW_SWAP_SECONDARY_BASE
#define TFW_BOOT_SLOT_END       (TFW_SWAP_SECONDARY_BASE + TFW_SWAP_SLOT_LEN)

#endif /* TFW_BOOT_SLOTS_H */
