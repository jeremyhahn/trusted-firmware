/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7's LPI2C master (src/hal/cortex-m/imx95/hal_bus.c),
 * unchanged, against a register model of one LPI2C instance in this file:
 * MCR, MSR, MIER, MCFGR1, MCCR0, MFCR, MFSR, MTDR and MRDR, the command
 * FIFO behind MTDR, the receive FIFO behind MRDR, and the NDF, ALF, FEF, SDF
 * and EPF flags. A target at one address answers on the modelled bus.
 *
 * The registers are reached as test_imx95_hal reaches the SoC's: mk/host.mk
 * puts test/model/cortex-m/ ahead of src/hal/cortex-m/, so TFW_CM_REG calls
 * tfw_imx95_model_reg, which this file defines. Each access is settled at the
 * next one (as test/model/imx95_model.c does): a value the code changed is a
 * write, MTDR is write-only and MRDR read-only. The offsets and bits are
 * written out here, not taken from lpi2c.h, so a wrong one there fails here.
 * Every offset is the Linux driver's (drivers/i2c/busses/i2c-imx-lpi2c.c,
 * docs/reference/README.md), TO CONFIRM against IMX95RM.
 *
 * The instance's base is zero on the board (board_profile.h), so the public
 * calls are tested to refuse; the transfers go through the base-taking calls
 * lpi2c.h declares, at a base of the test's own.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "lpi2c.h"

#define BASE 0x44340000u

#define O_MCR    0x10u
#define O_MSR    0x14u
#define O_MIER   0x18u
#define O_MCFGR1 0x24u
#define O_MCCR0  0x48u
#define O_MFCR   0x58u
#define O_MFSR   0x5Cu
#define O_MTDR   0x60u
#define O_MRDR   0x70u

#define MCR_MEN  (1u << 0)
#define MCR_RST  (1u << 1)
#define MCR_RTF  (1u << 8)
#define MCR_RRF  (1u << 9)

#define MSR_TDF  (1u << 0)
#define MSR_RDF  (1u << 1)
#define MSR_EPF  (1u << 8)
#define MSR_SDF  (1u << 9)
#define MSR_NDF  (1u << 10)
#define MSR_ALF  (1u << 11)
#define MSR_FEF  (1u << 12)
#define MSR_W1C  (MSR_EPF | MSR_SDF | MSR_NDF | MSR_ALF | MSR_FEF)
#define MSR_HALT (MSR_NDF | MSR_ALF | MSR_FEF)

#define MRDR_RXEMPTY (1u << 14)

#define CMD_TX    0u
#define CMD_RX    1u
#define CMD_STOP  2u
#define CMD_START 4u
#define CMD(c, d) ((uint32_t)(((c) << 8) | (d)))

#define TARGET 0x50u
#define FIFO   64u

static struct {
    uint32_t mcr, msr, mier, mcfgr1, mccr0, mfcr;
    uint32_t txq[FIFO];   /* commands written, not yet run */
    unsigned txn;
    uint8_t  rxq[FIFO];
    unsigned rxn;
    uint32_t log[FIFO];   /* every MTDR write, in order */
    unsigned logn;
    unsigned flushes;     /* MCR writes with RTF and RRF */
    unsigned stops;       /* STOPs the bus saw */
    unsigned unmodelled;
    unsigned accesses;    /* every register access */
    int      stall;       /* the bus never runs a command */
    int      nack_addr;   /* the target does not answer */
    int      nack_at;     /* the data byte the target refuses, -1 none */
    int      nack_late;   /* that NACK reaches MSR only with the STOP */
    int      ndf_held;    /* a late NACK not yet in MSR */
    unsigned busy;        /* 1 between a START and its STOP */
    uint8_t  addressed;   /* the address byte of the last START */
    uint8_t  got[FIFO];   /* bytes the target received */
    unsigned gotn;
    uint8_t  give[FIFO];  /* bytes the target sends */
    unsigned given;
} md;

enum reg { R_NONE, R_OTHER, R_MCR, R_MSR, R_MIER, R_MCFGR1, R_MCCR0, R_MFCR, R_MFSR, R_MTDR, R_MRDR };

static enum reg pending;
static uint32_t presented;
static uint32_t cell;

