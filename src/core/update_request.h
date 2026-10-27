/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the update-session request -- RESET with mode 1.
 *
 * Which implementation is linked is decided by mk/firmware.mk, by the same
 * RAM_END gate that picks src/core/handoff.c over handoff_none.c: a board with
 * no boot section has no tfw-boot to ask, and no fixed SRAM block to ask it
 * through. On the A/B part (BOOT_AB, the 2560) an image that stages into the
 * external store links update_request_store.c instead: each image takes one
 * update path (#52), and the store's tfw-boot opens no session to ask for. The
 * layout of the request and why it lives where it does are in
 * include/tfw/boot.h.
 */
#ifndef TFW_CORE_UPDATE_REQUEST_H
#define TFW_CORE_UPDATE_REQUEST_H

#include <stdint.h>

/* 1 when this build has a tfw-boot that serves an update path, whichever it is;
 * 0 on a board with no boot section. An application's OTA slots count only
 * where it is 1 (src/core/app_runner.c). */
uint8_t tfw_update_request_supported(void);

/* 1 when RESET(1) reaches an update session in tfw-boot; 0 on a board with no
 * boot section, and on an image whose update path is the external store. RESET(1)
 * is refused with TFW_ERR_UNSUPPORTED where it is 0. */
uint8_t tfw_update_request_session(void);

/* Leaves the request in the handoff region. Called immediately before the
 * watchdog reset, not when RESET(1) is received, so a RESET(0) that arrives
 * in between cannot turn into a bootloader entry it never asked for. */
void tfw_update_request_arm(void);

#endif /* TFW_CORE_UPDATE_REQUEST_H */
