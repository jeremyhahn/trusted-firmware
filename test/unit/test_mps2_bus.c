/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 I2C master (src/hal/cortex-m/mps2/hal_bus.c), bit-banged over the
 * SBCon, against a simulated bus in place of sbcon.c: the two open-collector
 * lines as the wired AND of the master's and one device's, and the device a
 * slave that watches the edges -- START and STOP while SCL is high, a bit
 * sampled on each rising SCL, its own SDA moved on each falling one.
 *
 * The device writes what it saw to a log, so each test reads as the bus
 * would on an analyser: S and Sr, the address byte and the device's A or N,
 * the bytes written, <xx for each byte the device sent and the master's A or
 * N after it, and P.
 *
 * The SPI master in the same file drives a PL022, modelled in place of
 * pl022.c: the register writes init makes, checked against DDI 0194H's
 * SSPCR0/SSPCR1 layout, and the shield 0 select (MISC bit 8) around each
 * transfer.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "sbcon.h"
#include "pl022.h"

#define SB_BOTH   (TFW_SB_SCL | TFW_SB_SDA)
#define FOREVER   0xFFFFu

enum { IDLE, RX, ACK_OUT, TX, ACK_IN, DONE };

static struct {
    uint32_t       m;            /* the lines the master has released */
    uint8_t        s_sda;        /* the device's SDA, 1 released */
    uint8_t        state;
    uint8_t        shift;
    uint8_t        nbits;
    uint8_t        is_addr;
    uint8_t        rw;
    uint8_t        master_ack;
    uint8_t        wcount;
    uint8_t        txi;
    /* the device */
    uint8_t        addr7;
    const uint8_t *tx;
    uint8_t        txlen;
    uint8_t        nack_write;   /* the data byte, 1-based, it refuses; 0 none */
    uint16_t       hold_sda;     /* SCL rises it holds SDA low through */
    uint16_t       hold_scl;     /* half periods it holds SCL low for */
    /* what the test reads */
    unsigned       scl_rises;
    unsigned       delays;
    uint16_t       delay_min;
    uint16_t       delay_max;
    char           log[256];
} bus;

static void say(const char *s)
{
    if (bus.log[0] != '\0') {
        strncat(bus.log, " ", sizeof bus.log - strlen(bus.log) - 1u);
    }
    strncat(bus.log, s, sizeof bus.log - strlen(bus.log) - 1u);
}

static void say_byte(const char *prefix, uint8_t b)
{
    char s[8];

    (void)snprintf(s, sizeof s, "%s%02X", prefix, (unsigned)b);
    say(s);
}

static uint32_t lines(void)
{
    uint32_t v = bus.m;

    if (!bus.s_sda || bus.hold_sda) {
        v &= ~TFW_SB_SDA;
    }
    if (bus.hold_scl) {
        v &= ~TFW_SB_SCL;
    }
    return v;
}

static void tx_load(void)
{
    uint8_t b = (bus.txi < bus.txlen) ? bus.tx[bus.txi] : 0xFFu;

    say_byte("<", b);
    bus.shift = b;
    bus.s_sda = (uint8_t)((b >> 7) & 1u);
    bus.nbits = 1u;
    bus.state = TX;
}

static void on_rise(uint8_t sda)
{
    bus.scl_rises++;
    if (bus.hold_sda && bus.hold_sda != FOREVER) {
        bus.hold_sda--;
    }
    if (bus.state == RX) {
        bus.shift = (uint8_t)((bus.shift << 1) | sda);
        bus.nbits++;
    } else if (bus.state == ACK_IN) {
        bus.master_ack = (uint8_t)!sda;
        say(bus.master_ack ? "A" : "N");
    }
}

