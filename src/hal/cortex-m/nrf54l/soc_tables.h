/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's facts that are data, not register access: the UARTE's
 * baud table, which pins exist, the PSEL encoding, the silicon revision
 * the vendor's errata workarounds are for, and how a write to the RRAM is
 * bounded and cut into stores. Kept apart from the drivers beside
 * it so a host test can pin every value (test/unit/test_nrf54l_soc_tables.c)
 * against the vendor's header, which a register write on a board nobody is
 * watching never would.
 *
 * The source is Nordic's nrfx MDK (mdk/nrf54l15_types.h, mdk/nrf54l_erratas.h,
 * docs/reference/README.md names the revision).
 */
#ifndef TFW_CM_NRF54L_SOC_TABLES_H
#define TFW_CM_NRF54L_SOC_TABLES_H

#include <stdint.h>

#include "tfw/status.h"

/* The BAUDRATE register value for a UARTE clocked at 16 MHz (UARTE20 is):
 * TFW_ERR_RANGE for a rate the vendor's table does not list. The table is the
 * vendor's, not a formula: its values are rounded by hand, and a formula
 * that agrees on most rates is wrong on the rest. */
tfw_status_t tfw_nrf54l_uarte_baud(uint32_t baud, uint32_t *reg);

/* Nonzero when GPIO port `port` has a pin `bit` on the nRF54L15: P0.0-P0.6,
 * P1.0-P1.16, P2.0-P2.10. */
uint8_t tfw_nrf54l_pin_exists(uint8_t port, uint8_t bit);

/* A peripheral's PSEL value connecting it to P<port>.<bit>: PIN in bits 0-4,
 * PORT in bits 5-7, CONNECT (bit 31) clear. The caller has checked the pin
 * exists. */
uint32_t tfw_nrf54l_psel(uint8_t port, uint8_t bit);

/* PSEL's value for a signal left unconnected: CONNECT set. */
#define TFW_NRF54L_PSEL_DISCONNECTED 0xFFFFFFFFuL

/* Nonzero for the nRF54L15's first silicon revision, which errata 31, 32 and
 * 40 apply to: FICR words 0x00FFC340 (the part) and 0x00FFC344 (the
 * revision) read 0x1C and 0x01 (nrf54l_erratas.h, nrf54l_errata_31). */
uint8_t tfw_nrf54l_is_rev1(uint32_t part, uint32_t revision);

/* The nRF54L15's RRAM: 1524 KB from 0x00000000 (Zephyr's nrf54l15.dtsi,
 * cpuapp_rram). */
#define TFW_NRF54L_RRAM_SIZE 0x0017D000uL

/* Nonzero when the len bytes at addr lie in the RRAM and none below floor:
 * tfw-boot passes its own region's end, because the RRAM controller's write
 * enable opens every byte of it (boot/hal/cortex-m/nrf54l/boot_rram.c). A
 * span that would wrap past 2^32 is refused, not folded. len 0 at a valid
 * addr is a span. */
uint8_t tfw_nrf54l_rram_span(uint32_t floor, uint32_t addr, uint32_t len);

/* The next store of a write at addr with left bytes to go: 4 when addr is
 * word-aligned and a word is left, else 1. The controller takes both widths
 * (haly/nrfy_rramc.h, nrfy_rramc_words_write and nrfy_rramc_bytes_write);
 * words make a page a quarter of the waits. */
uint32_t tfw_nrf54l_rram_unit(uint32_t addr, uint32_t left);

#endif /* TFW_CM_NRF54L_SOC_TABLES_H */
