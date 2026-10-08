/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The update-session request: the encoding both images share.
 *
 * Pure functions over a byte layout, like boot_handoff.c, so the application
 * that writes the request and the bootloader that reads it are tested against
 * one codec with no MCU on either side. The layout and the reasoning for it
 * are in include/tfw/boot.h.
 */
#ifndef TFW_BOOT_REQUEST_H
#define TFW_BOOT_REQUEST_H

#include <stdint.h>
#include "tfw/boot.h"

/* Writes the TFW_BOOT_REQUEST_LEN-byte request into `region`. */
void tfw_boot_request_encode(uint8_t *region);

/*
 * 1 when `region` holds a well-formed request AND `mcusr` -- tfw-boot's latch of
 * the reset flags, hal_early.c -- records a watchdog reset with no other cause
 * set. 0 for everything else: no request, garbage, a request left over from a
 * power cycle.
 */
uint8_t tfw_boot_request_valid(const uint8_t *region, uint8_t mcusr);

/*
 * tfw-boot only. Reads the request out of the handoff region, CLEARS it
 * unconditionally and returns whether it was valid. Clearing first and
 * deciding second is the one-shot guarantee: nothing the caller does next can
 * leave the request armed for the following reset.
 */
uint8_t tfw_boot_request_take(uint8_t mcusr);

#endif /* TFW_BOOT_REQUEST_H */
