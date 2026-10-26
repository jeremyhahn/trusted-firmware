/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The shield 0 SPI: an Arm PrimeCell SSP (PL022) and the FPGA I/O block's MISC
 * register, whose bit 8 is the shield 0 chip select. The PL022's own select
 * output is not wired to the shield; the image routes the select through MISC
 * (AN505, Arm DAI 0505B, 4.6 and Table 4-5; AN500, Arm DAI 0500B, 3.7 and
 * Table 3-6; docs/reference/README.md).
 *
 * The PL022 registers (Arm DDI 0194H, PL022 Technical Reference Manual,
 * Table 3-1), offsets from the board profile's TFW_MPS2_SPI_BASE:
 *
 *   SSPCR0   +0x00   SCR [15:8], SPH [7], SPO [6], FRF [5:4], DSS [3:0]
 *   SSPCR1   +0x04   SOD [3], MS [2], SSE [1], LBM [0]
 *   SSPDR    +0x08   a write fills the transmit FIFO, a read empties receive
 *   SSPSR    +0x0C   BSY [4], RFF [3], RNE [2], TNF [1], TFE [0]
 *   SSPCPSR  +0x10   CPSDVSR [7:0], even, 2 to 254
 *
 * hal_bus.c drives the bus over these; pl022.c is the only file that touches
 * the registers, so the host suite (test/unit/test_mps2_bus.c) links hal_bus.c
 * against a modelled PL022 in its place, as it does for the SBCon (sbcon.h).
 */
#ifndef TFW_CM_MPS2_PL022_H
#define TFW_CM_MPS2_PL022_H

#include <stdint.h>

#define TFW_SSP_CR0   0x00u
#define TFW_SSP_CR1   0x04u
#define TFW_SSP_DR    0x08u
#define TFW_SSP_SR    0x0Cu
#define TFW_SSP_CPSR  0x10u

/* SSPCR0 (DDI 0194H, 3.3.1, Table 3-2): FRF 00 is Motorola SPI, DSS 0111 an
 * 8-bit frame; SPO is the clock polarity and SPH its phase. */
#define TFW_SSP_CR0_DSS_8      0x0007u
#define TFW_SSP_CR0_SPO        0x0040u
#define TFW_SSP_CR0_SPH        0x0080u
#define TFW_SSP_CR0_SCR_SHIFT  8u
/* SSPCR1 (3.3.2, Table 3-3): SSE enables the port; MS 0, master, may change
 * only while SSE is 0. */
#define TFW_SSP_CR1_SSE        0x0002u
/* SSPSR (3.3.4, Table 3-5). */
#define TFW_SSP_SR_TFE         0x01u
#define TFW_SSP_SR_TNF         0x02u
#define TFW_SSP_SR_RNE         0x04u
#define TFW_SSP_SR_BSY         0x10u

/* MISC bit 8, SHIELD0_SPI_nCS: low selects the shield 0 device. */
#define TFW_MPS2_MISC_SHIELD0_NCS 0x100u

/* The pin reference hal_bus.c accepts as the shield 0 select: the FPGA I/O
 * block's MISC register is port 1 (port 0 is its LED register, hal_gpio.c),
 * and the bit is MISC's. */
#define TFW_MPS2_PORT_MISC     1u
#define TFW_MPS2_SPI_CS_BIT    8u

/* A PL022 register, by its offset above. */
uint32_t tfw_mps2_ssp_read(uint32_t off);
void     tfw_mps2_ssp_write(uint32_t off, uint32_t v);
/* The FPGA I/O block's MISC register. */
uint32_t tfw_mps2_misc_read(void);
void     tfw_mps2_misc_write(uint32_t v);

#endif /* TFW_CM_MPS2_PL022_H */