/* Run the queued commands while the bus is not halted by a flag. */
static void run(void)
{
    while (!md.stall && md.txn && !(md.msr & MSR_HALT) && (md.mcr & MCR_MEN)) {
        uint32_t c   = md.txq[0];
        uint32_t op  = (c >> 8) & 7u;
        uint8_t  d   = (uint8_t)c;
        unsigned i;

        memmove(md.txq, md.txq + 1, (md.txn - 1u) * sizeof md.txq[0]);
        md.txn--;
        if (op == CMD_START) {
            md.busy      = 1u;
            md.addressed = d;
            if (md.nack_addr || (d >> 1) != TARGET) {
                md.msr |= MSR_NDF;
            }
        } else if (op == CMD_TX) {
            if (md.nack_at >= 0 && (unsigned)md.nack_at == md.gotn) {
                if (md.nack_late) {
                    md.ndf_held = 1;
                } else {
                    md.msr |= MSR_NDF;
                }
            }
            if (md.gotn < FIFO) {
                md.got[md.gotn++] = d;
            }
        } else if (op == CMD_RX) {
            for (i = 0u; i <= d && md.rxn < FIFO; i++) {
                md.rxq[md.rxn++] = md.give[md.given++ % FIFO];
            }
        } else if (op == CMD_STOP) {
            if (md.busy) {
                md.busy = 0u;
                md.stops++;
                md.msr |= MSR_SDF;
                if (md.ndf_held) {
                    md.ndf_held = 0;
                    md.msr |= MSR_NDF;
                }
            }
        }
    }
}

static uint32_t view(enum reg r)
{
    switch (r) {
    case R_MCR:    return md.mcr;
    case R_MSR:    return md.msr | (md.txn == 0u ? MSR_TDF : 0u) | (md.rxn ? MSR_RDF : 0u);
    case R_MIER:   return md.mier;
    case R_MCFGR1: return md.mcfgr1;
    case R_MCCR0:  return md.mccr0;
    case R_MFCR:   return md.mfcr;
    case R_MFSR:   return (uint32_t)md.txn | ((uint32_t)md.rxn << 16);
    case R_MRDR:   return md.rxn ? md.rxq[0] : MRDR_RXEMPTY;
    default:       return 0u;
    }
}

static void on_write(enum reg r, uint32_t v)
{
    switch (r) {
    case R_MCR:
        if (v & MCR_RTF) {
            md.txn = 0u;
        }
        if (v & MCR_RRF) {
            md.rxn = 0u;
        }
        if ((v & (MCR_RTF | MCR_RRF)) == (MCR_RTF | MCR_RRF)) {
            md.flushes++;
        }
        if (v & MCR_RST) {
            md.msr = md.mier = md.mcfgr1 = md.mccr0 = md.mfcr = 0u;
            md.txn = md.rxn = 0u;
        }
        md.mcr = v & ~(MCR_RTF | MCR_RRF);
        break;
    case R_MSR:    md.msr &= ~(v & MSR_W1C); break;
    case R_MIER:   md.mier = v; break;
    case R_MCFGR1: md.mcfgr1 = v; break;
    case R_MCCR0:  md.mccr0 = v; break;
    case R_MFCR:   md.mfcr = v; break;
    case R_MTDR:
        if (md.logn < FIFO) {
            md.log[md.logn++] = v;
        }
        if (md.txn < FIFO) {
            md.txq[md.txn++] = v;
        }
        break;
    default: break;
    }
}

static void settle(void)
{
    uint32_t now = cell;

    if (pending == R_MTDR || (pending != R_MRDR && pending > R_OTHER && now != presented)) {
        on_write(pending, now);
    } else if (pending == R_MRDR && md.rxn) {
        memmove(md.rxq, md.rxq + 1, md.rxn - 1u);
        md.rxn--;
    }
    pending = R_NONE;
    run();
}

static enum reg decode(uintptr_t addr)
{
    if (addr < BASE || addr >= BASE + 0x1000u) {
        return R_OTHER;
    }
    switch ((uint32_t)(addr - BASE)) {
    case O_MCR:    return R_MCR;
    case O_MSR:    return R_MSR;
    case O_MIER:   return R_MIER;
    case O_MCFGR1: return R_MCFGR1;
    case O_MCCR0:  return R_MCCR0;
    case O_MFCR:   return R_MFCR;
    case O_MFSR:   return R_MFSR;
    case O_MTDR:   return R_MTDR;
    case O_MRDR:   return R_MRDR;
    default:       return R_OTHER;
    }
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    settle();
    md.accesses++;
    pending = decode(addr);
    if (pending == R_OTHER) {
        md.unmodelled++;
    }
    presented = view(pending);
    cell      = presented;
    return &cell;
}

