/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register model of the RP2350's I2C0 (DW_apb_i2c) with one device on the
 * bus, for test_rp2350_i2c.c. i2c_model.c defines the register hook the
 * stand-in cm_regs.h (boot/hal/cortex-m/rp2350/test/) sends every register
 * access to; the test sets the device and the faults up in rp_i2c and reads
 * back what reached the wire.
 */
#ifndef RP2350_I2C_MODEL_H
#define RP2350_I2C_MODEL_H

#include <stdint.h>

#define RP_I2C_OTHER_MAX 64u

struct rp2350_i2c_model {
    /* I2C0 */
    uint32_t con, tar, spklen, mask, enable, abrt_src, raw;
    uint32_t ss_hcnt, ss_lcnt, fs_hcnt, fs_lcnt;   /* SCL counts per mode */
    uint8_t  in_txn, dir_read, flushed, wcount;
    uint8_t  rx[32];
    uint8_t  rx_n;
    /* the device */
    uint8_t  dev_addr;
    uint8_t  nack_data_at;     /* 0xFF: acks every byte */
    uint8_t  dev_tx[16];
    uint8_t  dev_tx_i;
    uint8_t  dev_rx[16];
    uint8_t  dev_rx_n;
    /* faults */
    uint8_t  stuck;            /* the bus is held: no command completes */
    uint8_t  never_idle;       /* the controller never reports disabled */
    uint8_t  no_stop_on_abrt;  /* an abort leaves the bus without a STOP */
    /* what happened */
    unsigned stops, abort_reqs, cfg_while_enabled, dropped;
    char     log[256];
    /* every other address: stored */
    uint32_t oaddr[RP_I2C_OTHER_MAX], oval[RP_I2C_OTHER_MAX];
    unsigned on;
};

extern struct rp2350_i2c_model rp_i2c;

/* Clears the model, with no access pending and IC_CON at its reset value. */
void rp_i2c_reset(void);

/* Lets the last register access take effect. */
void rp_i2c_settle(void);

/* The wire so far: S, Sr, the address byte, data, r for a byte read, P. */
const char *rp_i2c_log(void);

/* The value last written to addr outside I2C0 (GPIO, pads, resets). */
uint32_t *rp_i2c_other(uint32_t addr);

/* The bus conditions of table 1105 (§12.2.14). */
enum rp_i2c_cond {
    RP_I2C_TLOW, RP_I2C_THIGH, RP_I2C_THD_STA, RP_I2C_TSU_STA,
    RP_I2C_TSU_STO, RP_I2C_TBUF
};

/* IC_CON SPEED as the part latched it: 1 standard, 2 fast or fast plus. */
uint32_t rp_i2c_speed(void);

/* The ic_clk cycles the part spends on cond, timed as table 1105 does from
 * the selected mode's counts: from LCNT, LCNT + 1 cycles; from HCNT,
 * HCNT + SPKLEN + 7 (§12.2.14.1). tSU;STA runs from LCNT in standard mode
 * and from HCNT in fast mode. */
uint32_t rp_i2c_cond_cycles(enum rp_i2c_cond cond);

#endif /* RP2350_I2C_MODEL_H */
