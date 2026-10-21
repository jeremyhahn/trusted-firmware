/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft root's DeviceID seed tfw-boot handed over (handoff version 4,
 * include/tfw/boot.h), kept for the life of the image. Linked beside handoff.c
 * only where the P-256 DeviceID that reads it is (mk/firmware.mk); every other
 * image links handoff_seed_none.c and keeps nothing, which is what put the
 * ATmega32U4 element row back under its flash budget (#173).
 */
#include <string.h>

#include "handoff.h"

static uint8_t seed[TFW_BOOT_SEED_LEN];

uint8_t *tfw_boot_handoff_seed_store(void)
{
    return seed;
}

uint8_t tfw_boot_handoff_device_seed(uint8_t out[TFW_BOOT_SEED_LEN])
{
    uint8_t i, acc = 0u;

    /* All zero is tfw-boot's "held no UDS" signal, and what init leaves on a
     * handoff that failed to parse: not a seed. */
    for (i = 0u; i < sizeof seed; i++) {
        acc |= seed[i];
    }
    if (acc == 0u) {
        return 0u;
    }
    memcpy(out, seed, sizeof seed);
    return 1u;
}
