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
#include "tfw/types.h"

/* Writes the TFW_BOOT_REQUEST_LEN-byte request into `region`. */
void tfw_boot_request_encode(uint8_t *region);

/*
 * 1 when `region` holds a well-formed request AND `cause` -- tfw-boot's own
 * observation of why the part reset, from the boot HAL -- is TFW_RESET_WATCHDOG.
 * 0 for everything else: no request, garbage, a request left over from a power
 * cycle. The HAL reports a watchdog reset only when nothing else also reset the
 * part (src/hal/avr/hal_reset_decode.h), so "watchdog" here means exactly what
 * the gate has always meant: only the application's own RESET opens a session.
 */
uint8_t tfw_boot_request_valid(const uint8_t *region, tfw_reset_cause_t cause);

/*
 * tfw-boot only. Reads the request out of the handoff region, CLEARS it
 * unconditionally and returns whether it was valid. Clearing first and
 * deciding second is the one-shot guarantee: nothing the caller does next can
 * leave the request armed for the following reset.
 */
uint8_t tfw_boot_request_take(tfw_reset_cause_t cause);

#endif /* TFW_BOOT_REQUEST_H */