static void on_fall(void)
{
    uint8_t ack;

    switch (bus.state) {
    case RX:
        if (bus.nbits < 8u) {
            break;
        }
        say_byte("", bus.shift);
        if (bus.is_addr) {
            bus.rw = (uint8_t)(bus.shift & 1u);
            ack = (uint8_t)((bus.shift >> 1) == bus.addr7);
        } else {
            bus.wcount++;
            ack = (uint8_t)!(bus.nack_write && bus.wcount == bus.nack_write);
        }
        say(ack ? "A" : "N");
        bus.s_sda = (uint8_t)!ack;
        bus.state = ack ? ACK_OUT : DONE;
        break;
    case ACK_OUT:
        bus.s_sda = 1u;
        if (bus.is_addr && bus.rw) {
            tx_load();
        } else {
            bus.state = RX;
            bus.is_addr = 0u;
            bus.shift = 0u;
            bus.nbits = 0u;
        }
        break;
    case TX:
        if (bus.nbits < 8u) {
            bus.s_sda = (uint8_t)(((unsigned)bus.shift >> (7u - bus.nbits)) & 1u);
            bus.nbits++;
        } else {
            bus.s_sda = 1u;
            bus.state = ACK_IN;
        }
        break;
    case ACK_IN:
        if (bus.master_ack) {
            bus.txi++;
            tx_load();
        } else {
            bus.state = DONE;
        }
        break;
    default:
        break;
    }
}

/* The master moved a line: what did the device see? */
static void master_sets(uint32_t m)
{
    uint32_t before = lines();
    uint32_t after;

    bus.m = m;
    after = lines();
    if ((before & after & TFW_SB_SCL) && (before & TFW_SB_SDA) != (after & TFW_SB_SDA)) {
        if (after & TFW_SB_SDA) {
            say("P");
            bus.state = IDLE;
        } else {
            say(bus.state == IDLE ? "S" : "Sr");
            bus.state = RX;
            bus.is_addr = 1u;
            bus.shift = 0u;
            bus.nbits = 0u;
        }
        bus.s_sda = 1u;
    } else if (!(before & TFW_SB_SCL) && (after & TFW_SB_SCL)) {
        on_rise((uint8_t)((after & TFW_SB_SDA) != 0u));
    } else if ((before & TFW_SB_SCL) && !(after & TFW_SB_SCL)) {
        on_fall();
    }
}

/* ---- sbcon.h and tfw_hal_delay_us, as hal_bus.c links them */

uint32_t tfw_mps2_sb_lines(void)          { return lines(); }
void     tfw_mps2_sb_release(uint32_t m)   { master_sets(bus.m | m); }
void     tfw_mps2_sb_drive_low(uint32_t m) { master_sets(bus.m & ~m); }

void tfw_hal_delay_us(uint16_t us)
{
    bus.delays++;
    if (us < bus.delay_min) { bus.delay_min = us; }
    if (us > bus.delay_max) { bus.delay_max = us; }
    if (bus.hold_scl && bus.hold_scl != FOREVER) {
        bus.hold_scl--;
    }
}

/* ---- the PL022 and MISC, modelled in place of pl022.c. A frame goes out on
 * each SSPDR write while SSE is set, and the device's answer to it lands in
 * the receive FIFO at once, unless the port is stalled. The log reads as the
 * shield's pins would: [ when MISC bit 8 falls, >xx for each byte shifted out
 * (>xx! if no device was selected), <xx for its answer, ] when bit 8 rises. */
static struct {
    uint32_t       cr0, cr1, cpsr;
    uint32_t       misc;
    uint8_t        rx[8];
    uint8_t        rxn;
    uint8_t        stalled;      /* frames never complete */
    const uint8_t *ans;          /* the device's answers, in order */
    uint8_t        ansn, ansi;
    unsigned       dr_writes;
    char           writes[128];  /* the register writes, in order */
    char           log[128];
} ssp;

/* Append tag, then v in digits hex digits when digits is not 0. */
static void ssp_say(char *s, size_t cap, const char *tag, int digits, unsigned v)
{
    size_t n = strlen(s);

    if (digits) {
        (void)snprintf(s + n, cap - n, "%s%0*X", tag, digits, v);
    } else {
        (void)snprintf(s + n, cap - n, "%s", tag);
    }
}

