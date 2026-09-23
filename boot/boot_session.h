/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's command surface: four commands and no more.
 *
 * Deliberately not src/core/dispatch.c. That dispatcher carries a PROGMEM table,
 * per-command arity bounds and a debug name table, all of which earn their keep
 * across forty application commands and none of which does across four. What it
 * would cost here is flash in the one section that has none to spare.
 *
 * The four are the smallest set that makes an update possible and an
 * interrupted one recoverable. There is no read command, no "jump to address",
 * and no way to ask the bootloader to do anything to the boot section: a
 * bootloader that could rewrite itself over the wire is a bootloader an
 * attacker can replace over the wire, and there is no key rotation story that
 * survives that.
 */
#ifndef AG_BOOT_SESSION_H
#define AG_BOOT_SESSION_H

#include <stdint.h>
#include "ag/protocol.h"
#include "ag/status.h"
#include "boot_ota.h"

/* What handling a command decided the session should do next. The loop owns the
 * consequences -- committing, resetting, jumping -- because those touch
 * hardware and this file must not. */
typedef enum {
    AG_BOOT_SESSION_CONTINUE = 0,
    AG_BOOT_SESSION_COMMIT,   /* OTA_END succeeded: verify and commit */
    AG_BOOT_SESSION_RESET
} ag_boot_session_action_t;

/*
 * Handles one decoded request.
 *
 * `payload`/`plen` are the request payload; `resp`/`rlen` receive the response
 * payload, which is at most AG_MAX_PAYLOAD. Returns the status for the response
 * frame and writes the action the loop must take.
 *
 * Arity is checked HERE, per command, rather than by a table. With four
 * commands the table would be larger than the checks.
 */
ag_status_t ag_boot_session_handle(ag_ota_t *ota,
                                   uint8_t cmd,
                                   const uint8_t *payload, uint8_t plen,
                                   uint8_t *resp, uint8_t *rlen,
                                   ag_boot_session_action_t *action);

#endif /* AG_BOOT_SESSION_H */
