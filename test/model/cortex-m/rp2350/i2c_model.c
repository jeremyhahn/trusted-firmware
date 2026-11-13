/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 I2C0 register model test_rp2350_i2c.c runs the I2C master over,
 * as test_imx95_hal.c runs the i.MX 95 files over test/model/imx95_model.c.
 * The stand-in cm_regs.h (boot/hal/cortex-m/rp2350/test/) sends every
 * TFW_CM_REG to tfw_cm_model_reg below. One device sits on the modelled bus;
 * the model logs what reaches the wire (S, Sr, the address byte, data, r for
 * a byte read, P), so each test checks the transaction the part would put out.
 *
 * Accesses settle one behind, as in imx95_model.c: an access is a write if
 * the cell changed, a read otherwise. Registers whose reads have side effects
 * (IC_DATA_CMD, the IC_CLR_* registers) present bit 31, which no write uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "cm_regs.h"
#include "soc_regs.h"
#include "i2c_model.h"

#define I2C_BASE   0x40090000u
#define RD_MARK    0x80000000u
#define ABRT_USER  (1u << 16)

struct rp2350_i2c_model rp_i2c;

static uint32_t pending_addr;
static uint8_t  pending;
static uint32_t presented;
static uint32_t cell;

static void logf_(const char *s)
{
    size_t n = strlen(rp_i2c.log);

    snprintf(rp_i2c.log + n, sizeof rp_i2c.log - n, "%s", s);
}

static void log_byte(const char *pre, uint8_t b)
{
    char tmp[8];

    snprintf(tmp, sizeof tmp, " %s%02X", pre, b);
    logf_(tmp);
}

