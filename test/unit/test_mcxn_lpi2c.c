/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x I2C master (src/hal/cortex-m/mcxn94x/hal_bus.c) and its clock
 * arithmetic (lpi2c_timing.c), on the host (row mcxn947).
 *
 * hal_bus.c is included whole, with its two controller accessors sent to a
 * model of the LPI2C master below and every other register (the clock gates,
 * the unit's function select, the pins) to a table the tests read back. The
 * model queues each command in a four-deep FIFO and runs it as it is queued,
 * or, with a lag, some MSR polls later, as the controller does while the bus
 * clocks: START with an address answered or refused by one device, transmit,
 * receive n bytes from the device, STOP. A refusal holds the FIFO. It
 * can hold the bus busy for another master, refuse a data byte, or stop
 * taking commands, which is a stuck SCL as the controller sees it.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

static uint32_t          model_rd(uint32_t off);
static void              model_wr(uint32_t off, uint32_t v);
/* The register model keeps the target's cm_regs.h out: soc_regs.h's
 * registers go to cm_slot. */
#include "mcxn_reg_model.h"
#define TFW_MCXN_I2C_RD(off)     model_rd(off)
#define TFW_MCXN_I2C_WR(off, v)  model_wr((off), (v))

/* boards/frdm-mcxn947/board_profile.h's bus pins; the host board has none. */
#define TFW_MCXN_I2C_PORT     4u
#define TFW_MCXN_I2C_SDA_BIT  0u
#define TFW_MCXN_I2C_SCL_BIT  1u
#define TFW_MCXN_I2C_PIN_FN   2u

#include "hal_bus.c"

#define DEV_ADDR 0x60u
#define CMD(c, d) ((uint16_t)(((c) << 8) | (d)))

/* ---- the other registers: an address-keyed table */
static struct {
    uintptr_t addr;
    uint32_t  val;
} slots[32];
static unsigned slot_n;

static volatile uint32_t *cm_slot(uintptr_t addr)
{
    for (unsigned i = 0; i < slot_n; i++) {
        if (slots[i].addr == addr) {
            return &slots[i].val;
        }
    }
    TEST_ASSERT_TRUE(slot_n < sizeof slots / sizeof slots[0]);
    slots[slot_n].addr = addr;
    slots[slot_n].val = 0u;
    return &slots[slot_n++].val;
}

static uint32_t slot_value(uintptr_t addr)
{
    return *cm_slot(addr);
}

/* ---- the controller and the one device on its bus */
static struct {
    uint32_t mcr, mcfgr1, mcfgr2, mccr0;
    uint32_t flags;          /* SDF, NDF, ALF, FEF, PLTF as set */
    int      mbf;            /* this master holds the bus */
    int      bbf_other;      /* polls another master or a stuck line holds
                              * it for; < 0 is for good */
    int      accept;         /* commands still taken; < 0 is no limit */
    int      nack_at;        /* index of the data byte refused; < 0 none */
    uint32_t refusal;        /* the flag that refusal raises: NDF */
    unsigned tx_index;
    uint16_t log[64];
    unsigned log_n;
    uint8_t  got[16];        /* the bytes the device received */
    unsigned got_n;
    uint8_t  answer[16];     /* what the device sends back */
    unsigned answer_pos;
    uint8_t  rx[16];         /* the receive FIFO */
    unsigned rx_head, rx_tail;
    unsigned stops;
    uint32_t fifo[4];        /* the transmit FIFO: commands queued, not run */
    unsigned fifo_n;
    int      lag;            /* MSR polls a command waits at the FIFO head
                              * before it runs; 0 runs it as it is queued */
    int      wait;
    unsigned restarts;       /* STARTs on a bus already ours: repeated STARTs */
    unsigned resets;         /* MCR RST writes */
    int      stop_lost;      /* a STOP runs but never ends the transfer */
    uint32_t after_clear;    /* a flag raised again by the first W1C while this master holds the bus: one shot */
    uint32_t stop_flag;      /* a flag the next STOP raises in place of SDF, the bus let go without it: one shot */
    uint32_t rx_flag;        /* a flag the receive raises after its bytes */
} m;

static uint32_t delayed_us;

void tfw_hal_delay_us(uint16_t us)
{
    delayed_us += us;
}

static void model_run(uint32_t v);

/* Runs the command at the FIFO head once it has waited its polls. A refusal
 * holds the FIFO until its flag is cleared. */
static void model_step(void)
{
    if (m.fifo_n == 0u || (m.flags & TFW_MCXN_LPI2C_MSR_NDF)) {
        return;
    }
    if (m.wait > 0) {
        m.wait--;
        return;
    }
    model_run(m.fifo[0]);
    m.fifo_n--;
    memmove(&m.fifo[0], &m.fifo[1], m.fifo_n * sizeof m.fifo[0]);
    m.wait = m.lag;
}

static uint32_t model_rd(uint32_t off)
{
    switch (off) {
    case TFW_MCXN_LPI2C_MSR_OFF: {
        int other = m.bbf_other != 0;

        if (m.bbf_other > 0) {
            m.bbf_other--;
        }
        model_step();
        return m.flags |
               (m.accept != 0 && m.fifo_n < 4u ? TFW_MCXN_LPI2C_MSR_TDF : 0u) |
               (m.rx_head != m.rx_tail ? TFW_MCXN_LPI2C_MSR_RDF : 0u) |
               (m.mbf ? TFW_MCXN_LPI2C_MSR_MBF : 0u) |
               (m.mbf || other ? TFW_MCXN_LPI2C_MSR_BBF : 0u);
    }
    case TFW_MCXN_LPI2C_MRDR_OFF:
        if (m.rx_head == m.rx_tail) {
            return TFW_MCXN_LPI2C_MRDR_RXEMPTY;
        }
        return m.rx[m.rx_tail++ % sizeof m.rx];
    case TFW_MCXN_LPI2C_MCR_OFF:    return m.mcr;
    case TFW_MCXN_LPI2C_MCFGR1_OFF: return m.mcfgr1;
    case TFW_MCXN_LPI2C_MCFGR2_OFF: return m.mcfgr2;
    case TFW_MCXN_LPI2C_MCCR0_OFF:  return m.mccr0;
    default:
        TEST_FAIL_MESSAGE("read of a register the driver has no use for");
        return 0u;
    }
}

/* A command written to MTDR: queued, and run at once unless the model lags. */
static void model_command(uint32_t v)
{
    TEST_ASSERT_TRUE(m.accept != 0);
    TEST_ASSERT_TRUE(m.fifo_n < sizeof m.fifo / sizeof m.fifo[0]);
    TEST_ASSERT_TRUE(m.log_n < sizeof m.log / sizeof m.log[0]);
    m.log[m.log_n++] = (uint16_t)v;
    if (m.accept > 0) {
        m.accept--;
    }
    if (m.fifo_n == 0u) {
        m.wait = m.lag;
    }
    m.fifo[m.fifo_n++] = v;
    while (m.lag == 0 && m.fifo_n != 0u && !(m.flags & TFW_MCXN_LPI2C_MSR_NDF)) {
        model_step();
    }
}

static void model_run(uint32_t v)
{
    uint32_t cmd = v >> TFW_MCXN_LPI2C_MTDR_CMD_SHIFT;
    uint32_t data = v & 0xFFu;

    switch (cmd) {
    case TFW_MCXN_LPI2C_CMD_START:
        if (m.mbf) {
            m.restarts++;
        }
        m.mbf = 1;
        m.tx_index = 0u;
        if ((data >> 1) != DEV_ADDR) {
            m.flags |= TFW_MCXN_LPI2C_MSR_NDF;
        }
        break;
    case TFW_MCXN_LPI2C_CMD_TX:
        TEST_ASSERT_TRUE(m.mbf);
        if ((int)m.tx_index++ == m.nack_at) {
            m.flags |= m.refusal;
            break;
        }
        m.got[m.got_n++] = (uint8_t)data;
        break;
    case TFW_MCXN_LPI2C_CMD_RX:
        TEST_ASSERT_TRUE(m.mbf);
        for (uint32_t i = 0; i <= data; i++) {
            m.rx[m.rx_head++ % sizeof m.rx] = m.answer[m.answer_pos++];
        }
        m.flags |= m.rx_flag;
        break;
    case TFW_MCXN_LPI2C_CMD_STOP:
        if (m.stop_flag) {
            m.flags |= m.stop_flag;
            m.stop_flag = 0u;
            m.mbf = 0;
            break;
        }
        if (m.mbf && !m.stop_lost) {
            m.mbf = 0;
            m.flags |= TFW_MCXN_LPI2C_MSR_SDF;
            m.stops++;
        }
        break;
    default:
        TEST_FAIL_MESSAGE("a command the driver does not send");
    }
}

static void model_wr(uint32_t off, uint32_t v)
{
    switch (off) {
    case TFW_MCXN_LPI2C_MSR_OFF:
        m.flags &= ~(v & MSR_W1C);
        if (m.mbf && m.after_clear) {
            m.flags |= m.after_clear;
            m.after_clear = 0u;
        }
        break;
    case TFW_MCXN_LPI2C_MCR_OFF:
        m.mcr = v & ~(TFW_MCXN_LPI2C_MCR_RTF | TFW_MCXN_LPI2C_MCR_RRF);
        if (v & TFW_MCXN_LPI2C_MCR_RTF) {
            m.fifo_n = 0u;
        }
        if (v & TFW_MCXN_LPI2C_MCR_RRF) {
            m.rx_tail = m.rx_head;
        }
        /* The master logic back to its reset state, MCR alone kept: the
         * transfer and the timing gone, the bus let go. */
        if (v & TFW_MCXN_LPI2C_MCR_RST) {
            m.resets++;
            m.fifo_n = 0u;
            m.mbf = 0;
            m.flags = 0u;
            m.rx_tail = m.rx_head;
            m.mcfgr1 = 0u;
            m.mcfgr2 = 0u;
            m.mccr0 = 0u;
        }
        break;
    case TFW_MCXN_LPI2C_MTDR_OFF:   model_command(v); break;
    case TFW_MCXN_LPI2C_MCFGR1_OFF: m.mcfgr1 = v; break;
    case TFW_MCXN_LPI2C_MCFGR2_OFF: m.mcfgr2 = v; break;
    case TFW_MCXN_LPI2C_MCCR0_OFF:  m.mccr0 = v; break;
    default:
        TEST_FAIL_MESSAGE("write to a register the driver has no use for");
    }
}

void setUp(void)
{
    memset(&m, 0, sizeof m);
    m.accept = -1;
    m.nack_at = -1;
    m.refusal = TFW_MCXN_LPI2C_MSR_NDF;
    slot_n = 0u;
    delayed_us = 0u;
}

void tearDown(void) {}

static void assert_log(const uint16_t *want, unsigned n)
{
    TEST_ASSERT_EQUAL_UINT(n, m.log_n);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(want, m.log, n);
}

/* ---- the clock arithmetic */

static uint32_t field(uint32_t reg, uint32_t shift)
{
    return (reg >> shift) & TFW_MCXN_LPI2C_MCCR0_FIELD_MAX;
}

static void assert_timing(uint32_t hz, uint32_t pre, uint32_t lo, uint32_t hi,
                          uint32_t sethold, uint32_t datavd, uint32_t filtsda)
{
    tfw_mcxn_lpi2c_timing_t t;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpi2c_timing(12000000u, hz, &t));
    TEST_ASSERT_EQUAL_UINT32(pre, t.mcfgr1);
    TEST_ASSERT_EQUAL_UINT32(filtsda << TFW_MCXN_LPI2C_MCFGR2_FILTSDA_SHIFT, t.mcfgr2);
    TEST_ASSERT_EQUAL_UINT32(lo, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_CLKLO_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(hi, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_CLKHI_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(sethold, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_SETHOLD_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(datavd, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_DATAVD_SHIFT));
}

/* 12 MHz / (61 + 1 + 55 + 1 + 2) = 100 kHz exactly, prescale 0; CLKHI cut
 * from 57 to 55 for tBUF, as LPI2C_MasterSetBaudRate does. */
static void test_100k_from_fro12m(void)
{
    assert_timing(100000u, 0u, 61u, 55u, 59u, 29u, 3u);
}

/* 12 MHz / (14 + 1 + 12 + 1 + 2) = 400 kHz; no FILTSDA needed. */
static void test_400k_from_fro12m(void)
{
    assert_timing(400000u, 0u, 14u, 12u, 14u, 6u, 0u);
}

static void test_1m_from_fro12m(void)
{
    assert_timing(1000000u, 0u, 5u, 3u, 5u, 2u, 0u);
}

/* 10 kHz needs prescale 16: 750 kHz / 75. FILTSDA would be 42, more than its
 * four bits hold, so it is the most they hold. */
static void test_a_slow_rate_takes_a_prescale(void)
{
    assert_timing(10000u, 4u, 38u, 35u, 36u, 17u, 15u);
}

/* 2 kHz: prescale 6 (div 64, latency 0) takes round(93.75) = 94 ticks, cycle
 * 92, 187500 / 94 = 1994 Hz, error 6; prescale 7 takes 47 ticks, cycle 45,
 * 93750 / 47 = 1994 Hz, error 6 too. The tie keeps the first, smaller
 * prescale. CLKHI 46 is cut to 45 for tBUF (45 + 48 <= 92 + 1), CLKLO 92 - 45
 * = 47; a period is 93 prescaled clocks, SETHOLD 93 / 2 - 1 = 45, DATAVD
 * 93 / 4 - 1 = 22; FILTSDA 300 - 64 - 2 is capped at 15. */
static void test_a_tie_goes_to_the_smaller_prescale(void)
{
    assert_timing(2000u, 6u, 47u, 45u, 45u, 22u, 15u);
}

/* The ceiling is CLKLO + CLKHI <= 120 - SCL_LATENCY, inclusive. 98 kHz:
 * prescale 0 takes round(122.45) = 122 ticks, cycle 122 - 2 - 2 = 118 = 120 - 2,
 * at the ceiling and taken; 12e6 / 122 = 98360 Hz, error 360, tied by
 * prescale 1 (6e6 / 61). CLKHI 58 cut to 56 for tBUF (56 + 63 <= 118 + 1),
 * CLKLO 62; a period is 122 clocks, SETHOLD 60, DATAVD 29; FILTSDA
 * 6 - 1 - 2 = 3. 97 kHz: prescale 0 takes round(123.71) = 124 ticks, cycle
 * 120 > 118, refused; prescale 1 takes 62 ticks, cycle 59, 6e6 / 62 = 96774
 * Hz, error 226. CLKHI 29 cut to 28 (28 + 32 <= 60), CLKLO 31; a period is
 * 61 clocks, SETHOLD 29, DATAVD 14; FILTSDA 6 - 2 - 2 = 2. */
static void test_the_cycle_ceiling_is_inclusive(void)
{
    assert_timing(98000u, 0u, 62u, 56u, 60u, 29u, 3u);
    assert_timing(97000u, 1u, 31u, 28u, 29u, 14u, 2u);
}

static void test_a_rate_out_of_reach_is_refused(void)
{
    tfw_mcxn_lpi2c_timing_t t;

    memset(&t, 0xA5, sizeof t);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(12000000u, 0u, &t));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(0u, 100000u, &t));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(12000000u, 500u, &t));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(12000000u, 3000000u, &t));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mccr0);
}

