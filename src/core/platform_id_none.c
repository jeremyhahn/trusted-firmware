/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-platform-block stub, linked instead of platform_id.c on a board with
 * no boot section -- see handoff_none.c, its reasoning is the same: no
 * bootloader means no reserved EEPROM layout for anything to have baked a
 * serial into.
 *
 * main.c calls ag_platform_id_init on every board; ag_platform_id_serial is
 * reached only from src/caps/attest.c, which this board's feature set already
 * compiles out (AG_FEATURE_ATTEST is 0 on a board that links this file), so
 * --gc-sections drops it and the zero buffer behind it rather than either
 * costing this part anything.
 */
#include <stdint.h>
#include "platform_id.h"
#include "ag/boot.h"

void ag_platform_id_init(void) {}

const uint8_t *ag_platform_id_serial(void)
{
    static const uint8_t zero[AG_BOOT_SERIAL_LEN] = {0};
    return zero;
}
