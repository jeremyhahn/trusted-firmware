/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "crc16.h"

/*
 * A thin loop over tfw_crc16_update(). It stays in this file, portable and
 * unspecialised, in every build: it is called once per frame rather than once
 * per byte inside a state machine, so it is not the cost #12 measured, and it
 * has nowhere else to be substituted from that would mean anything -- see
 * crc16.h for where the per-byte fold actually lives.
 */
uint16_t tfw_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = TFW_CRC16_INIT;
    uint16_t i;

    for (i = 0; i < len; i++) {
        crc = tfw_crc16_update(crc, buf[i]);
    }
    return crc;
}