/* The 5 % bound itself, on a rate the search does find a setting for. From
 * 12 MHz at prescale 0 the nearest period is 4 + 1 + 1 + 1 + 2 = 9 clocks
 * (CLKLO + 1, CLKHI + 1, the latency): 12e6 / 9 = 1333333 Hz. Asked for
 * 1403508 Hz that misses by 70175 Hz, hz / 20 exactly, and is accepted;
 * 1403509 Hz misses by 70176 > 70175 and is refused with *out left alone.
 * Nothing else refuses 1403509 Hz: every other check passes on that setting.
 * (1263158 Hz would not do: its best setting, prescale 1, has CLKHI 0 and
 * the field check refuses it whatever the bound.) */
static void test_the_five_percent_bound_is_inclusive(void)
{
    tfw_mcxn_lpi2c_timing_t t;

    memset(&t, 0xA5, sizeof t);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpi2c_timing(12000000u, 1403508u, &t));
    TEST_ASSERT_EQUAL_HEX32(0u, t.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(0u, t.mcfgr2);
    TEST_ASSERT_EQUAL_HEX32(0x01030104u, t.mccr0);
    TEST_ASSERT_EQUAL_UINT32(4u, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_CLKLO_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(1u, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_CLKHI_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(3u, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_SETHOLD_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(1u, field(t.mccr0, TFW_MCXN_LPI2C_MCCR0_DATAVD_SHIFT));

    memset(&t, 0xA5, sizeof t);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(12000000u, 1403509u, &t));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mcfgr2);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mccr0);
}