static void sync(void)
{
    settle();
}

void setUp(void)
{
    memset(&md, 0, sizeof md);
    md.nack_at = -1;
    pending    = R_NONE;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_init(BASE, 24000000uL, 100000uL));
    sync();
    md.logn = 0u;
}

void tearDown(void)
{
    sync();
    TEST_ASSERT_EQUAL_UINT32(0u, md.unmodelled);
    TEST_ASSERT_EQUAL_HEX32(0u, md.mier);   /* polled: no interrupt */
}

static void test_lpi2c_write_acked(void)
{
    static const uint8_t wr[] = { 0x12u, 0x34u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_UINT(4u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, TARGET << 1), md.log[0]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_TX, 0x12u), md.log[1]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_TX, 0x34u), md.log[2]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[3]);
    TEST_ASSERT_EQUAL_UINT(2u, md.gotn);
    TEST_ASSERT_EQUAL_MEMORY(wr, md.got, 2u);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
    TEST_ASSERT_EQUAL_UINT(0u, md.flushes);
}

static void test_lpi2c_read(void)
{
    static const uint8_t want[] = { 0xA1u, 0xB2u, 0xC3u };
    uint8_t              rd[3]  = { 0 };

    memcpy(md.give, want, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_xfer(BASE, TARGET, NULL, 0u, rd, 3u));
    sync();
    TEST_ASSERT_EQUAL_UINT(3u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, (TARGET << 1) | 1u), md.log[0]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_RX, 2u), md.log[1]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[2]);
    TEST_ASSERT_EQUAL_MEMORY(want, rd, 3u);
    TEST_ASSERT_EQUAL_UINT(0u, md.rxn);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
}

/* The read follows the write on a repeated START: one STOP, at the end. */
static void test_lpi2c_write_then_read_repeated_start(void)
{
    static const uint8_t wr[]   = { 0x10u };
    static const uint8_t want[] = { 0x5Au, 0xA5u };
    uint8_t              rd[2]  = { 0 };

    memcpy(md.give, want, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 1u, rd, 2u));
    sync();
    TEST_ASSERT_EQUAL_UINT(5u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, TARGET << 1), md.log[0]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_TX, 0x10u), md.log[1]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, (TARGET << 1) | 1u), md.log[2]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_RX, 1u), md.log[3]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[4]);
    TEST_ASSERT_EQUAL_MEMORY(want, rd, 2u);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
}

/* No target answers: NACK, nothing after the address, FIFOs flushed, STOP. */
static void test_lpi2c_address_nack(void)
{
    static const uint8_t wr[] = { 0x01u, 0x02u };

    md.nack_addr = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_UINT(2u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, TARGET << 1), md.log[0]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[1]);
    TEST_ASSERT_EQUAL_UINT(0u, md.gotn);
    TEST_ASSERT_TRUE(md.flushes >= 1u);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
    TEST_ASSERT_EQUAL_HEX32(0u, md.msr & MSR_W1C);
}

static void test_lpi2c_read_address_nack(void)
{
    uint8_t rd[2] = { 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_imx95_lpi2c_xfer(BASE, TARGET + 1u, NULL, 0u, rd, 2u));
    sync();
    TEST_ASSERT_EQUAL_UINT(2u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[1]);
    TEST_ASSERT_EQUAL_UINT(0u, md.rxn);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
}

/* The target refuses the second of three bytes: the third is never sent. */
static void test_lpi2c_data_nack(void)
{
    static const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };

    md.nack_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 3u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_UINT(4u, md.logn);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_TX, 0x02u), md.log[2]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[3]);
    TEST_ASSERT_TRUE(md.flushes >= 1u);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
}

/* A NACK on the last byte shows after the FIFO drained, and still counts. */
static void test_lpi2c_data_nack_on_the_last_byte(void)
{
    static const uint8_t wr[] = { 0x01u, 0x02u };

    md.nack_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[md.logn - 1u]);
    TEST_ASSERT_TRUE(md.flushes >= 1u);
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
}

/* The last byte's NACK arrives after TDF drained, in the MSR read that
 * also shows SDF: the transfer reports the NACK, and the master still
 * works for the next transfer. */
