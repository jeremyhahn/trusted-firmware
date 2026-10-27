/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host builds' A/B slot geometry: the host suites and tfwsim take the
 * 2560's place (boards/atmega2560/board_profile.h), so this is
 * boot/hal/avr/boot_slots.h's mapping.
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