/* The slowest rate the search times from 12 MHz, on its last prescale. 766
 * Hz: prescale 7 (div 128, latency 0) takes round(93750 / 766 = 122.39) =
 * 122 ticks, cycle 120, at the ceiling; 93750 / 122 = 768 Hz, error 2;
 * prescale 6 takes 245 ticks, too slow. CLKHI 60 cut to 58 for tBUF (58 + 63
 * <= 120 + 1), CLKLO 120 - 58 = 62; a period is 12e6 / 766 / 128 = 122
 * clocks, SETHOLD 60, DATAVD 29; FILTSDA 783 - 128 - 2 is capped at 15.
 * 765 Hz: round(122.55) = 123 ticks, cycle 121 > 120, and no prescale is
 * left, so it is refused with *out left alone. */
static void test_the_slowest_rate_takes_the_last_prescale(void)
{
    tfw_mcxn_lpi2c_timing_t t;

    assert_timing(766u, 7u, 62u, 58u, 60u, 29u, 15u);

    memset(&t, 0xA5, sizeof t);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpi2c_timing(12000000u, 765u, &t));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mcfgr2);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, t.mccr0);
}

/* ---- init */

static void test_init_routes_clocks_pins_and_the_unit(void)
{
    uint32_t pcr = (2u << 8) | TFW_MCXN_PCR_PS | TFW_MCXN_PCR_PE | TFW_MCXN_PCR_ODE |
                   TFW_MCXN_PCR_IBE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000u));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_FC_PSELID_I2C,
                            slot_value(TFW_MCXN_FC2_BASE + TFW_MCXN_FC_PSELID_OFF));
    TEST_ASSERT_EQUAL_HEX32(1u << 13, slot_value(TFW_MCXN_SYSCON_BASE + 0x224u));
    TEST_ASSERT_EQUAL_HEX32(1u << 17, slot_value(TFW_MCXN_SYSCON_BASE + 0x220u));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_FCCLKSEL_FRO12M,
                            slot_value(TFW_MCXN_SYSCON_BASE + 0x2B0u + 8u));
    TEST_ASSERT_EQUAL_HEX32(0u, slot_value(TFW_MCXN_SYSCON_BASE + 0x850u + 8u));
    TEST_ASSERT_EQUAL_HEX32(pcr, slot_value(TFW_MCXN_PORT_BASE(4u) + 0x80u));
    TEST_ASSERT_EQUAL_HEX32(pcr, slot_value(TFW_MCXN_PORT_BASE(4u) + 0x84u));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_LPI2C_MCR_MEN, m.mcr);
    TEST_ASSERT_EQUAL_HEX32((29u << 24) | (59u << 16) | (55u << 8) | 61u, m.mccr0);
    TEST_ASSERT_EQUAL_HEX32(0u, m.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(3u << 24, m.mcfgr2);
}

