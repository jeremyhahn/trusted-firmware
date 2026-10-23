/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <util/crc16.h> for the host, as wide as src/hal/avr/hal_crc16.c needs (#163):
 * avr-libc's three CRC-16 updates, each the C equivalent avr-libc documents
 * for its assembly. All three are here so that a unit calling the wrong one
 * still builds and is caught by its vectors, not by the compiler. Only on the
 * -I of the suite that tests that unit; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_UTIL_CRC16_H
#define TFW_TEST_UTIL_CRC16_H

#include <stdint.h>

/* Reflected 0xA001 (CRC-16/ARC). */
static inline uint16_t _crc16_update(uint16_t crc, uint8_t a)
{
    unsigned i;

    crc ^= a;
    for (i = 0u; i < 8u; i++) {
        crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

/* MSB-first 0x1021: XMODEM from 0x0000, CCITT-FALSE from 0xFFFF. */
static inline uint16_t _crc_xmodem_update(uint16_t crc, uint8_t data)
{
    unsigned i;

    crc = (uint16_t)(crc ^ ((uint16_t)data << 8));
    for (i = 0u; i < 8u; i++) {
        crc = (uint16_t)((crc & 0x8000u) ? (((unsigned)crc << 1) ^ 0x1021u) : ((unsigned)crc << 1));
    }
    return crc;
}

/* Reflected 0x8408 (KERMIT / X.25 order). */
static inline uint16_t _crc_ccitt_update(uint16_t crc, uint8_t data)
{
    data = (uint8_t)(data ^ (crc & 0xFFu));
    data = (uint8_t)(data ^ (data << 4));
    return (uint16_t)((((uint16_t)data << 8) | (crc >> 8)) ^ (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
}

#endif
