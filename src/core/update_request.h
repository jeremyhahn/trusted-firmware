/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the update-session request -- RESET with mode 1.
 *
 * Which of the two implementations is linked is decided by mk/avr.mk, by the
 * same RAM_END gate that picks src/core/handoff.c over handoff_none.c: a board
 * with no boot section has no tfw-boot to ask, and no fixed SRAM block to ask it
 * through. The layout of the request and why it lives where it does are in
 * include/tfw/boot.h.
 */
#ifndef TFW_CORE_UPDATE_REQUEST_H
#define TFW_CORE_UPDATE_REQUEST_H

#include <stdint.h>

/* 1 when this build can leave a request for tfw-boot; 0 on a board with no
 * boot section. RESET(1) is refused with TFW_ERR_UNSUPPORTED on those. */
uint8_t tfw_update_request_supported(void);

/* Leaves the request in the handoff region. Called immediately before the
 * watchdog reset, not when RESET(1) is received, so a RESET(0) that arrives
 * in between cannot turn into a bootloader entry it never asked for. */
void tfw_update_request_arm(void);

#endif /* TFW_CORE_UPDATE_REQUEST_H */
