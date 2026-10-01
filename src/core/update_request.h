/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the update-session request -- RESET with mode 1.
 *
 * Which of the two implementations is linked is decided by mk/avr.mk, by the
 * same RAM_END gate that picks src/core/handoff.c over handoff_none.c: a board
 * with no boot section has no ag-boot to ask, and no fixed SRAM block to ask it
 * through. The layout of the request and why it lives where it does are in
 * include/ag/boot.h.
 */
#ifndef AG_CORE_UPDATE_REQUEST_H
#define AG_CORE_UPDATE_REQUEST_H

#include <stdint.h>

/* 1 when this build can leave a request for ag-boot; 0 on a board with no
 * boot section. RESET(1) is refused with AG_ERR_UNSUPPORTED on those. */
uint8_t ag_update_request_supported(void);

/* Leaves the request in the handoff region. Called immediately before the
 * watchdog reset, not when RESET(1) is received, so a RESET(0) that arrives
 * in between cannot turn into a bootloader entry it never asked for. */
void ag_update_request_arm(void);

#endif /* AG_CORE_UPDATE_REQUEST_H */