uint32_t tfw_mps2_ssp_read(uint32_t off)
{
    uint8_t b;

    if (off == TFW_SSP_SR) {
        return (ssp.stalled ? 0u : TFW_SSP_SR_TNF)
               | (ssp.rxn ? TFW_SSP_SR_RNE : 0u);
    }
    if (off == TFW_SSP_DR && ssp.rxn) {
        b = ssp.rx[0];
        memmove(ssp.rx, ssp.rx + 1, --ssp.rxn);
        return b;
    }
    return 0u;
}

void tfw_mps2_ssp_write(uint32_t off, uint32_t v)
{
    uint8_t a;

    if (off == TFW_SSP_DR) {
        ssp.dr_writes++;
        if (!(ssp.cr1 & TFW_SSP_CR1_SSE) || ssp.stalled) {
            return;
        }
        ssp_say(ssp.log, sizeof ssp.log, ">", 2, v & 0xFFu);
        if (ssp.misc & TFW_MPS2_MISC_SHIELD0_NCS) {
            ssp_say(ssp.log, sizeof ssp.log, "!", 0, 0u);
        }
        a = ssp.ansi < ssp.ansn ? ssp.ans[ssp.ansi++] : 0xFFu;
        ssp_say(ssp.log, sizeof ssp.log, "<", 2, a);
        ssp.rx[ssp.rxn++] = a;
        return;
    }
    ssp_say(ssp.writes, sizeof ssp.writes,
            off == TFW_SSP_CR0 ? " CR0=" : off == TFW_SSP_CR1 ? " CR1="
            : off == TFW_SSP_CPSR ? " CPSR=" : " ?=", 4, v);
    if (off == TFW_SSP_CR0) { ssp.cr0 = v; }
    if (off == TFW_SSP_CR1) { ssp.cr1 = v; }
    if (off == TFW_SSP_CPSR) { ssp.cpsr = v; }
}

uint32_t tfw_mps2_misc_read(void)
{
    return ssp.misc;
}

void tfw_mps2_misc_write(uint32_t v)
{
    if ((ssp.misc ^ v) & TFW_MPS2_MISC_SHIELD0_NCS) {
        ssp_say(ssp.log, sizeof ssp.log,
                (v & TFW_MPS2_MISC_SHIELD0_NCS) ? "]" : "[", 0, 0u);
    }
    ssp_say(ssp.writes, sizeof ssp.writes, " MISC=", 3, v);
    ssp.misc = v;
}

/* The port after init in mode 0 at the fastest divisor, its logs cleared. */
static void ssp_ready(uint8_t lsb_first)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_init(0u, 0u, lsb_first));
    ssp.writes[0] = '\0';
    ssp.log[0] = '\0';
}

static const uint8_t device_bytes[3] = { 0x55u, 0x66u, 0x77u };

static void quiet(void)
{
    bus.scl_rises = 0u;
    bus.delays = 0u;
    bus.delay_min = 0xFFFFu;
    bus.delay_max = 0u;
    bus.log[0] = '\0';
}

void setUp(void)
{
    memset(&bus, 0, sizeof bus);
    bus.m = SB_BOTH;
    bus.s_sda = 1u;
    bus.addr7 = 0x50u;
    bus.tx = device_bytes;
    bus.txlen = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    quiet();
    memset(&ssp, 0, sizeof ssp);
    /* MISC with the shield 0 select low and other bits set, which every write
     * must keep: bit 9 the shield 1 select, the rest the LCD's. */
    ssp.misc = 0x2A5u;
}
void tearDown(void) {}

