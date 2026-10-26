/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SBCon two-wire interface, the I2C block Arm's MPS2 FPGA images carry: no
 * shifter, no state machine, just two open-collector lines the software drives
 * and reads (Arm DUI 0224I, RealView Platform Baseboard for ARM926EJ-S User
 * Guide, 4.19 "Serial bus interface", Table 4-61; docs/reference/README.md).
 *
 *   SB_CONTROL   read  +0x0   bit 0 SCL, bit 1 SDA: the line levels
 *   SB_CONTROLS  write +0x0   a 1 sets the bit: the line is released, high
 *   SB_CONTROLC  write +0x4   a 1 clears the bit: the line is pulled low
 *
 * hal_bus.c clocks the protocol over these three; sbcon.c is the only file that
 * touches the registers, so the host suite (test/unit/test_mps2_bus.c) links
 * hal_bus.c against a simulated bus in its place.
 */
#ifndef TFW_CM_MPS2_SBCON_H
#define TFW_CM_MPS2_SBCON_H

#include <stdint.h>

#define TFW_SB_SCL 0x1u
#define TFW_SB_SDA 0x2u

/* SB_CONTROL: the levels on the bus, TFW_SB_SCL | TFW_SB_SDA where high. */
uint32_t tfw_mps2_sb_lines(void);
/* SB_CONTROLS: release the lines in mask, which then float high unless a
 * device holds them low. */
void     tfw_mps2_sb_release(uint32_t mask);
/* SB_CONTROLC: pull the lines in mask low. */
void     tfw_mps2_sb_drive_low(uint32_t mask);

#endif /* TFW_CM_MPS2_SBCON_H */
