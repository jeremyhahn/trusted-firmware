/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bench bake image (#7): writes the platform identity block tfwprov
 * --bake-eeprom produced into [TFW_BOOT_EE_BASE, TFW_BOOT_EE_BASE +
 * TFW_BOOT_EE_BLOCK_LEN) on a part that still runs a stock serial bootloader, and says
 * on the UART what it did. See docs/device-provisioning/engineering.md.
 *
 * NOT part of any application image. The application refuses EEPROM_WRITE over
 * this span (src/caps/storage.c) and keeps refusing it; this is a separate
 * image, built by `make platform-bake` from its own object list, and
 * `make platform-bake-no-link-check` fails if any of it reaches the firmware.
 */
#ifndef TFW_PLATFORM_BAKE_H
#define TFW_PLATFORM_BAKE_H

#include <stdint.h>

#include "tfw/boot.h"

typedef enum {
    TFW_BAKE_ALREADY         = 0, /* the region already holds this block */
    TFW_BAKE_REFUSED         = 1, /* the region holds a DIFFERENT valid block */
    TFW_BAKE_BAKED           = 2, /* written, read back, parsed and equal */
    TFW_BAKE_READBACK_FAILED = 3  /* written, and what came back is not it */
} tfw_bake_result_t;

/* The block, as tfwprov --bake-eeprom wrote it. Generated into the build
 * directory by mk/platform-bake.mk from tfwprov's own output; never encoded
 * here, so there is one encoder of the block and it is tfwprov. */
extern const uint8_t tfw_bake_block[TFW_BOOT_EE_BLOCK_LEN];

/*
 * Bakes `want` into the region, once.
 *
 *   a valid block equal to want     -> ALREADY, nothing written
 *   a valid block that differs      -> REFUSED, nothing written
 *   anything else (blank, torn,
 *   bad CRC, an unknown layout)     -> written, read back, parsed with
 *                                      tfw_boot_platform_parse and compared:
 *                                      BAKED or READBACK_FAILED
 *
 * A `want` that does not itself parse is REFUSED before the region is read:
 * the image never writes a block the bootloader would reject. Nothing outside
 * the region is ever written.
 */
tfw_bake_result_t tfw_bake_run(const uint8_t want[TFW_BOOT_EE_BLOCK_LEN]);

/* "already", "refused", "baked", "readback-failed"; "unknown" otherwise. */
const char *tfw_bake_result_name(tfw_bake_result_t r);

/* The one result line, on the UART:
 *
 *   platform-bake board=<name> serial=<32 hex digits> result=<name>\r\n
 *
 * The serial is want's, so the line names what the image carries whatever the
 * outcome. Returns once every byte is queued. */
void tfw_bake_report(const char *board, tfw_bake_result_t r,
                     const uint8_t want[TFW_BOOT_EE_BLOCK_LEN]);

#endif /* TFW_PLATFORM_BAKE_H */