static void test_init_refuses_a_rate_it_cannot_time(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(3000000u));
    TEST_ASSERT_EQUAL_UINT(0u, slot_n);
}

/* ---- transfers */

static void test_a_write_is_start_bytes_and_stop(void)
{
    static const uint8_t out[] = { 0x03, 0x07, 0x41 };
    static const uint16_t want[] = {
        CMD(4, DEV_ADDR << 1), CMD(0, 0x03), CMD(0, 0x07), CMD(0, 0x41), CMD(2, 0)
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 3u, NULL, 0u));
    assert_log(want, 5u);
    TEST_ASSERT_EQUAL_UINT(3u, m.got_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(out, m.got, 3u);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
}

static void test_a_read_asks_for_every_byte_then_stops(void)
{
    static const uint16_t want[] = { CMD(4, (DEV_ADDR << 1) | 1), CMD(1, 3), CMD(2, 0) };
    uint8_t in[4];

    memcpy(m.answer, "\x11\x22\x33\x44", 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, NULL, 0u, in, 4u));
    assert_log(want, 3u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(m.answer, in, 4u);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

static void test_a_write_then_read_is_a_repeated_start(void)
{
    static const uint8_t out[] = { 0x30 };
    static const uint16_t want[] = {
        CMD(4, DEV_ADDR << 1), CMD(0, 0x30), CMD(4, (DEV_ADDR << 1) | 1), CMD(1, 1), CMD(2, 0)
    };
    uint8_t in[2];

    memcpy(m.answer, "\xA0\x5B", 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, in, 2u));
    assert_log(want, 5u);
    TEST_ASSERT_EQUAL_HEX8(0xA0, in[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5B, in[1]);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

static void test_an_unanswered_address_is_nack_and_the_bus_is_stopped(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x35u, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_FALSE(m.mbf);
    TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[m.log_n - 1u]);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
    TEST_ASSERT_EQUAL_UINT(0u, m.got_n);
}

static void test_an_unanswered_read_address_is_nack_and_stopped(void)
{
    uint8_t in[2];

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x35u, NULL, 0u, in, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_FALSE(m.mbf);
}

static void test_a_refused_data_byte_is_nack_and_the_bus_is_stopped(void)
{
    static const uint8_t out[] = { 0x01, 0x02, 0x03 };
    uint8_t in[1];

    m.nack_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 3u, in, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, m.got_n);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_FALSE(m.mbf);
    TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[m.log_n - 1u]);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
}