static void test_a_write_then_read_is_start_bytes_repeated_start_and_stop(void)
{
    uint8_t rd[2] = { 0u, 0u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, rd, 2u));
    TEST_ASSERT_EQUAL_STRING("S A0 A 01 A Sr A1 A <55 A <66 N P", bus.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(device_bytes, rd, 2u);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

static void test_a_write_only_transfer_ends_in_stop(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x10u, 0x20u }, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("S A0 A 10 A 20 A P", bus.log);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

/* Every byte the master reads is ACKed but the last, whose NACK tells the
 * device to let go of SDA before the STOP. */
static void test_a_read_acks_every_byte_but_the_last(void)
{
    uint8_t rd[3] = { 0u, 0u, 0u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 3u));
    TEST_ASSERT_EQUAL_STRING("S A1 A <55 A <66 A <77 N P", bus.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(device_bytes, rd, 3u);
}

/* The AVR driver's code for the same case: TFW_ERR_NACK, after a STOP, and the
 * read the caller asked for is never started. */
static void test_an_unanswered_address_is_nack_and_the_bus_is_stopped(void)
{
    uint8_t rd[1] = { 0xEEu };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x51u, (const uint8_t[]){ 0x01u }, 1u, rd, 1u));
    TEST_ASSERT_EQUAL_STRING("S A2 N P", bus.log);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[0]);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());

    quiet();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x51u, NULL, 0u, rd, 1u));
    TEST_ASSERT_EQUAL_STRING("S A3 N P", bus.log);
}

static void test_a_refused_data_byte_is_nack_and_the_bus_is_stopped(void)
{
    uint8_t rd[1] = { 0xEEu };

    bus.nack_write = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK,
        tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u, 0x02u }, 2u, rd, 1u));
    TEST_ASSERT_EQUAL_STRING("S A0 A 01 N P", bus.log);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[0]);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

/* A device left mid-byte holding SDA is clocked until it lets go, and the
 * transfer then goes ahead (UM10204 3.1.16, bus clear). */
static void test_sda_held_low_is_clocked_free_before_the_start(void)
{
    bus.hold_sda = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("S A0 A 01 A P", bus.log);
}

/* Held through all nine clocks is a stuck bus: TFW_ERR_TIMEOUT, as the AVR
 * driver's bounded wait returns, with no START sent and both lines let go. */
static void test_sda_stuck_low_is_a_timeout_after_nine_clocks(void)
{
    bus.hold_sda = FOREVER;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", bus.log);
    TEST_ASSERT_EQUAL_UINT(9u, bus.scl_rises);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, bus.m);
}

static void test_scl_stuck_low_is_a_timeout(void)
{
    bus.hold_scl = FOREVER;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(0x50u, NULL, 0u, (uint8_t[1]){ 0u }, 1u));
    TEST_ASSERT_EQUAL_STRING("", bus.log);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, bus.m);
}

/* A device stretching the clock is not a stuck bus: it is waited for. */
static void test_a_stretched_clock_is_waited_for(void)
{
    bus.hold_scl = 10u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("S A0 A 01 A P", bus.log);
}

/* Nothing to write and nothing to read is TFW_OK and no bus activity. */
static void test_an_empty_transfer_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", bus.log);
    TEST_ASSERT_EQUAL_UINT(0u, bus.delays);
}

/* Half a period, rounded up, so the clock never runs faster than asked:
 * 400 kHz is a 2 us half period, 250 kHz. */
static void test_init_rounds_the_half_period_up(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000uL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT16(2u, bus.delay_min);
    TEST_ASSERT_EQUAL_UINT16(2u, bus.delay_max);

    quiet();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(1000uL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, (const uint8_t[]){ 0x01u }, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT16(500u, bus.delay_min);
    TEST_ASSERT_EQUAL_UINT16(500u, bus.delay_max);
}

static void test_init_refuses_a_rate_it_cannot_time(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(0uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(7uL));   /* 71 429 us: past 16 bits */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(8uL));          /* 62 500 us */
}

static void test_init_lets_both_lines_go(void)
{
    tfw_mps2_sb_drive_low(SB_BOTH);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, bus.m);
}

