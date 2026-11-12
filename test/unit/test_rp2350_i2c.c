/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 I2C master (src/hal/cortex-m/rp2350/hal_bus.c, row rp2350) over
 * a register model of I2C0's DW_apb_i2c, as test_imx95_hal.c runs the i.MX 95
 * files over test/model/imx95_model.c. The driver compiles unchanged: the
 * stand-in cm_regs.h (boot/hal/cortex-m/rp2350/test/) sends every TFW_CM_REG
 * to tfw_cm_model_reg below. One device sits on the modelled bus; the model
 * logs what reaches the wire (S, Sr, the address byte, data, r for a byte
 * read, P), so each test checks the transaction the part would put out.
 *
 * Accesses settle one behind, as in imx95_model.c: an access is a write if
 * the cell changed, a read otherwise. Registers whose reads have side effects
 * (IC_DATA_CMD, the IC_CLR_* registers) present bit 31, which no write uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define I2C_BASE   0x40090000u
#define RD_MARK    0x80000000u
#define ABRT_USER  (1u << 16)

#define OTHER_MAX 64u

static struct {
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
    uint32_t oaddr[OTHER_MAX], oval[OTHER_MAX];
    unsigned on;
} m;

static uint32_t pending_addr;
static uint8_t  pending;
static uint32_t presented;
static uint32_t cell;

