/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 bus registers on the host (row cortex-m, #628). The image's
 * src/hal/cortex-m/mps2/sbcon.c and pl022.c run unchanged: their TFW_CM_REG
 * lands here (test/model/cortex-m/mps2/cm_regs.h), the address is decoded
 * against the board profile's bases, and the access goes on to the suite's
 * bus as one of the hooks below.
 *
 *   TFW_MPS2_I2C_BASE + 0x0    read: sb_lines      write: sb_release (SB_CONTROLS)
 *   TFW_MPS2_I2C_BASE + 0x4                        write: sb_drive_low (SB_CONTROLC)
 *   TFW_MPS2_SPI_BASE + off    read: ssp_read(off) write: ssp_write(off, v),
 *                              off CR0 to CPSR, the registers pl022.h names
 *   TFW_MPS2_FPGAIO_BASE + 0x4C read: misc_read    write: misc_write
 *
 * A register access is a read or a write only by what the code does with the
 * word it is given, which a host pointer cannot see. The link therefore wraps
 * each accessor (-Wl,--wrap, MPS2_BUS_REGS_WRAP in mk/host.mk): the wrap says
 * which it is, the real accessor runs, and the model fails the test unless it
 * made exactly one access to a register that direction allows. A read is
 * served when the word is handed out, a write when the accessor returns. Any
 * other access, the image's hal_adc.c making one among them, fails too.
 */
#ifndef TFW_MPS2_BUS_MODEL_H
#define TFW_MPS2_BUS_MODEL_H

#include <stdint.h>

volatile uint32_t *tfw_mps2_model_reg(uint32_t addr);

/* The suite's bus, one hook per register direction above. */
uint32_t tfw_mps2_model_sb_lines(void);
void     tfw_mps2_model_sb_release(uint32_t mask);
void     tfw_mps2_model_sb_drive_low(uint32_t mask);
uint32_t tfw_mps2_model_ssp_read(uint32_t off);
void     tfw_mps2_model_ssp_write(uint32_t off, uint32_t v);
uint32_t tfw_mps2_model_misc_read(void);
void     tfw_mps2_model_misc_write(uint32_t v);

/* The accessors as the link's --wrap names them. */
uint32_t __real_tfw_mps2_sb_lines(void);
void     __real_tfw_mps2_sb_release(uint32_t mask);
void     __real_tfw_mps2_sb_drive_low(uint32_t mask);
uint32_t __real_tfw_mps2_ssp_read(uint32_t off);
void     __real_tfw_mps2_ssp_write(uint32_t off, uint32_t v);
uint32_t __real_tfw_mps2_misc_read(void);
void     __real_tfw_mps2_misc_write(uint32_t v);
uint32_t __wrap_tfw_mps2_sb_lines(void);
void     __wrap_tfw_mps2_sb_release(uint32_t mask);
void     __wrap_tfw_mps2_sb_drive_low(uint32_t mask);
uint32_t __wrap_tfw_mps2_ssp_read(uint32_t off);
void     __wrap_tfw_mps2_ssp_write(uint32_t off, uint32_t v);
uint32_t __wrap_tfw_mps2_misc_read(void);
void     __wrap_tfw_mps2_misc_write(uint32_t v);

#endif /* TFW_MPS2_BUS_MODEL_H */
