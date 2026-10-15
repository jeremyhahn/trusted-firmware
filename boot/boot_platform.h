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
#ifndef TFW_BOOT_PLATFORM_H
#define TFW_BOOT_PLATFORM_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/* Parses TFW_BOOT_EE_LEN bytes.
 *
 *   TFW_ERR_NOT_PROVISIONED  the magic is absent -- a blank or foreign region
 *   TFW_ERR_UNSUPPORTED      a layout version this build does not know
 *   TFW_ERR_CRC              the CRC does not match the contents
 *
 * The three are distinguished on purpose. "Never provisioned" and "provisioned
 * and since corrupted" call for opposite responses: the first is a device that
 * has not been through the factory, the second is a device whose supply sagged
 * during an EEPROM write, which docs/security/fuses-and-lockbits.md names as
 * the reason BODLEVEL is a security setting here. A bootloader that reported
 * both as "not provisioned" would send a field failure back to the factory.
 */
tfw_status_t tfw_boot_platform_parse(const uint8_t *raw, tfw_boot_platform_t *out);

/* Serialises, computing the CRC. Writes exactly TFW_BOOT_EE_LEN bytes.
 *
 * `raw` is read as well as written: the reserved bytes and the device serial
 * are preserved from what is already there rather than zeroed, so the
 * bootloader recording a boot count cannot erase what tfwprov wrote. */
void tfw_boot_platform_serialise(const tfw_boot_platform_t *in, uint8_t *raw);

/* The CRC over bytes [0, TFW_BOOT_EE_O_CRC). Exposed because tfwprov must compute
 * the same value, and a CRC that is wrong the same way on both sides is a CRC
 * that checks nothing. */
uint16_t tfw_boot_platform_crc(const uint8_t *raw);

/* Little-endian field access, for every boot-side layout: the platform block,
 * the image trailer and the update request. One copy, here, because each file
 * keeping its own left three identical copies in a boot section with no room
 * for them; boot_platform.c is in every link that has the other two. */
uint32_t tfw_boot_rd_u32(const uint8_t *p);
uint16_t tfw_boot_rd_u16(const uint8_t *p);
void     tfw_boot_wr_u32(uint8_t *p, uint32_t v);
void     tfw_boot_wr_u16(uint8_t *p, uint16_t v);

#endif /* TFW_BOOT_PLATFORM_H */