static uint32_t *other(uint32_t addr)
{
    unsigned i;

    for (i = 0u; i < rp_i2c.on; i++) {
        if (rp_i2c.oaddr[i] == addr) {
            return &rp_i2c.oval[i];
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(rp_i2c.on < RP_I2C_OTHER_MAX, "model: too many addresses");
    rp_i2c.oaddr[rp_i2c.on] = addr;
    rp_i2c.oval[rp_i2c.on]  = 0u;
    return &rp_i2c.oval[rp_i2c.on++];
}

static void stop(void)
{
    logf_(" P");
    rp_i2c.raw |= TFW_RP_IC_INTR_STOP_DET;
    rp_i2c.in_txn = 0u;
    rp_i2c.stops++;
}

static void abort_txn(uint32_t src)
{
    rp_i2c.abrt_src |= src;
    rp_i2c.raw      |= TFW_RP_IC_INTR_TX_ABRT;
    rp_i2c.flushed   = 1u;
    rp_i2c.rx_n      = 0u;
    if (!rp_i2c.no_stop_on_abrt) {
        stop();
    }
}

static void command(uint32_t v)
{
    uint8_t is_read = (v & TFW_RP_IC_DATA_CMD_CMD_READ) != 0u;
    uint8_t b;

    if ((rp_i2c.enable & TFW_RP_IC_ENABLE_ENABLE) == 0u || rp_i2c.flushed) {
        rp_i2c.dropped++;
        return;
    }
    if (rp_i2c.stuck) {
        return;
    }
    if (!rp_i2c.in_txn || (v & TFW_RP_IC_DATA_CMD_RESTART) || is_read != rp_i2c.dir_read) {
        logf_(rp_i2c.in_txn ? " Sr" : " S");
        log_byte("", (uint8_t)((rp_i2c.tar << 1) | is_read));
        rp_i2c.in_txn   = 1u;
        rp_i2c.dir_read = is_read;
        if ((rp_i2c.tar & 0x7Fu) != rp_i2c.dev_addr) {
            abort_txn(TFW_RP_IC_ABRT_7B_ADDR_NOACK);
            return;
        }
    }
    if (is_read) {
        b = rp_i2c.dev_tx[rp_i2c.dev_tx_i++ & 15u];
        rp_i2c.rx[rp_i2c.rx_n++ & 31u] = b;
        log_byte("r", b);
    } else {
        log_byte("", (uint8_t)v);
        if (rp_i2c.wcount == rp_i2c.nack_data_at) {
            abort_txn(TFW_RP_IC_ABRT_TXDATA_NOACK);
            return;
        }
        rp_i2c.wcount++;
        rp_i2c.dev_rx[rp_i2c.dev_rx_n++ & 15u] = (uint8_t)v;
    }
    if (v & TFW_RP_IC_DATA_CMD_STOP) {
        stop();
    }
}

static uint32_t view(uint32_t a)
{
    if (a == 0x40020008u) {                 /* RESETS.RESET_DONE: all out */
        return 0xFFFFFFFFu;
    }
    if (a < I2C_BASE || a >= I2C_BASE + 0x100u) {
        return *other(a);
    }
    switch (a - I2C_BASE) {
    case 0x000u: return rp_i2c.con;
    case 0x004u: return rp_i2c.tar;
    case 0x010u: return (rp_i2c.rx_n ? rp_i2c.rx[0] : 0u) | RD_MARK;
    case 0x014u: return rp_i2c.ss_hcnt;
    case 0x018u: return rp_i2c.ss_lcnt;
    case 0x01Cu: return rp_i2c.fs_hcnt;
    case 0x020u: return rp_i2c.fs_lcnt;
    case 0x030u: return rp_i2c.mask;
    case 0x034u: return rp_i2c.raw;
    case 0x040u: case 0x054u: case 0x060u: return RD_MARK;
    case 0x06Cu: return rp_i2c.enable;
    case 0x070u:
        return TFW_RP_IC_STATUS_TFNF |
               (rp_i2c.stuck ? 0u : TFW_RP_IC_STATUS_TFE) |
               (rp_i2c.rx_n ? TFW_RP_IC_STATUS_RFNE : 0u) |
               ((rp_i2c.stuck || rp_i2c.in_txn) ? TFW_RP_IC_STATUS_MST_ACTIVITY : 0u);
    case 0x080u: return rp_i2c.abrt_src;
    case 0x09Cu:
        return ((rp_i2c.enable & TFW_RP_IC_ENABLE_ENABLE) || rp_i2c.never_idle) ? 1u : 0u;
    case 0x0A0u: return rp_i2c.spklen;
    default:
        TEST_FAIL_MESSAGE("model: unmodelled I2C register");
        return 0u;
    }
}

static void on_read(uint32_t a)
{
    if (a == I2C_BASE + 0x010u) {
        if (rp_i2c.rx_n) {
            memmove(rp_i2c.rx, rp_i2c.rx + 1, (size_t)(rp_i2c.rx_n - 1u));
            rp_i2c.rx_n--;
        }
    } else if (a == I2C_BASE + 0x040u) {
        rp_i2c.raw = 0u;
        rp_i2c.abrt_src = 0u;
        rp_i2c.flushed = 0u;
    } else if (a == I2C_BASE + 0x054u) {
        rp_i2c.raw &= ~TFW_RP_IC_INTR_TX_ABRT;
        rp_i2c.abrt_src = 0u;
        rp_i2c.flushed = 0u;
    } else if (a == I2C_BASE + 0x060u) {
        rp_i2c.raw &= ~TFW_RP_IC_INTR_STOP_DET;
    }
}

static void cfg(uint32_t *r, uint32_t v)
{
    if (rp_i2c.enable & TFW_RP_IC_ENABLE_ENABLE) {
        rp_i2c.cfg_while_enabled++;     /* the part ignores it */
        return;
    }
    *r = v;
}

static void on_write(uint32_t a, uint32_t v)
{
    if (a < I2C_BASE || a >= I2C_BASE + 0x100u) {
        *other(a) = v;
        return;
    }
    switch (a - I2C_BASE) {
    case 0x000u: cfg(&rp_i2c.con, v); break;
    case 0x004u: cfg(&rp_i2c.tar, v); break;
    case 0x010u: command(v); break;
    case 0x014u: cfg(&rp_i2c.ss_hcnt, v); break;
    case 0x018u: cfg(&rp_i2c.ss_lcnt, v); break;
    case 0x01Cu: cfg(&rp_i2c.fs_hcnt, v); break;
    case 0x020u: cfg(&rp_i2c.fs_lcnt, v); break;
    case 0x030u: rp_i2c.mask = v; break;
    case 0x0A0u: cfg(&rp_i2c.spklen, v); break;
    case 0x06Cu:
        if ((v & TFW_RP_IC_ENABLE_ABORT) && (rp_i2c.enable & TFW_RP_IC_ENABLE_ENABLE)) {
            rp_i2c.abort_reqs++;
            if (rp_i2c.stuck) {
                rp_i2c.enable = v;      /* the STOP cannot go out: ABORT stays set */
                break;
            }
            if (rp_i2c.in_txn) {
                stop();
            }
            rp_i2c.raw     |= TFW_RP_IC_INTR_TX_ABRT;
            rp_i2c.abrt_src |= ABRT_USER;
            rp_i2c.flushed  = 1u;
            rp_i2c.enable   = v & ~TFW_RP_IC_ENABLE_ABORT;
            break;
        }
        rp_i2c.enable = v;
        if ((v & TFW_RP_IC_ENABLE_ENABLE) == 0u) {
            rp_i2c.rx_n = 0u;
            rp_i2c.flushed = 0u;
            if (!rp_i2c.stuck) {
                rp_i2c.in_txn = 0u;
            }
        }
        break;
    default:
        TEST_FAIL_MESSAGE("model: write to an unmodelled I2C register");
    }
}

static void settle(void)
{
    if (!pending) {
        return;
    }
    pending = 0u;
    if (cell != presented) {
        on_write(pending_addr, cell);
    } else {
        on_read(pending_addr);
    }
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    settle();
    pending      = 1u;
    pending_addr = addr;
    presented    = view(addr);
    cell         = presented;
    return &cell;
}

void rp_i2c_reset(void)
{
    memset(&rp_i2c, 0, sizeof rp_i2c);
    pending = 0u;
    rp_i2c.con = 0x65u;     /* IC_CON reset (table 1109): master, slave off, RESTART, FAST */
}

void rp_i2c_settle(void)
{
    settle();
}

const char *rp_i2c_log(void)
{
    return rp_i2c.log;
}

uint32_t *rp_i2c_other(uint32_t addr)
{
    return other(addr);
}

uint32_t rp_i2c_speed(void)
{
    return (rp_i2c.con & TFW_RP_IC_CON_SPEED_MASK) >> 1;
}

uint32_t rp_i2c_cond_cycles(enum rp_i2c_cond cond)
{
    uint8_t  std  = rp_i2c_speed() == 1u;
    uint32_t low  = (std ? rp_i2c.ss_lcnt : rp_i2c.fs_lcnt) + 1u;
    uint32_t high = (std ? rp_i2c.ss_hcnt : rp_i2c.fs_hcnt) + rp_i2c.spklen + 7u;

    switch (cond) {
    case RP_I2C_TLOW:
    case RP_I2C_TBUF:
        return low;
    case RP_I2C_TSU_STA:
        return std ? low : high;
    default:
        return high;
    }
}