static void test_lpi2c_late_nack_seen_with_stop(void)
{
    static const uint8_t wr[] = { 0x01u, 0x02u };

    md.nack_at   = 1;
    md.nack_late = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_UINT(1u, md.stops);
    TEST_ASSERT_TRUE(md.flushes >= 1u);
    TEST_ASSERT_EQUAL_HEX32(0u, md.msr & (MSR_HALT | MSR_SDF));

    md.nack_at = -1;
    md.gotn    = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_UINT(2u, md.stops);
    TEST_ASSERT_EQUAL_MEMORY(wr, md.got, 2u);
}

/* The bus never runs a command: TIMEOUT, the FIFOs flushed, a STOP asked. */
static void test_lpi2c_a_transfer_that_never_ends_times_out(void)
{
    static const uint8_t wr[] = { 0x01u, 0x02u };

    md.stall = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_imx95_lpi2c_xfer(BASE, TARGET, wr, 2u, NULL, 0u));
    sync();
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_START, TARGET << 1), md.log[0]);
    TEST_ASSERT_EQUAL_HEX32(CMD(CMD_STOP, 0u), md.log[md.logn - 1u]);
    TEST_ASSERT_TRUE(md.flushes >= 1u);
    TEST_ASSERT_EQUAL_UINT(0u, md.txn);
    TEST_ASSERT_EQUAL_UINT(0u, md.stops);
}

static void test_lpi2c_zero_length_touches_nothing(void)
{
    unsigned before = md.accesses;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_xfer(BASE, TARGET, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(before, md.accesses);
}

/* 100 kHz from a 24 MHz root: prescaler 4, a 57-count cycle split 38 low and
 * 19 high (Linux's lpi2c_imx_config() with the filters off). */
static void test_lpi2c_clock_at_24mhz(void)
{
    uint32_t c1 = 0u, c0 = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(24000000uL, 100000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(2u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x09131326u, c0);   /* DATAVD 9, SETHOLD 19, HI 19, LO 38 */
    /* setUp's init programmed the same, then enabled the master. */
    TEST_ASSERT_EQUAL_UINT32(2u, md.mcfgr1);
    TEST_ASSERT_EQUAL_HEX32(0x09131326u, md.mccr0);
    TEST_ASSERT_EQUAL_HEX32(MCR_MEN, md.mcr);
    TEST_ASSERT_EQUAL_HEX32(0u, md.mfcr);
}

/* From a 16 MHz root: prescaler 2, a 77-count cycle split 51 and 26. */
static void test_lpi2c_clock_at_16mhz(void)
{
    uint32_t c1 = 0u, c0 = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(16000000uL, 100000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(1u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x0D1A1A33u, c0);
}

static void test_lpi2c_clock_refuses_a_rate_it_cannot_make(void)
{
    uint32_t c1 = 0xEEu, c0 = 0xEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpi2c_clock(24000000uL, 0u, &c1, &c0));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpi2c_clock(1000000uL, 400000uL, &c1, &c0));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpi2c_clock(24000000uL, 1000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(0xEEu, c1);
    TEST_ASSERT_EQUAL_UINT32(0xEEu, c0);
}

/* The board's base is zero: every call refuses and no register is touched. */
static void test_base_zero_refuses(void)
{
    static const uint8_t wr[] = { 0x01u };
    uint8_t              rd[1];
    unsigned             before = md.accesses;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_i2c_init(100000uL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_i2c_xfer(TARGET, wr, 1u, rd, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_imx95_lpi2c_init(0u, 24000000uL, 100000uL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_imx95_lpi2c_xfer(0u, TARGET, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(before, md.accesses);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lpi2c_write_acked);
    RUN_TEST(test_lpi2c_read);
    RUN_TEST(test_lpi2c_write_then_read_repeated_start);
    RUN_TEST(test_lpi2c_address_nack);
    RUN_TEST(test_lpi2c_read_address_nack);
    RUN_TEST(test_lpi2c_data_nack);
    RUN_TEST(test_lpi2c_data_nack_on_the_last_byte);
    RUN_TEST(test_lpi2c_late_nack_seen_with_stop);
    RUN_TEST(test_lpi2c_a_transfer_that_never_ends_times_out);
    RUN_TEST(test_lpi2c_zero_length_touches_nothing);
    RUN_TEST(test_lpi2c_clock_at_24mhz);
    RUN_TEST(test_lpi2c_clock_at_16mhz);
    RUN_TEST(test_lpi2c_clock_refuses_a_rate_it_cannot_make);
    RUN_TEST(test_base_zero_refuses);
    return UNITY_END();
}
