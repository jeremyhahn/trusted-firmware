/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RECOVERY: what the bootloader does when there is nothing it may boot.
 *
 * Two functions, substituted at link time. tfw_boot_recovery never returns;
 * tfw_boot_session_open, below, is the same loop entered on purpose by a healthy
 * device and bounded.
 *
 * Two implementations exist because the boot section is 4 KB on most of this
 * family and 8 KB on one part, and the OTA path is the largest thing in the
 * bootloader after the hash. It is also the only part that is not strictly
 * necessary there: an application can stage an image and reboot into a
 * bootloader that only measures, verifies and commits.
 *
 *   recovery/recovery_ota.c    serves a framed OTA session. The device stays
 *                              reachable and a new image can be sent.
 *   recovery/recovery_halt.c   stops. Recovery needs a programmer.
 *
 * tfw_boot_recovery does not return, because every caller reaches it having
 * established that no image may run -- and a recovery that could return would
 * give a caller somewhere to go afterwards, which there is not.
 */
#ifndef TFW_BOOT_RECOVERY_H
#define TFW_BOOT_RECOVERY_H

#include "tfw/status.h"
#include "boot_platform.h"
#include "boot_policy.h"

/* Called when nothing may be booted. Does not return. `reason` is the status
 * that got us here, reported to a host where one is listening. */
void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason);

/* Derives the CDI, hands it over and jumps into the application. Does not
 * return. Declared here because the OTA recovery calls it too, after an image
 * it received has verified -- and a second copy of the handoff would be a
 * second chance to get the one sequence wrong that the CDI depends on. */
typedef struct {
    uint32_t idle_polls;
    uint32_t idle_bytes;
    uint32_t max_polls;
    uint32_t max_bytes;
} tfw_boot_session_limits_t;

/*
 * Opens an OTA session at the application's request and RETURNS when it ends
 * without a commit, so the caller falls through to the normal measure, verify
 * and jump. A completed OTA_END that verifies hands over and never returns; a
 * RESET command resets.
 *
 * Four bounds, any of which ends it (0 disables one):
 *
 *   idle_polls / idle_bytes   quiet-line and busy-line budgets; restart on an
 *                             accepted OTA_BEGIN, OTA_DATA or OTA_END
 *   max_polls / max_bytes     a CEILING on the whole session that nothing
 *                             restarts: accepted frames are well-formed, not
 *                             authenticated, so progress cannot be trusted to
 *                             reset a bound. A drip meets max_polls, a flood
 *                             meets max_bytes.
 *
 * It is also ONE ATTEMPT: the first OTA_END that does not verify ends it. PING
 * and OTA_STATUS answer but extend nothing. There is no timer and no delay, so
 * the units are loop iterations and bytes; main.c converts durations.
 *
 * What returning means: the caller runs the ordinary measure, verify and jump.
 * If the attempt had already overwritten application pages (single slot; see
 * boot_ota.h) that fails and the device lands in tfw_boot_recovery, which is
 * unbounded. A BOOT_OTA=0 image has no loop and returns at once.
 */
void tfw_boot_session_open(tfw_boot_platform_t *plat,
                          const tfw_boot_session_limits_t *lim);

void tfw_boot_hand_over(const tfw_boot_verdict_t *v);

#endif /* TFW_BOOT_RECOVERY_H */
