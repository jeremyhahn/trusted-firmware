/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bake image's whole decision, kept apart from main.c so the host tests run
 * it through the mock HAL exactly as the part runs it.
 *
 * WRITE THROUGH THE EEPROM, READ THROUGH THE NVM. tfw_hal_nvm_read is the view
 * the firmware's own tfw_platform_id_init and tfw-boot take of the block, and
 * it has no write half on purpose (include/tfw/hal.h). On AVR the two are the
 * same EEPROM, so writing through tfw_hal_eeprom_write and checking through
 * tfw_hal_nvm_read proves the block is where the firmware will look for it,
 * not merely where this image put it.
 */
#include <string.h>

#include "tfw/hal.h"
#include "boot_platform.h"
#include "bake.h"

/* The region as read. Static rather than on the stack: 192 bytes is a tenth of
 * a 328P's SRAM, and this image has no other use for it. */
static uint8_t have[TFW_BOOT_EE_BLOCK_LEN];

tfw_bake_result_t tfw_bake_run(const uint8_t want[TFW_BOOT_EE_BLOCK_LEN])
{
    tfw_boot_platform_t parsed;

    /* Never write a block the bootloader would reject. */
    if (tfw_boot_platform_parse(want, &parsed) != TFW_OK) {
        return TFW_BAKE_REFUSED;
    }

    /* A region that cannot even be read is not known to be blank, so it is
     * not written. */
    if (tfw_hal_nvm_read(TFW_BOOT_EE_BASE, have, TFW_BOOT_EE_BLOCK_LEN) != TFW_OK) {
        return TFW_BAKE_REFUSED;
    }

    /* A valid block is an identity. The same one again is nothing to do; a
     * different one is a device that already has an identity, and replacing it
     * is not a bench decision -- see include/tfw/boot.h on re-baking. */
    if (tfw_boot_platform_parse(have, &parsed) == TFW_OK) {
        return memcmp(have, want, TFW_BOOT_EE_BLOCK_LEN) == 0 ? TFW_BAKE_ALREADY
                                                         : TFW_BAKE_REFUSED;
    }

    /* Blank, torn, a bad CRC or a layout this build does not know: none of
     * them is an identity the firmware would accept, so none is lost. */
    if (tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, want, TFW_BOOT_EE_BLOCK_LEN) != TFW_OK) {
        return TFW_BAKE_READBACK_FAILED;
    }
    if (tfw_hal_nvm_read(TFW_BOOT_EE_BASE, have, TFW_BOOT_EE_BLOCK_LEN) != TFW_OK
        || tfw_boot_platform_parse(have, &parsed) != TFW_OK
        || memcmp(have, want, TFW_BOOT_EE_BLOCK_LEN) != 0) {
        return TFW_BAKE_READBACK_FAILED;
    }
    return TFW_BAKE_BAKED;
}

const char *tfw_bake_result_name(tfw_bake_result_t r)
{
    switch (r) {
    case TFW_BAKE_ALREADY:         return "already";
    case TFW_BAKE_REFUSED:         return "refused";
    case TFW_BAKE_BAKED:           return "baked";
    case TFW_BAKE_READBACK_FAILED: return "readback-failed";
    default:                       return "unknown";
    }
}

static void put_c(char c)
{
    while (!tfw_hal_uart_tx_try((uint8_t)c)) {
    }
}

static void put_s(const char *s)
{
    while (*s != '\0') {
        put_c(*s++);
    }
}

static void put_hex(uint8_t b)
{
    static const char digits[] = "0123456789abcdef";

    put_c(digits[b >> 4]);
    put_c(digits[b & 0x0Fu]);
}

void tfw_bake_report(const char *board, tfw_bake_result_t r,
                     const uint8_t want[TFW_BOOT_EE_BLOCK_LEN])
{
    uint8_t i;

    put_s("platform-bake board=");
    put_s(board);
    put_s(" serial=");
    for (i = 0u; i < TFW_BOOT_SERIAL_LEN; i++) {
        put_hex(want[TFW_BOOT_EE_O_SERIAL + i]);
    }
    put_s(" result=");
    put_s(tfw_bake_result_name(r));
    put_s("\r\n");
}
