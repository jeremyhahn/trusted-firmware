/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-platform-block stub, linked instead of platform_id.c on a board with
 * no boot section -- see handoff_none.c, its reasoning is the same: no
 * bootloader means no reserved EEPROM layout for anything to have baked a
 * serial into.
 *
 * main.c calls tfw_platform_id_init on every board; tfw_platform_id_serial is
 * reached only from src/caps/attest.c, which this board's feature set already
 * compiles out (TFW_FEATURE_ATTEST is 0 on a board that links this file), so
 * --gc-sections drops it and the zero buffer behind it rather than either
 * costing this part anything.
 */
#include <stdint.h>
#include <string.h>
#include "platform_id.h"
#include "tfw/boot.h"

void tfw_platform_id_init(void) {}

const uint8_t *tfw_platform_id_serial(void)
{
    static const uint8_t zero[TFW_BOOT_SERIAL_LEN] = {0};
    return zero;
}

void tfw_platform_id_operator_pubkey(uint8_t out[TFW_BOOT_OPERATOR_PUBKEY_LEN])
{
    memset(out, 0, TFW_BOOT_OPERATOR_PUBKEY_LEN);
}