static void logf_(const char *s)
{
    size_t n = strlen(m.log);

    snprintf(m.log + n, sizeof m.log - n, "%s", s);
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

    for (i = 0u; i < m.on; i++) {
        if (m.oaddr[i] == addr) {
            return &m.oval[i];
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(m.on < OTHER_MAX, "model: too many addresses");
    m.oaddr[m.on] = addr;
    m.oval[m.on]  = 0u;
    return &m.oval[m.on++];
}

static void stop(void)
{
    logf_(" P");
    m.raw |= TFW_RP_IC_INTR_STOP_DET;
    m.in_txn = 0u;
    m.stops++;
}

static void abort_txn(uint32_t src)
{
    m.abrt_src |= src;
    m.raw      |= TFW_RP_IC_INTR_TX_ABRT;
    m.flushed   = 1u;
    m.rx_n      = 0u;
    if (!m.no_stop_on_abrt) {
        stop();
    }
}

static void command(uint32_t v)
{
    uint8_t is_read = (v & TFW_RP_IC_DATA_CMD_CMD_READ) != 0u;
    uint8_t b;

    if ((m.enable & TFW_RP_IC_ENABLE_ENABLE) == 0u || m.flushed) {
        m.dropped++;
        return;
    }
    if (m.stuck) {
        return;
    }
    if (!m.in_txn || (v & TFW_RP_IC_DATA_CMD_RESTART) || is_read != m.dir_read) {
        logf_(m.in_txn ? " Sr" : " S");
        log_byte("", (uint8_t)((m.tar << 1) | is_read));
        m.in_txn   = 1u;
        m.dir_read = is_read;
        if ((m.tar & 0x7Fu) != m.dev_addr) {
            abort_txn(TFW_RP_IC_ABRT_7B_ADDR_NOACK);
            return;
        }
    }
    if (is_read) {
        b = m.dev_tx[m.dev_tx_i++ & 15u];
        m.rx[m.rx_n++ & 31u] = b;
        log_byte("r", b);
    } else {
        log_byte("", (uint8_t)v);
        if (m.wcount == m.nack_data_at) {
            abort_txn(TFW_RP_IC_ABRT_TXDATA_NOACK);
            return;
        }
        m.wcount++;
        m.dev_rx[m.dev_rx_n++ & 15u] = (uint8_t)v;
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
    case 0x000u: return m.con;
    case 0x004u: return m.tar;
    case 0x010u: return (m.rx_n ? m.rx[0] : 0u) | RD_MARK;
    case 0x01Cu: return m.hcnt;
    case 0x020u: return m.lcnt;
    case 0x030u: return m.mask;
    case 0x034u: return m.raw;
    case 0x040u: case 0x054u: case 0x060u: return RD_MARK;
    case 0x06Cu: return m.enable;
    case 0x070u:
        return TFW_RP_IC_STATUS_TFNF |
               (m.stuck ? 0u : TFW_RP_IC_STATUS_TFE) |
               (m.rx_n ? TFW_RP_IC_STATUS_RFNE : 0u) |
               ((m.stuck || m.in_txn) ? TFW_RP_IC_STATUS_MST_ACTIVITY : 0u);
    case 0x080u: return m.abrt_src;
    case 0x09Cu:
        return ((m.enable & TFW_RP_IC_ENABLE_ENABLE) || m.never_idle) ? 1u : 0u;
    case 0x0A0u: return m.spklen;
    default:
        TEST_FAIL_MESSAGE("model: unmodelled I2C register");
        return 0u;
    }
}

static void on_read(uint32_t a)
{
    if (a == I2C_BASE + 0x010u) {
        if (m.rx_n) {
            memmove(m.rx, m.rx + 1, (size_t)(m.rx_n - 1u));
            m.rx_n--;
        }
    } else if (a == I2C_BASE + 0x040u) {
        m.raw = 0u;
        m.abrt_src = 0u;
        m.flushed = 0u;
    } else if (a == I2C_BASE + 0x054u) {
        m.raw &= ~TFW_RP_IC_INTR_TX_ABRT;
        m.abrt_src = 0u;
        m.flushed = 0u;
    } else if (a == I2C_BASE + 0x060u) {
        m.raw &= ~TFW_RP_IC_INTR_STOP_DET;
    }
}

static void cfg(uint32_t *r, uint32_t v)
{
    if (m.enable & TFW_RP_IC_ENABLE_ENABLE) {
        m.cfg_while_enabled++;     /* the part ignores it */
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
    case 0x000u: cfg(&m.con, v); break;
    case 0x004u: cfg(&m.tar, v); break;
    case 0x010u: command(v); break;
    case 0x01Cu: cfg(&m.hcnt, v); break;
    case 0x020u: cfg(&m.lcnt, v); break;
    case 0x030u: m.mask = v; break;
    case 0x0A0u: cfg(&m.spklen, v); break;
    case 0x06Cu:
        if ((v & TFW_RP_IC_ENABLE_ABORT) && (m.enable & TFW_RP_IC_ENABLE_ENABLE)) {
            m.abort_reqs++;
            if (m.stuck) {
                m.enable = v;      /* the STOP cannot go out: ABORT stays set */
                break;
            }
            if (m.in_txn) {
                stop();
            }
            m.raw     |= TFW_RP_IC_INTR_TX_ABRT;
            m.abrt_src |= ABRT_USER;
            m.flushed  = 1u;
            m.enable   = v & ~TFW_RP_IC_ENABLE_ABORT;
            break;
        }
        m.enable = v;
        if ((v & TFW_RP_IC_ENABLE_ENABLE) == 0u) {
            m.rx_n = 0u;
            m.flushed = 0u;
            if (!m.stuck) {
                m.in_txn = 0u;
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

void setUp(void)
{
    memset(&m, 0, sizeof m);
    pending = 0u;
    m.con = 0x65u;          /* IC_CON reset (table 1109): master, slave off, RESTART, FAST */
    m.dev_addr = 0x60u;
    m.nack_data_at = 0xFFu;
    m.dev_tx[0] = 0xA5u;
    m.dev_tx[1] = 0x5Au;
    m.dev_tx[2] = 0x3Cu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000uL));
    settle();
}

void tearDown(void) {}

static tfw_status_t xfer(uint8_t a, const uint8_t *wr, uint8_t wl, uint8_t *rd, uint8_t rl)
{
    tfw_status_t st = tfw_hal_i2c_xfer(a, wr, wl, rd, rl);

    settle();
    return st;
}

/* ---- init */

static void test_init_programs_the_fast_mode_master_and_its_pins(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x65u, m.con);
    TEST_ASSERT_EQUAL_UINT32(7u, m.hcnt);
    TEST_ASSERT_EQUAL_UINT32(17u, m.lcnt);
    TEST_ASSERT_EQUAL_UINT32(1u, m.spklen);
    TEST_ASSERT_EQUAL_UINT32(0u, m.mask);
    TEST_ASSERT_EQUAL_UINT32(0u, m.enable);
    TEST_ASSERT_EQUAL_UINT32(TFW_RP_FUNC_I2C, *other(0x40028024u));   /* GPIO4 CTRL */
    TEST_ASSERT_EQUAL_UINT32(TFW_RP_FUNC_I2C, *other(0x4002802Cu));   /* GPIO5 CTRL */
    TEST_ASSERT_EQUAL_HEX32(0x5Au, *other(0x40038014u));             /* GPIO4 pad */
    TEST_ASSERT_EQUAL_HEX32(0x5Au, *other(0x40038018u));             /* GPIO5 pad */
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_RESET_I2C0, *other(0x40023000u)); /* reset released */
    TEST_ASSERT_EQUAL_UINT32(0u, m.cfg_while_enabled);
}

static void test_init_refuses_a_rate_it_cannot_make(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(0uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(1000001uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(10uL));
}

/* ---- transfers */

static void test_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 3u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 02 03 P", m.log);
    TEST_ASSERT_EQUAL_UINT8(3u, m.dev_rx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, m.dev_rx, 3u);
    TEST_ASSERT_EQUAL_UINT32(0x60u, m.tar);
    TEST_ASSERT_EQUAL_UINT32(1u, m.stops);
    TEST_ASSERT_EQUAL_UINT32(0u, m.cfg_while_enabled);
    TEST_ASSERT_EQUAL_UINT32(0u, m.raw & TFW_RP_IC_INTR_STOP_DET);
}

static void test_read(void)
{
    uint8_t rd[3] = { 0 };
    const uint8_t want[] = { 0xA5u, 0x5Au, 0x3Cu };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, NULL, 0u, rd, 3u));
    TEST_ASSERT_EQUAL_STRING(" S C1 rA5 r5A r3C P", m.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 3u);
    TEST_ASSERT_EQUAL_UINT32(1u, m.stops);
}

