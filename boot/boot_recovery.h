/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RECOVERY: what the bootloader does when there is nothing it may boot.
 *
 * One function, substituted at link time, and it never returns.
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
 * The seam is a function that does not return, because every caller reaches it
 * having established that no image may run -- and a recovery that could return
 * would give a caller somewhere to go afterwards, which there is not.
 */
#ifndef AG_BOOT_RECOVERY_H
#define AG_BOOT_RECOVERY_H

#include "ag/status.h"
#include "boot_platform.h"
#include "boot_policy.h"

/* Called when nothing may be booted. Does not return. `reason` is the status
 * that got us here, reported to a host where one is listening. */
void ag_boot_recovery(ag_boot_platform_t *plat, ag_status_t reason);

/* Derives the CDI, hands it over and jumps into the application. Does not
 * return. Declared here because the OTA recovery calls it too, after an image
 * it received has verified -- and a second copy of the handoff would be a
 * second chance to get the one sequence wrong that the CDI depends on. */
void ag_boot_hand_over(const ag_boot_verdict_t *v);

#endif /* AG_BOOT_RECOVERY_H */