static void test_a_refused_last_byte_is_nack_and_stopped(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.nack_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 19-23 promise, and msr_error (lines 52-67) maps, each error
 * flag to its own status: lost arbitration BUSY, pin low timeout TIMEOUT,
 * FIFO error INTERNAL. Each one ends the transfer with a STOP. */
static void test_each_error_flag_is_its_own_status_and_stopped(void)
{
    static const uint8_t out[] = { 0x01, 0x02, 0x03 };
    static const struct {
        uint32_t   flag;
        tfw_status_t st;
    } rows[] = {
        { TFW_MCXN_LPI2C_MSR_ALF, TFW_ERR_BUSY },
        { TFW_MCXN_LPI2C_MSR_PLTF, TFW_ERR_TIMEOUT },
        { TFW_MCXN_LPI2C_MSR_FEF, TFW_ERR_INTERNAL },
    };

    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        setUp();
        m.nack_at = 1;
        m.refusal = rows[i].flag;
        TEST_ASSERT_EQUAL(rows[i].st, tfw_hal_i2c_xfer(DEV_ADDR, out, 3u, NULL, 0u));
        TEST_ASSERT_EQUAL_UINT(1u, m.got_n);
        TEST_ASSERT_EQUAL_UINT(1u, m.stops);
        TEST_ASSERT_FALSE(m.mbf);
        TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[m.log_n - 1u]);
        TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
    }
}

