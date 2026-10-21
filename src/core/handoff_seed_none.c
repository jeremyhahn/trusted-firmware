/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * No store for the DeviceID seed: linked beside handoff.c in an image without
 * the P-256 DeviceID, the only reader of the seed (mk/firmware.mk). The NULL
 * tells tfw_boot_handoff_init to keep nothing, and with LTO the seed's copy and
 * zeroize drop out of the image (#173). tfw_boot_handoff_device_seed has no
 * caller here, so it is not defined.
 */
#include <stddef.h>

#include "handoff.h"

uint8_t *tfw_boot_handoff_seed_store(void)
{
    return NULL;
}
