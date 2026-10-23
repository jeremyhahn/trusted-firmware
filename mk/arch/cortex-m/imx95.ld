/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7's images (SPEC row `imx95`): mps2-tz's layout, unchanged.
 *
 * As there, no address is written here: mk/arch/cortex-m.mk passes every one
 * from the board's profile.mk (boards/frdm-imx95-m7/profile.mk holds the TCM
 * map, at the M7's addresses, and the addresses the A55 side sees it at). The
 * code region is the ITCM and .data, .bss, the stack and the handoff block are
 * the DTCM; a loader writes both, so the layout for flash in mps2-tz.ld, with
 * .data's initial values after the code, holds here as well.
 *
 * What only this part needs is the resource table Linux's remoteproc reads
 * when it is the loader (src/hal/cortex-m/imx95/rsc_table.c), checked below.
 */

INCLUDE mk/arch/cortex-m/mps2-tz.ld

/* VTOR takes the table's size rounded up to a power of two as its alignment:
 * 16 + 80 vectors (src/hal/cortex-m/hal_early.c) are 384 bytes, so 512. */
ASSERT((__tfw_text_base & 0x1FF) == 0, "the vector table needs a 512-byte aligned code region")

/* The resource table is an output section of its own, `.resource_table`,
 * because the kernel's ELF loader finds the table by that section name and,
 * after loading, copies it back to the section's address: an address the M7
 * and the loader both reach, so the code region (the ITCM). mps2-tz.ld places
 * the section in its flow, after .ARM.exidx and before .data's initial values,
 * because a section placed from here, after that script's last section, would
 * take an address below ld's location counter, which GNU ld warns of on every
 * link ("dot moved backwards"). Only the application links rsc_table.c
 * (src/hal/cortex-m/imx95/), so a tfw-boot image has no table. */
ASSERT(SIZEOF(.resource_table) == 0 || SIZEOF(.resource_table) >= 16,
       "the resource table is shorter than its 16-byte header")
ASSERT(SIZEOF(.resource_table) == 0 || (ADDR(.resource_table) & 3) == 0,
       "the resource table's u32 words need a 4-byte aligned address")
