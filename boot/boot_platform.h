/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The platform-reserved EEPROM region, parsed and serialised.
 *
 * Split from the HAL that reads it so that the LAYOUT is host-testable without
 * an EEPROM. The interesting cases here are all corruption cases -- a bad CRC,
 * a layout version from the future, a region that was never written -- and none
 * of them is convenient to produce on a bench.
 */
#ifndef AG_BOOT_PLATFORM_H
#define AG_BOOT_PLATFORM_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

/* Parses AG_BOOT_EE_LEN bytes.
 *
 *   AG_ERR_NOT_PROVISIONED  the magic is absent -- a blank or foreign region
 *   AG_ERR_UNSUPPORTED      a layout version this build does not know
 *   AG_ERR_CRC              the CRC does not match the contents
 *
 * The three are distinguished on purpose. "Never provisioned" and "provisioned
 * and since corrupted" call for opposite responses: the first is a device that
 * has not been through the factory, the second is a device whose supply sagged
 * during an EEPROM write, which docs/security/fuses-and-lockbits.md names as
 * the reason BODLEVEL is a security setting here. A bootloader that reported
 * both as "not provisioned" would send a field failure back to the factory.
 */
ag_status_t ag_boot_platform_parse(const uint8_t *raw, ag_boot_platform_t *out);

/* Serialises, computing the CRC. Writes exactly AG_BOOT_EE_LEN bytes.
 *
 * `raw` is read as well as written: the reserved bytes and the device serial
 * are preserved from what is already there rather than zeroed, so the
 * bootloader recording a boot count cannot erase what agprov wrote. */
void ag_boot_platform_serialise(const ag_boot_platform_t *in, uint8_t *raw);

/* The CRC over bytes [0, AG_BOOT_EE_O_CRC). Exposed because agprov must compute
 * the same value, and a CRC that is wrong the same way on both sides is a CRC
 * that checks nothing. */
uint16_t ag_boot_platform_crc(const uint8_t *raw);

#endif /* AG_BOOT_PLATFORM_H */