/* The controller runs the queue while the driver writes it: a refusal lands
 * after later commands, or the STOP itself, are queued and held behind it.
 * The driver empties the FIFO, so nothing queued after the refusal is sent,
 * and its own STOP completes. */
static void assert_late_refusal_stopped(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_FALSE(m.mbf);
    TEST_ASSERT_EQUAL_UINT(0u, m.fifo_n);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
    TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[m.log_n - 1u]);
}

static void test_an_address_refused_after_the_stop_is_queued_is_nack(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.lag = 4;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x35u, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[3]);     /* STOP queued behind it */
    TEST_ASSERT_EQUAL_UINT(0u, m.got_n);
    assert_late_refusal_stopped();
}

static void test_a_byte_refused_after_the_stop_is_queued_is_nack(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.lag = 4;
    m.nack_at = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_HEX16(CMD(2, 0), m.log[3]);
    TEST_ASSERT_EQUAL_UINT(0u, m.got_n);
    assert_late_refusal_stopped();
}

static void test_a_read_address_refused_behind_its_receive_is_nack(void)
{
    uint8_t in[2];

    m.lag = 4;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x35u, NULL, 0u, in, 2u));
    TEST_ASSERT_EQUAL_HEX16(CMD(1, 1), m.log[1]);     /* RX queued behind it */
    TEST_ASSERT_EQUAL_UINT(0u, m.answer_pos);
    assert_late_refusal_stopped();
}

static void test_a_bus_that_never_comes_free_is_a_timeout_untouched(void)
{
    static const uint8_t out[] = { 0x01 };

    m.bbf_other = -1;          /* for good */
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.log_n);
    TEST_ASSERT_EQUAL_UINT(0u, m.stops);
    TEST_ASSERT_EQUAL_UINT32(I2C_WAIT_US, delayed_us);
}

static void test_a_bus_that_comes_free_in_time_is_used(void)
{
    static const uint8_t out[] = { 0x01 };

    m.bbf_other = 5;           /* busy for five polls */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(5u, delayed_us);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

/* The controller takes the START and then nothing more: SCL held low by a
 * device, as the master sees it. The transfer times out, and the STOP it asks
 * for cannot be queued either. */
static void test_a_stuck_controller_is_a_timeout(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.accept = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, m.log_n);
    TEST_ASSERT_TRUE(delayed_us >= I2C_WAIT_US);
}

/* A device holds SCL through a write, then lets go. The stalled transfer is a
 * timeout that resets the controller, its timing put back; the next one
 * starts on an idle bus with a START of its own, not a repeated START of the
 * old transfer. */
