/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's READ-ONLY view of the platform-reserved EEPROM block --
 * issue #6.
 *
 * This links boot/boot_platform.c's ag_boot_platform_parse -- the decode half
 * of the same layout agprov writes and the bootloader also reads -- and calls
 * it over bytes fetched with the application's own ag_hal_eeprom_read. It does
 * not link, and has no way to reach, ag_boot_platform_serialise: an
 * application able to write this block back would be an application able to
 * forge its own device_serial, which is the exact hole
 * src/caps/storage.c's reserved-region refusal exists to close.
 */
#ifndef AG_CORE_PLATFORM_ID_H
#define AG_CORE_PLATFORM_ID_H

#include <stdint.h>
#include "ag/boot.h"

void ag_platform_id_init(void);

/* AG_BOOT_SERIAL_LEN zero bytes when the region is unprovisioned or
 * corrupted -- zero-rather-than-fabricated, the same rule ag_attest_device_id
 * has always followed. */
const uint8_t *ag_platform_id_serial(void);

/* Fills out[AG_BOOT_OPERATOR_PUBKEY_LEN] -- issue #4's AUTH_CHALLENGE/
 * AUTH_RESPONSE trust anchor. All-zero under the same conditions
 * ag_platform_id_serial() reports zero for (unprovisioned, corrupted, or a
 * layout version this build does not know); an all-zero point is not a valid
 * P-256 public key, so ag_crypto_verify_operator refuses rather than a
 * zeroed anchor ever appearing to accept a signature.
 *
 * UNLIKE serial, THIS IS NOT CACHED AT BOOT: it re-reads and re-parses the
 * whole platform block from EEPROM on every call, trading a few hundred
 * bytes of transient stack for 64 fewer bytes of permanent RAM. That is the
 * right trade here specifically because it is read once per authentication
 * attempt, never on a hot path the way serial is (every ATTEST, every alias
 * certificate). */
void ag_platform_id_operator_pubkey(uint8_t out[AG_BOOT_OPERATOR_PUBKEY_LEN]);

#endif /* AG_CORE_PLATFORM_ID_H */