/* SPI and 1-Wire are not on this board; their entry points refuse. */
/* SSPCR0 for each mode and divisor (DDI 0194H, Table 3-2): SCR in [15:8] is
 * 1, 7, 31 or 63 over a prescale of 2, for divisors 4, 16, 64 and 128; SPH
 * [7] is mode bit 0, SPO [6] mode bit 1; FRF 00 and DSS 0111, 8-bit Motorola
 * SPI. The select is released first and the port stopped while it is set. */
static void test_spi_init_sets_a_master_in_the_mode_and_divisor(void)
{
    static const struct { uint8_t mode, clkdiv; const char *writes; } k[] = {
        { 0u, 0u, " MISC=3A5 CR1=0000 CPSR=0002 CR0=0107 CR1=0002" },
        { 1u, 1u, " MISC=3A5 CR1=0000 CPSR=0002 CR0=0787 CR1=0002" },
        { 2u, 2u, " MISC=3A5 CR1=0000 CPSR=0002 CR0=1F47 CR1=0002" },
        { 3u, 3u, " MISC=3A5 CR1=0000 CPSR=0002 CR0=3FC7 CR1=0002" },
    };
    size_t i;

    for (i = 0u; i < sizeof k / sizeof k[0]; i++) {
        ssp.writes[0] = '\0';
        ssp.log[0] = '\0';
        ssp.misc = 0x2A5u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_init(k[i].mode, k[i].clkdiv, 0u));
        TEST_ASSERT_EQUAL_STRING(k[i].writes, ssp.writes);
        TEST_ASSERT_EQUAL_STRING("]", ssp.log);
    }
}

static void test_spi_xfer_selects_shifts_each_byte_and_releases(void)
{
    static const uint8_t ans[] = { 0xEFu, 0x40u, 0x18u };
    const tfw_pinref_t cs = { 1u, 8u };
    const uint8_t wr[3] = { 0x9Fu, 0x00u, 0x00u };
    uint8_t rd[3] = { 0u };

    ssp_ready(0u);
    ssp.ans = ans;
    ssp.ansn = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wr, rd, 3u));
    TEST_ASSERT_EQUAL_STRING("[>9F<EF>00<40>00<18]", ssp.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ans, rd, 3u);
    TEST_ASSERT_EQUAL_HEX32(0x3A5u, ssp.misc);
}

/* w5x00.c and spi_nor.c pass one buffer as both. */
static void test_spi_xfer_reads_into_the_buffer_it_writes(void)
{
    static const uint8_t ans[] = { 0xC2u, 0x20u };
    const tfw_pinref_t cs = { 1u, 8u };
    uint8_t buf[2] = { 0x9Fu, 0x00u };

    ssp_ready(0u);
    ssp.ans = ans;
    ssp.ansn = 2u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, buf, buf, 2u));
    TEST_ASSERT_EQUAL_STRING("[>9F<C2>00<20]", ssp.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ans, buf, 2u);
}

/* The PL022 shifts most significant bit first only; least first is each byte
 * reversed on its way out and back. */
static void test_spi_lsb_first_reverses_each_byte(void)
{
    static const uint8_t ans[] = { 0x80u, 0x03u };
    static const uint8_t want[] = { 0x01u, 0xC0u };
    const tfw_pinref_t cs = { 1u, 8u };
    const uint8_t wr[2] = { 0x01u, 0xC0u };
    uint8_t rd[2] = { 0u };

    ssp_ready(1u);
    ssp.ans = ans;
    ssp.ansn = 2u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wr, rd, 2u));
    TEST_ASSERT_EQUAL_STRING("[>80<80>03<03]", ssp.log);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 2u);
}

