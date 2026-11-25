/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 secure/non-secure split of mps2-an505 as QEMU models it (#590).
 * The SSE-200's IDAU makes address bit 28 the alias bit:
 * 0x1xxxxxxx and 0x3xxxxxxx are Secure, 0x0xxxxxxx and 0x2xxxxxxx their
 * Non-secure aliases of the same RAM. The SAU narrows that to the regions
 * below. Plain integer macros only: tz_s.ld and tz_ns.ld are run through the
 * C preprocessor with this header, as tz_regions.c is compiled with it.
 */
#ifndef TFW_TZ_MAP_MPS2_AN505_H
#define TFW_TZ_MAP_MPS2_AN505_H

/* SSRAM-0, Secure alias: the secure image at INITSVTOR0, the NSC region in
 * its last 2 KiB. */
#define TZ_S_CODE_BASE    0x10000000
#define TZ_S_CODE_SIZE    0x0000F800
#define TZ_NSC_BASE       0x1000F800
#define TZ_NSC_SIZE       0x00000800

/* SSRAM-0 at 1 MiB, Non-secure alias; QEMU loads it through the Secure alias
 * (TZ_NS_CODE_LOAD), the same RAM. */
#define TZ_NS_CODE_BASE   0x00100000
#define TZ_NS_CODE_SIZE   0x00010000
#define TZ_NS_CODE_LOAD   0x10100000

/* The SSE-200 internal SRAM, Secure alias: the secure image's data and stack,
 * and the address the QEMU case's non-secure read targets. */
#define TZ_S_RAM_BASE     0x30000000
#define TZ_S_RAM_SIZE     0x00008000

/* SSRAM-0 at 2 MiB, Non-secure alias. */
#define TZ_NS_RAM_BASE    0x00200000
#define TZ_NS_RAM_SIZE    0x00010000

/* The bit the IDAU reads to tell the aliases apart, in code and SRAM. */
#define TZ_CODE_ALIAS_BIT 0x10000000
#define TZ_RAM_ALIAS_BIT  0x10000000

/* The Non-secure peripheral alias (UART0 at 0x40200000). */
#define TZ_NS_PERIPH_BASE 0x40000000
#define TZ_NS_PERIPH_SIZE 0x10000000

/* The console both worlds print on: CMSDK UART0, a Non-secure peripheral. */
#define TZ_CONSOLE_CMSDK  0x40200000

#endif