static void test_a_stalled_transfer_is_aborted_and_the_next_starts_clean(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };
    static const uint16_t want[] = {
        CMD(4, DEV_ADDR << 1), CMD(0, 0x01), CMD(0, 0x02), CMD(2, 0)
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000u));
    m.resets = 0u;
    m.accept = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_LPI2C_MCR_MEN, m.mcr);
    TEST_ASSERT_EQUAL_HEX32((29u << 24) | (59u << 16) | (55u << 8) | 61u, m.mccr0);
    TEST_ASSERT_EQUAL_HEX32(0u, m.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(3u << 24, m.mcfgr2);

    m.accept = -1;             /* the line let go */
    m.log_n = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    assert_log(want, 4u);
    TEST_ASSERT_EQUAL_UINT(0u, m.restarts);
    TEST_ASSERT_EQUAL_UINT(2u, m.got_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(out, m.got, 2u);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

/* This master still holding the bus is a transfer never ended, no bus to
 * START on: a timeout with nothing queued. */
static void test_a_bus_this_master_still_holds_is_not_started_on(void)
{
    static const uint8_t out[] = { 0x01 };

    m.mbf = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.log_n);
    TEST_ASSERT_EQUAL_UINT(0u, m.restarts);
}

/* hal_bus.c 216-217: a flag left from the last transfer is cleared at the
 * start, so the first push does not read a stale ALF as this transfer's. */
static void test_a_flag_left_from_before_is_cleared_at_the_start(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };
    static const uint16_t want[] = {
        CMD(4, DEV_ADDR << 1), CMD(0, 0x01), CMD(0, 0x02), CMD(2, 0)
    };

    m.flags = TFW_MCXN_LPI2C_MSR_ALF;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    assert_log(want, 4u);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags);
}

/* hal_bus.c 218-219 (RRF): a byte left in the receive FIFO is dropped at
 * the start, not handed to the next read as its first byte. */
static void test_a_byte_left_in_the_receive_fifo_is_not_read(void)
{
    uint8_t in[2];

    m.rx[0] = 0xEEu;
    m.rx_head = 1u;
    memcpy(m.answer, "\x11\x22", 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, NULL, 0u, in, 2u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(m.answer, in, 2u);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

/* hal_bus.c 218-219 (RTF): a command left in the transmit FIFO is dropped
 * at the start, so it does not run ahead of the new START. */
static void test_a_command_left_in_the_transmit_fifo_is_not_run(void)
{
    static const uint8_t out[] = { 0x01 };
    static const uint16_t want[] = { CMD(4, DEV_ADDR << 1), CMD(0, 0x01), CMD(2, 0) };

    m.fifo[0] = CMD(4, 0x50u << 1);    /* a START to another address */
    m.fifo_n = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, NULL, 0u));
    assert_log(want, 3u);
    TEST_ASSERT_EQUAL_UINT(0u, m.restarts);
    TEST_ASSERT_EQUAL_UINT(1u, m.got_n);
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
}

/* hal_bus.c 154-171: a STOP that is queued but never shows is a timeout,
 * and the master is reset so the next transfer starts clean. */
static void test_a_stop_that_never_shows_is_a_timeout_and_aborted(void)
{
    static const uint8_t out[] = { 0x01 };

    m.lag = 1000000;                   /* nothing queued runs within the wait */
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(3u, m.log_n);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_EQUAL_UINT(0u, m.fifo_n);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 145-152: a STOP that cannot be queued after the last byte
 * is a timeout, waited for once, and the master is reset. */
static void test_a_stop_that_cannot_be_queued_is_a_timeout_waited_once(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.accept = 3;                      /* START and both bytes, no STOP */
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(3u, m.log_n);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
    TEST_ASSERT_TRUE(delayed_us < 2u * I2C_WAIT_US);
}

/* hal_bus.c lines 137-152: a refused byte whose STOP then cannot be queued
 * keeps its NACK, and the master is reset. */
static void test_a_refused_byte_whose_stop_cannot_be_queued_is_nack(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.nack_at = 0;
    m.accept = 2;                      /* START and the first byte, no STOP */
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(2u, m.log_n);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 137-171: a refused byte whose STOP runs but never shows
 * keeps its NACK, and the master is reset. */
static void test_a_refused_byte_whose_stop_never_shows_is_nack(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.nack_at = 0;
    m.stop_lost = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.stops);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 137-152: a flag the bus raises again after the error flush
 * makes the STOP's push fail busy; the refused byte keeps its NACK. */
static void test_a_refused_byte_keeps_its_nack_when_the_stop_meets_a_new_flag(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.nack_at = 0;
    m.after_clear = TFW_MCXN_LPI2C_MSR_ALF;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.stops);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 154-171: a STOP that loses arbitration after a refusal,
 * no SDF and the bus let go, leaves the refused byte its NACK. */
static void test_a_refused_byte_keeps_its_nack_when_its_stop_raises_a_flag(void)
{
    static const uint8_t out[] = { 0x01, 0x02 };

    m.nack_at = 0;
    m.stop_flag = TFW_MCXN_LPI2C_MSR_ALF;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV_ADDR, out, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.stops);
    TEST_ASSERT_EQUAL_UINT(1u, m.resets);
    TEST_ASSERT_FALSE(m.mbf);
}

/* hal_bus.c lines 137-143: a read that fails with its bytes still in the
 * receive FIFO has them flushed with the error, not left for the next read. */
static void test_a_failed_read_flushes_the_receive_fifo(void)
{
    uint8_t in[2];

    m.lag = 1;
    m.answer[0] = 0x5A;
    m.answer[1] = 0xA5;
    m.rx_flag = TFW_MCXN_LPI2C_MSR_FEF;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_i2c_xfer(DEV_ADDR, NULL, 0u, in, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, m.stops);
    TEST_ASSERT_EQUAL_UINT(m.rx_head, m.rx_tail);
}

static void test_an_empty_transfer_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV_ADDR, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, m.log_n);
    TEST_ASSERT_EQUAL_HEX32(0u, m.mcr);
}

