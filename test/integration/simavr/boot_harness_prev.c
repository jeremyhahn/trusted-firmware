/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The harness's half that names the store's PREV (issue #52): only a board
 * whose profile has one, the 328P and the 32U4, links it (mk/boot.mk); every
 * other board, the 2560 first, falls back to its secondary slot and links
 * boot_harness_prev_none.c, so its profile is never asked for a PREV.
 */
#include <stdint.h>

#include "tfw/config.h"

long boot_harness_prev_addr(void);

long boot_harness_prev_addr(void)
{
    return (long)TFW_OTA_STORE_PREV_ADDR;
}
