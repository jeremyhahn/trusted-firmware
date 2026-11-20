/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 secure/non-secure split of the NUCLEO-H563ZI (#590,
 * docs/plans/590.md), from the STM32H563 memory map (RM0481): flash at
 * 0x08000000 Non-secure and 0x0C000000 Secure, SRAM at 0x20000000 and
 * 0x30000000, peripherals at 0x40000000 and 0x50000000. Built and
 * size-checked only: the board is not flashed with it. Running it needs
 * TZEN=1 and the bank 1 secure watermark over the first 128 KiB, option
 * bytes the owner sets (CA 583); nothing in this tree writes them.
 */
#ifndef TFW_TZ_MAP_NUCLEO_H563ZI_H
#define TFW_TZ_MAP_NUCLEO_H563ZI_H

/* Flash bank 1, Secure alias: pages 0-15, the NSC region in the last 2 KiB. */
#define TZ_S_CODE_BASE    0x0C000000
#define TZ_S_CODE_SIZE    0x0001F800
#define TZ_NSC_BASE       0x0C01F800
#define TZ_NSC_SIZE       0x00000800

/* Flash from 128 KiB, Non-secure alias; programmed where it runs. */
#define TZ_NS_CODE_BASE   0x08020000
#define TZ_NS_CODE_SIZE   0x00040000
#define TZ_NS_CODE_LOAD   0x08020000

/* SRAM1's first 64 KiB, Secure alias, then its next 64 KiB Non-secure. */
#define TZ_S_RAM_BASE     0x30000000
#define TZ_S_RAM_SIZE     0x00010000
#define TZ_NS_RAM_BASE    0x20010000
#define TZ_NS_RAM_SIZE    0x00010000

/* The bit the IDAU reads to tell the aliases apart: 26 in flash (0x08/0x0C),
 * 28 in SRAM and the peripherals. */
#define TZ_CODE_ALIAS_BIT 0x04000000
#define TZ_RAM_ALIAS_BIT  0x10000000

#define TZ_NS_PERIPH_BASE 0x40000000
#define TZ_NS_PERIPH_SIZE 0x10000000

/* No console this slice: GTZC and USART3 bring-up are the hardware slice's. */

#endif
