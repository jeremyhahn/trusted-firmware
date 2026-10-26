/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The boot ROM's function lookup on the RP2350 (rp_flash.h): the one piece of
 * rp_flash_rom.c's path the host cannot run, so the host test of that path
 * supplies a ROM table model in its place.
 */

#include <stdint.h>

#include "rp_flash.h"

typedef uintptr_t (*rp_rom_lookup_fn)(uint32_t code, uint32_t mask);

/* The halfword at TFW_RP_ROM_LOOKUP_PTR, read with an explicit load: a
 * constant address this low is one the compiler takes for a null pointer. */
uintptr_t tfw_rp_rom_func(uint32_t code)
{
    uint32_t lookup;

    __asm__ volatile ("ldrh %0, [%1]" : "=r"(lookup) : "r"(TFW_RP_ROM_LOOKUP_PTR) : "memory");
    return ((rp_rom_lookup_fn)(uintptr_t)lookup)(code, TFW_RP_RT_FLAG_FUNC_ARM_SEC);
}