static void test_write_then_read_with_restart(void)
{
    const uint8_t wr[] = { 0x10u };
    uint8_t rd[2] = { 0 };
    const uint8_t want[] = { 0xA5u, 0x5Au };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, rd, 2u));
    TEST_ASSERT_EQUAL_STRING(" S C0 10 Sr C1 rA5 r5A P", m.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 2u);
    TEST_ASSERT_EQUAL_UINT32(1u, m.stops);
}

static void test_two_transfers_to_two_addresses(void)
{
    const uint8_t wr[] = { 0x55u };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x61u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 55 P S C2 P S C0 55 P", m.log);
    TEST_ASSERT_EQUAL_UINT32(0u, m.cfg_while_enabled);
}

static void test_empty_transfer_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", m.log);
}

/* ---- aborts */

static void test_address_nack_on_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, wr, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S 44 P", m.log);
    TEST_ASSERT_EQUAL_UINT8(0u, m.dev_rx_n);
    TEST_ASSERT_EQUAL_UINT32(0u, m.raw);       /* TX_ABRT and STOP_DET cleared */
}

static void test_address_nack_on_read(void)
{
    uint8_t rd[2] = { 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, NULL, 0u, rd, 2u));
    TEST_ASSERT_EQUAL_STRING(" S 45 P", m.log);
}

static void test_data_nack_stops_the_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };
    uint8_t rd[1] = { 0 };

    m.nack_data_at = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x60u, wr, 3u, rd, 1u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 02 P", m.log);
    TEST_ASSERT_EQUAL_UINT8(1u, m.dev_rx_n);
    TEST_ASSERT_EQUAL_UINT32(1u, m.stops);
}

static void test_abort_without_a_stop_is_ended_by_abort(void)
{
    const uint8_t wr[] = { 0x01u };

    m.no_stop_on_abrt = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S 44 P", m.log);
    TEST_ASSERT_EQUAL_UINT32(1u, m.abort_reqs);
    TEST_ASSERT_EQUAL_UINT32(0u, m.enable);
}

/* ---- a bus that does not come free */

static void test_stuck_bus_times_out_and_aborts(void)
{
    const uint8_t wr[] = { 0x01u };

    m.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, m.abort_reqs);
    TEST_ASSERT_EQUAL_UINT32(0u, m.enable & TFW_RP_IC_ENABLE_ENABLE);
    TEST_ASSERT_EQUAL_STRING("", m.log);
}

static void test_stuck_bus_on_read_times_out(void)
{
    uint8_t rd[1] = { 0 };

    m.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, NULL, 0u, rd, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, m.abort_reqs);
}

static void test_controller_that_never_goes_idle_times_out(void)
{
    const uint8_t wr[] = { 0x01u };

    m.never_idle = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", m.log);
    TEST_ASSERT_EQUAL_UINT32(0u, m.tar);       /* never addressed */
}

static void test_bus_recovers_after_a_timeout(void)
{
    const uint8_t wr[] = { 0x01u };

    m.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    m.stuck = 0u;
    m.in_txn = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 P", m.log);
}

/* ---- the buses this SoC does not carry */

static void test_spi_and_onewire_are_unsupported(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_init(0u, 0u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(NULL, &b, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(NULL, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(NULL, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(NULL, &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_programs_the_fast_mode_master_and_its_pins);
    RUN_TEST(test_init_refuses_a_rate_it_cannot_make);
    RUN_TEST(test_write);
    RUN_TEST(test_read);
    RUN_TEST(test_write_then_read_with_restart);
    RUN_TEST(test_two_transfers_to_two_addresses);
    RUN_TEST(test_empty_transfer_touches_nothing);
    RUN_TEST(test_address_nack_on_write);
    RUN_TEST(test_address_nack_on_read);
    RUN_TEST(test_data_nack_stops_the_write);
    RUN_TEST(test_abort_without_a_stop_is_ended_by_abort);
    RUN_TEST(test_stuck_bus_times_out_and_aborts);
    RUN_TEST(test_stuck_bus_on_read_times_out);
    RUN_TEST(test_controller_that_never_goes_idle_times_out);
    RUN_TEST(test_bus_recovers_after_a_timeout);
    RUN_TEST(test_spi_and_onewire_are_unsupported);
    return UNITY_END();
}
