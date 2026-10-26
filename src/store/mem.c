/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OTA store as a span of memory the part's reset leaves alone (issue #75's
 * contract, src/core/ota_store.h): OTA_STORE := mem in a board's profile.mk.
 *
 * For a board with no NOR part and a RAM that keeps its contents across the
 * reset the update cycle uses -- the emulated MPS2 boards, whose machine RAM
 * QEMU neither loads nor clears on system_reset (docs/cortex-m/README.md, "The
 * image store"). The span is board data: TFW_OTA_STORE_MEM_BASE and
 * TFW_OTA_STORE_MEM_SIZE in board_profile.h, and the store's addresses are
 * offsets into it, so the layout (TFW_OTA_STORE_*) reads as it does on a NOR.
 *
 * It keeps a NOR's rules, because the stager and tfw-boot are written to
 * them: an erase sets a whole 4 KB sector to 0xFF, and a write only clears
 * bits (each byte becomes old AND new), never sets one. A store that let a
 * write set bits would pass a stager that forgot an erase, and the same image
 * would then fail on every board with a real part.
 *
 * tfw-boot reads the same span through its own three opcodes,
 * boot/hal/<arch>/boot_ota_store_<arch>.c.
 */
#include <stdint.h>
#include <string.h>
#include "tfw/config.h"
#include "../core/ota_store.h"

#define MEM  ((uint8_t *)(uintptr_t)(TFW_OTA_STORE_MEM_BASE))
#define SIZE ((uint32_t)(TFW_OTA_STORE_MEM_SIZE))

_Static_assert((TFW_OTA_STORE_MEM_SIZE) % TFW_STORE_SECTOR == 0u,
               "the store is a whole number of erase sectors");

static uint8_t inside(uint32_t addr, uint32_t len)
{
    return (uint8_t)(addr <= SIZE && len <= SIZE - addr);
}

tfw_status_t tfw_store_init(void)
{
    /* No select to park and no bus to take. */
    return TFW_OK;
}

tfw_status_t tfw_store_probe(void)
{
    /* The span is memory the board always has: there is no part to be absent. */
    return TFW_OK;
}

tfw_status_t tfw_store_erase(uint32_t addr)
{
    uint32_t base = addr - (addr % (uint32_t)TFW_STORE_SECTOR);

    if (!inside(base, (uint32_t)TFW_STORE_SECTOR)) {
        return TFW_ERR_RANGE;
    }
    memset(MEM + base, 0xFF, (size_t)TFW_STORE_SECTOR);
    return TFW_OK;
}

tfw_status_t tfw_store_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    if (len > TFW_STORE_CHUNK || !inside(addr, len)) {
        return TFW_ERR_RANGE;
    }
    memcpy(buf, MEM + addr, len);
    return TFW_OK;
}

tfw_status_t tfw_store_write(uint32_t addr, const uint8_t *src, uint8_t len)
{
    uint8_t i;

    if (len > TFW_STORE_CHUNK || !inside(addr, len)) {
        return TFW_ERR_RANGE;
    }
    for (i = 0u; i < len; i++) {
        MEM[addr + i] = (uint8_t)(MEM[addr + i] & src[i]);
    }
    return TFW_OK;
}
