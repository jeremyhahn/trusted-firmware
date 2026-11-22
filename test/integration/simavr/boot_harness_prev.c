/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The harness's half that names the store's PREV (issue #52): only a one-slot
 * board with a store and BOOT_PREV=1, the 32U4, links it (mk/boot.mk); the
 * 2560 and the 328P link boot_harness_prev_none.c, so their profiles are
 * never asked for a PREV.
 */
#include <stdint.h>

#include "tfw/config.h"

long boot_harness_prev_addr(void);

long boot_harness_prev_addr(void)
{
    return (long)TFW_OTA_STORE_PREV_ADDR;
}
