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
    uint32_t con, tar, hcnt, lcnt, spklen, mask, enable, abrt_src, raw;
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

#endif /* RP2350_I2C_MODEL_H */