static void test_spi_and_onewire_are_unsupported(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_init(0u, 4u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(NULL, &b, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(NULL, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(NULL, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(NULL, &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_100k_from_fro12m);
    RUN_TEST(test_400k_from_fro12m);
    RUN_TEST(test_1m_from_fro12m);
    RUN_TEST(test_a_slow_rate_takes_a_prescale);
    RUN_TEST(test_a_tie_goes_to_the_smaller_prescale);
    RUN_TEST(test_the_cycle_ceiling_is_inclusive);
    RUN_TEST(test_a_rate_out_of_reach_is_refused);
    RUN_TEST(test_the_five_percent_bound_is_inclusive);
    RUN_TEST(test_the_slowest_rate_takes_the_last_prescale);
    RUN_TEST(test_init_routes_clocks_pins_and_the_unit);
    RUN_TEST(test_init_refuses_a_rate_it_cannot_time);
    RUN_TEST(test_a_write_is_start_bytes_and_stop);
    RUN_TEST(test_a_read_asks_for_every_byte_then_stops);
    RUN_TEST(test_a_write_then_read_is_a_repeated_start);
    RUN_TEST(test_an_unanswered_address_is_nack_and_the_bus_is_stopped);
    RUN_TEST(test_an_unanswered_read_address_is_nack_and_stopped);
    RUN_TEST(test_a_refused_data_byte_is_nack_and_the_bus_is_stopped);
    RUN_TEST(test_a_refused_last_byte_is_nack_and_stopped);
    RUN_TEST(test_each_error_flag_is_its_own_status_and_stopped);
    RUN_TEST(test_an_address_refused_after_the_stop_is_queued_is_nack);
    RUN_TEST(test_a_byte_refused_after_the_stop_is_queued_is_nack);
    RUN_TEST(test_a_read_address_refused_behind_its_receive_is_nack);
    RUN_TEST(test_a_bus_that_never_comes_free_is_a_timeout_untouched);
    RUN_TEST(test_a_bus_that_comes_free_in_time_is_used);
    RUN_TEST(test_a_stuck_controller_is_a_timeout);
    RUN_TEST(test_a_stalled_transfer_is_aborted_and_the_next_starts_clean);
    RUN_TEST(test_a_bus_this_master_still_holds_is_not_started_on);
    RUN_TEST(test_a_flag_left_from_before_is_cleared_at_the_start);
    RUN_TEST(test_a_byte_left_in_the_receive_fifo_is_not_read);
    RUN_TEST(test_a_command_left_in_the_transmit_fifo_is_not_run);
    RUN_TEST(test_a_stop_that_never_shows_is_a_timeout_and_aborted);
    RUN_TEST(test_a_stop_that_cannot_be_queued_is_a_timeout_waited_once);
    RUN_TEST(test_a_refused_byte_whose_stop_cannot_be_queued_is_nack);
    RUN_TEST(test_a_refused_byte_whose_stop_never_shows_is_nack);
    RUN_TEST(test_a_refused_byte_keeps_its_nack_when_the_stop_meets_a_new_flag);
    RUN_TEST(test_a_refused_byte_keeps_its_nack_when_its_stop_raises_a_flag);
    RUN_TEST(test_a_failed_read_flushes_the_receive_fifo);
    RUN_TEST(test_an_empty_transfer_touches_nothing);
    RUN_TEST(test_spi_and_onewire_are_unsupported);
    return UNITY_END();
}
