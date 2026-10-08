/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the CDI handoff address.
 *
 * tfw-boot places its handoff block at a FIXED SRAM address it derives from
 * the board's RAM_END (mk/boot.mk's BOOT_HANDOFF_AT), because there is no
 * shared linker script between the two images for it to name a symbol in
 * instead. The application has to land on the identical address by the same
 * arithmetic, or it reads whatever else happens to be at the top of its own
 * SRAM rather than what the bootloader wrote -- silently, since a wrong
 * address still has bytes in it, just not the bootloader's.
 *
 * Uninitialised-but-not-zeroed, exactly as the bootloader's copy is: the C
 * runtime zeroes .bss before main(), and zeroing this before it is read would
 * erase the CDI a moment before the one thing that needs it. The address
 * itself comes from mk/avr.mk's --section-start=.handoff, which uses the SAME
 * formula mk/boot.mk does -- one number, derived once, is the only way the
 * two images cannot drift apart on it.
 *
 * `.handoff` IS NOLOAD, genuinely -- mk/handoff.ld (linked in by both
 * mk/avr.mk and mk/boot.mk) splices `.handoff (NOLOAD) : { *(.handoff) }`
 * into the default script, the same SHT_NOBITS treatment `.bss` already
 * gets. That is what keeps this array's 80 bytes out of every derived
 * image -- the *.hex files, and mk/release.mk's provenance .bin -- rather
 * than an `-R .handoff` repeated at each place one gets produced. See that
 * file for why a fragment rather than a full custom script.
 *
 * A board with no boot section (the ATtiny85) links this file too: nothing
 * ever writes here, so the block reads as whatever cold SRAM or a previous
 * run left behind, and src/core/handoff.c's magic/CRC check treats that the
 * same way it treats a stack that grew into a real one -- refused, not
 * misread.
 */

#include "tfw/boot.h"

/*
 * `.handoff`, a section of its own -- NOT the generic `.noinit` other
 * uninitialised-but-not-zeroed data (src/hal/avr/hal_early.c's
 * tfw_hal_early_flags, notably) also lives in. mk/avr.mk pins THIS section to
 * a fixed address with `--section-start`; pinning the generic `.noinit`
 * output section would move every OTHER object assigned to it there too,
 * which is exactly the collision that shipped once, briefly, in this file's
 * history -- `tfw_hal_early_flags` landing inside this array's 80 bytes and
 * overflowing the region the linker had for it.
 */
static uint8_t handoff[TFW_BOOT_HANDOFF_LEN] __attribute__((section(".handoff")));

uint8_t *tfw_hal_handoff_block(void)
{
    return handoff;
}