static void test_spi_xfer_drains_a_stale_answer_first(void)
{
    static const uint8_t ans[] = { 0x5Au };
    const tfw_pinref_t cs = { 1u, 8u };
    const uint8_t wr = 0x05u;
    uint8_t rd = 0u;

    ssp_ready(0u);
    ssp.rx[0] = 0xAAu;
    ssp.rx[1] = 0xBBu;
    ssp.rxn = 2u;
    ssp.ans = ans;
    ssp.ansn = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, &wr, &rd, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, rd);
    TEST_ASSERT_EQUAL_UINT8(0u, ssp.rxn);
}

/* Port 0 is the LEDs, and MISC bit 9 is shield 1's select: neither is the
 * shield 0 device, so nothing is selected or clocked. */
static void test_spi_xfer_refuses_any_other_select(void)
{
    const tfw_pinref_t led = { 0u, 0u };
    const tfw_pinref_t shield1 = { 1u, 9u };
    const uint8_t wr = 0x9Fu;
    uint8_t rd = 0u;

    ssp_ready(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_spi_xfer(&led, &wr, &rd, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_spi_xfer(&shield1, &wr, &rd, 1u));
    TEST_ASSERT_EQUAL_STRING("", ssp.log);
    TEST_ASSERT_EQUAL_UINT(0u, ssp.dr_writes);
    TEST_ASSERT_EQUAL_HEX32(0x3A5u, ssp.misc);
}

/* A port that never finishes a frame: 2000 polls a microsecond apart, then
 * the select is released and the transfer is a timeout, as on AVR. */
static void test_spi_a_stopped_port_is_a_timeout_and_the_select_released(void)
{
    const tfw_pinref_t cs = { 1u, 8u };
    const uint8_t wr = 0x9Fu;
    uint8_t rd = 0u;

    ssp_ready(0u);
    ssp.stalled = 1u;
    quiet();
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_spi_xfer(&cs, &wr, &rd, 1u));
    TEST_ASSERT_EQUAL_STRING("[]", ssp.log);
    TEST_ASSERT_EQUAL_UINT(2000u, bus.delays);
    TEST_ASSERT_EQUAL_UINT16(1u, bus.delay_max);
    TEST_ASSERT_EQUAL_HEX32(0x3A5u, ssp.misc);
}

static void test_onewire_is_unsupported(void)
{
    const tfw_pinref_t p = { 0 };
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(&p, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(&p, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(&p, &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_write_then_read_is_start_bytes_repeated_start_and_stop);
    RUN_TEST(test_a_write_only_transfer_ends_in_stop);
    RUN_TEST(test_a_read_acks_every_byte_but_the_last);
    RUN_TEST(test_an_unanswered_address_is_nack_and_the_bus_is_stopped);
    RUN_TEST(test_a_refused_data_byte_is_nack_and_the_bus_is_stopped);
    RUN_TEST(test_sda_held_low_is_clocked_free_before_the_start);
    RUN_TEST(test_sda_stuck_low_is_a_timeout_after_nine_clocks);
    RUN_TEST(test_scl_stuck_low_is_a_timeout);
    RUN_TEST(test_a_stretched_clock_is_waited_for);
    RUN_TEST(test_an_empty_transfer_touches_nothing);
    RUN_TEST(test_init_rounds_the_half_period_up);
    RUN_TEST(test_init_refuses_a_rate_it_cannot_time);
    RUN_TEST(test_init_lets_both_lines_go);
    RUN_TEST(test_spi_init_sets_a_master_in_the_mode_and_divisor);
    RUN_TEST(test_spi_xfer_selects_shifts_each_byte_and_releases);
    RUN_TEST(test_spi_xfer_reads_into_the_buffer_it_writes);
    RUN_TEST(test_spi_lsb_first_reverses_each_byte);
    RUN_TEST(test_spi_xfer_drains_a_stale_answer_first);
    RUN_TEST(test_spi_xfer_refuses_any_other_select);
    RUN_TEST(test_spi_a_stopped_port_is_a_timeout_and_the_select_released);
    RUN_TEST(test_onewire_is_unsupported);
    return UNITY_END();
}
