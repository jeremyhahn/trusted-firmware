/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * DICE layer 0: the CDI, and the two ways this family can produce one.
 *
 * Substituted at LINK time, exactly as include/ag/crypto.h is and for the same
 * reasons -- one implementation per image, no vtable, no runtime selection. An
 * image built for the software tier cannot be talked into using an element it
 * does not have, and a boot section with 4 KB in it cannot afford to carry both.
 *
 *   boot/tier/cdi_soft.c    BOOT_TIER=soft      HMAC in software, UDS in
 *                           boot-section flash behind BLB mode 3
 *   boot/tier/cdi_atecc.c   BOOT_TIER=atecc608a HMAC inside the part, the UDS
 *                           never crossing the bus
 *
 * Both compute HMAC(UDS, H(application)), which is form (2) of DICE HW
 * Requirements r78 §5 -- the stronger of the two, because an HMAC's security is
 * the full width of its key where a bare hash's is half its digest.
 */
#ifndef AG_BOOT_CDI_H
#define AG_BOOT_CDI_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

/* Prepares whatever the tier needs. Returns AG_OK on a tier that is present but
 * unprovisioned: a device with no identity still boots, it simply cannot
 * attest, and refusing to boot would turn a factory step somebody skipped into
 * a brick. */
ag_status_t ag_boot_cdi_init(void);

/* What this build's root of trust actually is, for the tier byte of the handoff
 * block. The application reports it in CAPS, and a host decides what evidence
 * is worth BEFORE it accepts any -- so a bootloader that overstated it here
 * would defeat the ordering the whole scheme depends on. */
uint8_t ag_boot_cdi_tier(void);

/*
 * CDI = HMAC(UDS, measurement).
 *
 * Returns AG_ERR_NOT_PROVISIONED when there is no UDS, and the bootloader then
 * hands the application a handoff block it will refuse -- rather than a CDI
 * derived from a default secret that every build of this source would share.
 * Evidence signed under such a key proves nothing while looking exactly like
 * evidence that proves something, which is the failure src/crypto/soft/
 * provider.c already refuses and this must not reintroduce one layer down.
 */
ag_status_t ag_boot_cdi_derive(const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                               uint8_t cdi[AG_BOOT_MEASURE_LEN]);

/*
 * The anti-rollback floor, read and advanced.
 *
 * On the element tier this is the part's monotonic counter, which is authority
 * on its own -- it cannot be made to descend by anything, including a
 * bootloader with a bug. Without an element the floor is in reserved EEPROM,
 * where it is only as monotonic as the write that maintains it, which is why
 * BODLEVEL is a security setting on this part.
 *
 * `floor_read` returns AG_ERR_UNSUPPORTED on a tier that keeps no floor of its
 * own, and the caller then falls back to the EEPROM copy. That is a tier
 * capability question and not a fallback in the disallowed sense: the two tiers
 * genuinely store the floor in two different places, and the one that has a
 * counter must use it.
 */
ag_status_t ag_boot_floor_read(uint32_t *out);
ag_status_t ag_boot_floor_write(uint32_t value);

#endif /* AG_BOOT_CDI_H */
