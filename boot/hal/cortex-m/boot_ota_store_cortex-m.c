/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OTA store for tfw-boot on Cortex-M -- the store section of
 * boot/boot_hal.h -- over the memory span src/store/mem.c gives the
 * application (TFW_OTA_STORE_MEM_BASE, TFW_OTA_STORE_MEM_SIZE). Linked only
 * where the board names an OTA_STORE (mk/boot.mk, BOOT_STORE).
 *
 * boot/boot_stage.c speaks SPI NOR opcodes to the store, so this answers the
 * five it sends, with a NOR's rules: JEDEC ID, status (never busy), write
 * enable, read, and page program, which only clears bits and needs a write
 * enable first, as the part's does. Anything else is refused, not ignored: an
 * opcode tfw-boot starts to send that this store does not model would
 * otherwise pass here and fail on every board with a real part.
 *
 * The JEDEC ID is a fixed one no part answers with (manufacturer 0x7F is the
 * JEP106 continuation code, never a first byte): boot_stage only refuses
 * 0x00 and 0xFF, the reads of an absent part.
 */
#include <stdint.h>
#include <string.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "../../boot_hal.h"

#define MEM  ((uint8_t *)(uintptr_t)(TFW_OTA_STORE_MEM_BASE))
#define SIZE ((uint32_t)(TFW_OTA_STORE_MEM_SIZE))

#define OP_PROGRAM 0x02u
#define OP_READ    0x03u
#define OP_STATUS  0x05u
#define OP_WREN    0x06u
#define OP_JEDEC   0x9Fu

static const uint8_t jedec[3] = { 0x7Fu, 0x4Du, 0x45u };

static uint8_t wel;     /* the write-enable latch: set by WREN, spent by a program */

static uint32_t addr24(const uint8_t *cmd)
{
    return ((uint32_t)cmd[1] << 16) | ((uint32_t)cmd[2] << 8) | (uint32_t)cmd[3];
}

static uint8_t inside(uint32_t addr, uint32_t len)
{
    return (uint8_t)(addr <= SIZE && len <= SIZE - addr);
}

tfw_status_t tfw_boot_store_open(void)
{
    wel = 0u;
    return TFW_OK;
}

tfw_status_t tfw_boot_store_cmd(const uint8_t *cmd, uint8_t clen, uint8_t *in, uint16_t ilen)
{
    uint32_t a;
    uint32_t i;

    if (clen == 0u) {
        return TFW_ERR_RANGE;
    }
    switch (cmd[0]) {
    case OP_JEDEC:
        if (clen != 1u || ilen > sizeof jedec) {
            return TFW_ERR_RANGE;
        }
        memcpy(in, jedec, ilen);
        return TFW_OK;
    case OP_STATUS:
        if (clen != 1u) {
            return TFW_ERR_RANGE;
        }
        memset(in, 0, ilen);    /* WIP clear: a write here is done when it returns */
        return TFW_OK;
    case OP_WREN:
        if (clen != 1u || ilen != 0u) {
            return TFW_ERR_RANGE;
        }
        wel = 1u;
        return TFW_OK;
    case OP_READ:
        if (clen != 4u) {
            return TFW_ERR_RANGE;
        }
        a = addr24(cmd);
        if (!inside(a, ilen)) {
            return TFW_ERR_RANGE;
        }
        memcpy(in, MEM + a, ilen);
        return TFW_OK;
    case OP_PROGRAM:
        if (clen < 4u || ilen != 0u || wel == 0u) {
            return TFW_ERR_RANGE;
        }
        a = addr24(cmd);
        if (!inside(a, (uint32_t)clen - 4u)) {
            return TFW_ERR_RANGE;
        }
        for (i = 0u; i < (uint32_t)clen - 4u; i++) {
            MEM[a + i] = (uint8_t)(MEM[a + i] & cmd[4u + i]);
        }
        wel = 0u;
        return TFW_OK;
    default:
        return TFW_ERR_RANGE;
    }
}

void tfw_boot_store_close(void)
{
    wel = 0u;
}
