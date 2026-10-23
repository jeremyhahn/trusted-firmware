/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_bus.c, register-level (#163): the TWI, the SPI master and
 * the bit-banged 1-Wire, each against a model of the other end.
 *
 * Every register access is a call into this suite before the access lands,
 * so a model acts on what the unit wrote the next time it is called:
 *
 *   TWI   a TWCR write with TWINT is a command, run at the next access: a
 *         START, the address (TWDR, acked when it names the model's device),
 *         a data byte out, a byte in (ACK or NACK as TWEA says), a STOP. Each
 *         is logged as (TWCR << 8) | byte. TWCR then reads back with TWIE set
 *         -- a bit the unit never writes, which is how the next command is
 *         told from a stale one -- and TWINT once the model's polls are up.
 *         TWSR carries the status with a prescaler bit set, so an unmasked
 *         status compare fails.
 *   SPI   SPDR's accesses alternate write and read; the first SPSR poll after
 *         a write clocks the byte (once the model's polls are up), checking
 *         the caller's select is asserted, and loads the reply.
 *   1-W   the line's driver is sampled at each access and each _delay_us()
 *         (util/delay.h's stand-in, which advances the model clock), giving
 *         every low pulse the unit drove, start and length. A device holds
 *         the line low for a presence pulse or a 0 bit, and PINx reads the
 *         line at that instant, every other bit the opposite level.
 */
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "tfw/config.h"
#include "avr/io.h"

#define MEM 0x100u

static uint8_t  mem[MEM], before[MEM];
static uint8_t  regs[TFW_AVR_REGS], regs_before[TFW_AVR_REGS];
static uint32_t now;

/* ------------------------------------------------------------- TWI model */

#define TW_DONE 0x81u   /* TWINT | TWIE */
#define TW_BUSY 0x01u   /* TWIE */
#define TW_NONE UINT_MAX

static uint16_t tw_log[64];
static unsigned tw_n;
static uint8_t  tw_dev;
static uint8_t  tw_rx[8];
static unsigned tw_rx_i, tw_wr_i, tw_nack_data, tw_stall, tw_late, tw_left, tw_polls;
static enum { TW_IDLE, TW_ADDR, TW_WRITE, TW_READ } tw_state;

static void tw_command(uint8_t v)
{
    uint8_t data = 0u, st;

    if (v & (1u << TWSTA)) {
        st       = tw_state == TW_IDLE ? 0x08u : 0x10u;
        tw_state = TW_ADDR;
    } else if (v & (1u << TWSTO)) {
        st       = 0xF8u;
        tw_state = TW_IDLE;
    } else if (tw_state == TW_ADDR) {
        uint8_t ack;

        data     = regs[TFW_AVR_TWDR];
        ack      = (uint8_t)((data >> 1) == tw_dev);
        st       = (data & 1u) ? (ack ? 0x40u : 0x48u) : (ack ? 0x18u : 0x20u);
        tw_state = (data & 1u) ? TW_READ : TW_WRITE;
        tw_wr_i  = 0u;
    } else if (tw_state == TW_WRITE) {
        data = regs[TFW_AVR_TWDR];
        st   = tw_wr_i++ == tw_nack_data ? 0x30u : 0x28u;
    } else if (tw_state == TW_READ) {
        data               = tw_rx[tw_rx_i++];
        regs[TFW_AVR_TWDR] = data;
        st                 = (v & (1u << TWEA)) ? 0x50u : 0x58u;
    } else {
        TEST_FAIL_MESSAGE("a TWI command with no START");
        return;
    }
    regs[TFW_AVR_TWSR] = (uint8_t)(st | 0x02u);
    TEST_ASSERT_TRUE(tw_n < 64u);
    tw_log[tw_n++] = (uint16_t)((v << 8) | data);
    tw_left        = (tw_n - 1u == tw_stall) ? TW_NONE : tw_late;
    regs[TFW_AVR_TWCR] = tw_left ? TW_BUSY : TW_DONE;
}

/* Run the command the unit wrote last, if it has not run: on every access,
 * and by a test after the call returns, for the last write. */
static void tw_settle(void)
{
    uint8_t c = regs[TFW_AVR_TWCR];

    if (c != TW_DONE && c != TW_BUSY && (c & (1u << TWINT))) {
        tw_command(c);
    }
}

/* ------------------------------------------------------------- SPI model */

static uint8_t  spi_tx[16], spi_reply[16];
static unsigned spi_n, spi_stall, spi_late, spi_left, spi_polls, spi_cs_bad;
static int      spi_phase, spi_started, spi_clocked;
static uint8_t  spdr_out, spdr_in, spsr;
static const tfw_pinref_t *spi_cs;

static volatile uint8_t *spi_spdr(void)
{
    if (spi_phase == 0) {
        spi_phase   = 1;
        spi_started = 0;
        spi_clocked = 0;
        return &spdr_out;
    }
    TEST_ASSERT_TRUE_MESSAGE(spi_clocked, "SPDR read before the byte was clocked");
    spi_phase = 0;
    return &spdr_in;
}

static volatile uint8_t *spi_spsr(void)
{
    spsr = 0x7Fu;                                   /* every bit but SPIF */
    if (spi_phase == 1 && !spi_clocked) {
        if (!spi_started) {
            spi_started = 1;
            spi_left    = spi_n == spi_stall ? TW_NONE : spi_late;
        }
        if (spi_left != 0u) {
            spi_left--;
            spi_polls++;
            return &spsr;
        }
        if (!(mem[spi_cs->port + 1u] & (1u << spi_cs->bit)) ||
            (mem[spi_cs->port + 2u] & (1u << spi_cs->bit))) {
            spi_cs_bad++;
        }
        TEST_ASSERT_TRUE(spi_n < 16u);
        spi_tx[spi_n] = spdr_out;
        spdr_in       = spi_reply[spi_n++];
        spi_clocked   = 1;
    }
    if (spi_clocked) {
        spsr = 0x81u;                               /* SPIF | SPI2X */
    }
    return &spsr;
}

/* ------------------------------------------------------- 1-Wire model */

static const tfw_pinref_t ow = { 0x26u, 2u };     /* PINC, PC2 */
#define OW_DDR  0x27u
#define OW_PORT 0x28u

static uint32_t ow_start[64], ow_len[64], ow_reads[64];
static unsigned ow_n, ow_nreads;
static int      ow_low_now, ow_present, ow_reading;
static uint32_t ow_since;
static uint8_t  ow_bits[32];
static uint8_t  pin_byte;

static void ow_sample(void)
{
    int low = (mem[OW_DDR] & (1u << ow.bit)) && !(mem[OW_PORT] & (1u << ow.bit));

    if (low && !ow_low_now) {
        ow_since = now;
    } else if (!low && ow_low_now) {
        TEST_ASSERT_TRUE(ow_n < 64u);
        ow_start[ow_n] = ow_since;
        ow_len[ow_n++] = now - ow_since;
    }
    ow_low_now = low;
}

/* A device: a presence pulse 30-150 us after the reset's release, and in a
 * read slot a 0 bit held low for 30 us from the slot's falling edge. */
static int ow_device_low(void)
{
    if (ow_present && now >= 480u + 30u && now < 480u + 150u) {
        return 1;
    }
    if (ow_reading) {
        unsigned slot = ow_low_now ? ow_n : ow_n - 1u;
        uint32_t s    = ow_low_now ? ow_since : ow_start[ow_n - 1u];

        if (ow_n + (unsigned)ow_low_now > 0u && slot < 32u && ow_bits[slot] == 0u &&
            now < s + 30u) {
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------- register access */

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    tw_settle();
    switch (id) {
    case TFW_AVR_TWCR:
        if (regs[TFW_AVR_TWCR] == TW_BUSY) {
            tw_polls++;
            if (tw_left == 0u) {
                regs[TFW_AVR_TWCR] = TW_DONE;
            } else {
                tw_left--;
            }
        }
        return &regs[id];
    case TFW_AVR_TWBR:
    case TFW_AVR_TWSR:
    case TFW_AVR_TWDR:
    case TFW_AVR_SPCR:
    case TFW_AVR_PORTB:
        return &regs[id];
    case TFW_AVR_DDRB:
        TEST_ASSERT_TRUE_MESSAGE(regs[TFW_AVR_PORTB] & (1u << TFW_SPI_BIT_SS),
                                 "SS made an output before it was latched high");
        return &regs[id];
    case TFW_AVR_SPDR:
        return spi_spdr();
    case TFW_AVR_SPSR:
        return spi_spsr();
    default:
        TEST_FAIL_MESSAGE("hal_bus.c touched a register outside its buses");
        return 0;
    }
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    TEST_ASSERT_TRUE(addr < MEM);
    ow_sample();
    if (addr == ow.port) {
        int level = !(ow_low_now || ow_device_low());

        TEST_ASSERT_TRUE(ow_nreads < 64u);
        ow_reads[ow_nreads++] = now;
        pin_byte = (uint8_t)(level ? (1u << ow.bit) : ~(1u << ow.bit));
        return &pin_byte;
    }
    return &mem[addr];
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_bus.c made a word access");
    return 0;
}

void tfw_avr_delay_us(double us)
{
    ow_sample();
    now += (uint32_t)us;
}

/* --------------------------------------------------------------- set-up */

static void fill(void)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        mem[i] = (uint8_t)(i * 3u + 7u);
    }
    for (i = 0u; i < TFW_AVR_REGS; i++) {
        regs[i] = (uint8_t)(0x5Au ^ i);
    }
    regs[TFW_AVR_TWCR] = 0x00u;
    memcpy(before, mem, sizeof mem);
    memcpy(regs_before, regs, sizeof regs);
    now = 0u;

    memset(tw_log, 0, sizeof tw_log);
    tw_n = tw_rx_i = tw_wr_i = tw_polls = tw_left = tw_late = 0u;
    tw_nack_data = tw_stall = TW_NONE;
    tw_state     = TW_IDLE;
    tw_dev       = 0x50u;

    memset(spi_tx, 0, sizeof spi_tx);
    spi_n = spi_polls = spi_cs_bad = spi_late = spi_left = 0u;
    spi_stall = TW_NONE;
    spi_phase = spi_started = spi_clocked = 0;

    ow_n = ow_nreads = 0u;
    ow_low_now = ow_present = ow_reading = 0;
}

static void only_changed(unsigned a, unsigned b)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        if (i != a && i != b) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(before[i], mem[i], "a stray write");
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------- I2C */

static void test_i2c_init_divisor_is_the_ceiling(void)
{
    static const struct { uint32_t hz; uint8_t twbr; } ok[] = {
        { 100000u, 72u }, { 400000u, 12u },
        { 350000u, 15u },        /* 14.86 rounds up: 14 would be 363 636 Hz */
        { 999999u, 1u }, { 1000000u, 0u }, { 1050000u, 0u }, { 2000000u, 0u },
        { 30419u, 255u },        /* the slowest an 8-bit TWBR reaches */
    };
    unsigned i;

    for (i = 0u; i < sizeof ok / sizeof ok[0]; i++) {
        fill();
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_init(ok[i].hz));
        TEST_ASSERT_EQUAL_UINT8(ok[i].twbr, regs[TFW_AVR_TWBR]);
        TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TWSR]);
        TEST_ASSERT_EQUAL_HEX8(0x04u, regs[TFW_AVR_TWCR]);     /* TWEN alone */
        TEST_ASSERT_EQUAL_UINT(0u, tw_n);
    }
}

static void test_i2c_init_out_of_reach_touches_nothing(void)
{
    static const uint32_t bad[] = { 0u, 1000u, 30418u };   /* 30418: TWBR 256 */
    unsigned              i;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        fill();
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_i2c_init(bad[i]));
        TEST_ASSERT_EQUAL_MEMORY(regs_before, regs, sizeof regs);
    }
}

static void expect_log(const uint16_t *want, unsigned n)
{
    TEST_ASSERT_EQUAL_UINT(n, tw_n);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(want, tw_log, n);
}

static void test_i2c_write(void)
{
    static const uint8_t  wr[] = { 0x12u, 0x34u };
    static const uint16_t want[] = { 0xA400u, 0x84A0u, 0x8412u, 0x8434u, 0x9400u };
    uint8_t               rd = 0xEEu;

    fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_xfer(0x50u, wr, 2u, &rd, 0u));
    tw_settle();
    expect_log(want, 5u);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd);
}

static void test_i2c_read_acks_all_but_last(void)
{
    static const uint16_t want[] = { 0xA400u, 0x84A1u, 0xC411u, 0xC422u, 0x8433u, 0x9400u };
    uint8_t               rd[4] = { 0xEEu, 0xEEu, 0xEEu, 0xEEu };

    fill();
    tw_rx[0] = 0x11u; tw_rx[1] = 0x22u; tw_rx[2] = 0x33u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_xfer(0x50u, 0, 0u, rd, 3u));
    tw_settle();
    expect_log(want, 6u);
    TEST_ASSERT_EQUAL_HEX8(0x11u, rd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, rd[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33u, rd[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[3]);
}

static void test_i2c_write_then_read_is_repeated_start(void)
{
    static const uint8_t  wr[] = { 0x07u };
    static const uint16_t want[] = { 0xA400u, 0x84A0u, 0x8407u,
                                     0xA400u, 0x84A1u, 0x845Au, 0x9400u };
    uint8_t               rd = 0u;

    fill();
    tw_rx[0] = 0x5Au;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_xfer(0x50u, wr, 1u, &rd, 1u));
    tw_settle();
    expect_log(want, 7u);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, rd);
}

static void test_i2c_nothing_to_do_is_a_stop(void)
{
    static const uint16_t want[] = { 0x9400u };

    fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_xfer(0x50u, 0, 0u, 0, 0u));
    tw_settle();
    expect_log(want, 1u);
}

static void test_i2c_address_nack(void)
{
    static const uint8_t  wr[] = { 0x07u };
    static const uint16_t w_want[] = { 0xA400u, 0x84A2u, 0x9400u };
    static const uint16_t r_want[] = { 0xA400u, 0x84A3u, 0x9400u };
    uint8_t               rd = 0xEEu;

    /* Nobody at 0x51: the write's address is refused, and STOP sent. */
    fill();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x51u, wr, 1u, &rd, 1u));
    tw_settle();
    expect_log(w_want, 3u);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd);

    /* A read alone, refused the same way. */
    fill();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x51u, 0, 0u, &rd, 1u));
    tw_settle();
    expect_log(r_want, 3u);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd);
}

static void test_i2c_data_nack_stops_writing(void)
{
    static const uint8_t  wr[] = { 0x12u, 0x34u, 0x56u };
    static const uint16_t want[] = { 0xA400u, 0x84A0u, 0x8412u, 0x8434u };
    unsigned              i;

    fill();
    tw_nack_data = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, wr, 3u, 0, 0u));
    tw_settle();
    TEST_ASSERT_TRUE(tw_n >= 4u);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(want, tw_log, 4u);
    for (i = 4u; i < tw_n; i++) {
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x9400u, tw_log[i], "wrote on after a NACK");
    }
}

static void test_i2c_stalled_bus_times_out_at_the_bound(void)
{
    static const uint8_t wr[] = { 0x12u };
    unsigned             at;

    /* A write and a two-byte read is seven waits: stall each in turn. */
    for (at = 0u; at < 7u; at++) {
        uint8_t rd[2] = { 0xEEu, 0xEEu };

        fill();
        tw_rx[0] = 0x11u; tw_rx[1] = 0x22u;
        tw_stall = at;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(0x50u, wr, 1u, rd, 2u));
        TEST_ASSERT_EQUAL_UINT(at + 1u, tw_n);       /* gave up on the stalled one */
        TEST_ASSERT_EQUAL_UINT(60000u, tw_polls);
        TEST_ASSERT_EQUAL_HEX8(at > 5u ? 0x11u : 0xEEu, rd[0]);
        TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[1]);
    }
}

static void test_i2c_last_poll_still_counts(void)
{
    static const uint8_t wr[] = { 0x12u };

    fill();
    tw_late = 59999u;                     /* TWINT on the 60 000th poll */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_i2c_xfer(0x50u, wr, 1u, 0, 0u));
    TEST_ASSERT_EQUAL_UINT(3u * 60000u, tw_polls);    /* the busy reads and the one with TWINT */

    fill();
    tw_late = 60000u;                     /* one poll too late */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(0x50u, wr, 1u, 0, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, tw_n);
}

/* ---------------------------------------------------------------- SPI */

static void test_spi_init(void)
{
    static const struct { uint8_t mode, div, lsb, spcr; } c[] = {
        { 0u, 0u, 0u, 0x50u }, { 1u, 0u, 0u, 0x54u }, { 2u, 0u, 0u, 0x58u },
        { 3u, 0u, 0u, 0x5Cu }, { 0u, 1u, 0u, 0x51u }, { 0u, 2u, 0u, 0x52u },
        { 0u, 3u, 0u, 0x53u }, { 0u, 0u, 1u, 0x70u }, { 0u, 0u, 2u, 0x70u },
        { 3u, 3u, 1u, 0x7Fu },
        { 0xFCu, 0xFEu, 0u, 0x52u },      /* only the low two bits of each */
        { 0xFDu, 0xFDu, 0u, 0x55u },
    };
    unsigned i;

    for (i = 0u; i < sizeof c / sizeof c[0]; i++) {
        fill();
        regs[TFW_AVR_PORTB] = 0x81u;
        regs[TFW_AVR_DDRB]  = 0x40u;
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_spi_init(c[i].mode, c[i].div, c[i].lsb));
        TEST_ASSERT_EQUAL_HEX8(c[i].spcr, regs[TFW_AVR_SPCR]);
        TEST_ASSERT_EQUAL_HEX8(0x85u, regs[TFW_AVR_PORTB]);          /* SS (PB2) high */
        TEST_ASSERT_EQUAL_HEX8(0x6Cu, regs[TFW_AVR_DDRB]);           /* PB2, PB3, PB5 */
        only_changed(MEM, MEM);
    }
}

/* PD4 as the select: PIND 0x29, DDRD 0x2A, PORTD 0x2B. */
static const tfw_pinref_t cs = { 0x29u, 4u };

static void spi_fill(void)
{
    fill();
    spi_cs = &cs;
    mem[0x2Au] = before[0x2Au] = 0x21u;      /* PD4 an input */
    mem[0x2Bu] = before[0x2Bu] = 0x5Bu;      /* PD4 latched high */
    memcpy(regs_before, regs, sizeof regs);
}

static void test_spi_xfer_full_duplex(void)
{
    static const uint8_t wr[3] = { 0x9Fu, 0x00u, 0xA5u };
    uint8_t              rd[4] = { 0xEEu, 0xEEu, 0xEEu, 0xEEu };

    spi_fill();
    spi_reply[0] = 0xC2u; spi_reply[1] = 0x20u; spi_reply[2] = 0x17u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_spi_xfer(&cs, wr, rd, 3u));
    TEST_ASSERT_EQUAL_UINT(3u, spi_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, spi_tx, 3u);
    TEST_ASSERT_EQUAL_HEX8(0xC2u, rd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, rd[1]);
    TEST_ASSERT_EQUAL_HEX8(0x17u, rd[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[3]);
    TEST_ASSERT_EQUAL_UINT(0u, spi_cs_bad);
    TEST_ASSERT_EQUAL_HEX8(0x31u, mem[0x2Au]);           /* driven, others kept */
    TEST_ASSERT_EQUAL_HEX8(0x5Bu, mem[0x2Bu]);           /* released */
    TEST_ASSERT_EQUAL_INT(0, spi_phase);
    only_changed(0x2Au, 0x2Bu);
}

static void test_spi_xfer_empty_still_pulses_select(void)
{
    uint8_t rd = 0xEEu;

    spi_fill();
    mem[0x2Bu] = before[0x2Bu] = 0x4Bu;      /* PD4 latched low before */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_spi_xfer(&cs, &rd, &rd, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, spi_n);
    TEST_ASSERT_EQUAL_HEX8(0x31u, mem[0x2Au]);
    TEST_ASSERT_EQUAL_HEX8(0x5Bu, mem[0x2Bu]);           /* ends deselected */
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd);
}

static void test_spi_xfer_stall_releases_select(void)
{
    static const uint8_t wr[3] = { 1u, 2u, 3u };
    uint8_t              rd[3] = { 0xEEu, 0xEEu, 0xEEu };

    spi_fill();
    spi_reply[0] = 0x44u;
    spi_stall = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_spi_xfer(&cs, wr, rd, 3u));
    TEST_ASSERT_EQUAL_UINT(60000u, spi_polls);
    TEST_ASSERT_EQUAL_UINT(1u, spi_n);
    TEST_ASSERT_EQUAL_HEX8(0x44u, rd[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rd[1]);
    TEST_ASSERT_EQUAL_HEX8(0x5Bu, mem[0x2Bu]);           /* released on failure too */
    only_changed(0x2Au, 0x2Bu);
}

static void test_spi_last_poll_still_counts(void)
{
    static const uint8_t wr[2] = { 1u, 2u };
    uint8_t              rd[2];

    spi_fill();
    spi_late = 59999u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_spi_xfer(&cs, wr, rd, 2u));
    TEST_ASSERT_EQUAL_UINT(2u * 59999u, spi_polls);

    spi_fill();
    spi_late = 60000u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_spi_xfer(&cs, wr, rd, 2u));
    TEST_ASSERT_EQUAL_UINT(0u, spi_n);
}

/* ------------------------------------------------------------- 1-Wire */

static void ow_fill(void)
{
    fill();
    mem[OW_DDR]  = before[OW_DDR]  = 0x91u;   /* PC2 an input */
    mem[OW_PORT] = before[OW_PORT] = 0x6Cu;   /* PC2's latch high */
}

static void test_ow_reset_presence(void)
{
    uint8_t present;

    for (present = 0u; present < 2u; present++) {
        uint8_t got = 0xEEu;

        ow_fill();
        ow_present = present;
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_ow_reset(&ow, &got));
        ow_sample();
        TEST_ASSERT_EQUAL_UINT8(present, got);
        TEST_ASSERT_EQUAL_UINT(1u, ow_n);
        TEST_ASSERT_EQUAL_UINT32(0u, ow_start[0]);
        TEST_ASSERT_EQUAL_UINT32(480u, ow_len[0]);
        TEST_ASSERT_EQUAL_UINT(1u, ow_nreads);
        TEST_ASSERT_EQUAL_UINT32(550u, ow_reads[0]);
        TEST_ASSERT_EQUAL_UINT32(960u, now);
        TEST_ASSERT_EQUAL_HEX8(0x91u, mem[OW_DDR]);     /* released */
        TEST_ASSERT_EQUAL_HEX8(0x68u, mem[OW_PORT]);    /* latch low, others kept */
        only_changed(OW_DDR, OW_PORT);
    }
}

static void test_ow_write_lsb_first_slots(void)
{
    static const uint8_t buf[2] = { 0xA5u, 0x01u };
    unsigned             s;

    ow_fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_ow_write(&ow, buf, 2u));
    ow_sample();
    TEST_ASSERT_EQUAL_UINT(16u, ow_n);
    for (s = 0u; s < 16u; s++) {
        uint8_t bit = (uint8_t)((buf[s / 8u] >> (s % 8u)) & 1u);

        TEST_ASSERT_EQUAL_UINT32(70u * s, ow_start[s]);
        TEST_ASSERT_EQUAL_UINT32(bit ? 6u : 60u, ow_len[s]);
    }
    TEST_ASSERT_EQUAL_UINT32(16u * 70u, now);
    TEST_ASSERT_EQUAL_UINT(0u, ow_nreads);
    TEST_ASSERT_EQUAL_HEX8(0x91u, mem[OW_DDR]);
    TEST_ASSERT_EQUAL_HEX8(0x68u, mem[OW_PORT]);
    only_changed(OW_DDR, OW_PORT);
}

static void test_ow_read_samples_each_slot(void)
{
    static const uint8_t want[2] = { 0x35u, 0xC6u };  /* neither a palindrome */
    uint8_t              buf[3]  = { 0xEEu, 0xEEu, 0xEEu };
    unsigned             s;

    ow_fill();
    ow_reading = 1;
    for (s = 0u; s < 16u; s++) {
        ow_bits[s] = (uint8_t)((want[s / 8u] >> (s % 8u)) & 1u);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_ow_read(&ow, buf, 2u));
    ow_sample();
    TEST_ASSERT_EQUAL_HEX8(0x35u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC6u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[2]);
    TEST_ASSERT_EQUAL_UINT(16u, ow_n);
    TEST_ASSERT_EQUAL_UINT(16u, ow_nreads);
    for (s = 0u; s < 16u; s++) {
        TEST_ASSERT_EQUAL_UINT32(70u * s, ow_start[s]);
        TEST_ASSERT_EQUAL_UINT32(6u, ow_len[s]);
        TEST_ASSERT_EQUAL_UINT32(70u * s + 15u, ow_reads[s]);
    }
    TEST_ASSERT_EQUAL_UINT32(16u * 70u, now);
    TEST_ASSERT_EQUAL_HEX8(0x91u, mem[OW_DDR]);
    only_changed(OW_DDR, OW_PORT);
}

static void test_ow_empty_buffers_do_nothing(void)
{
    uint8_t buf = 0xEEu;

    ow_fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_ow_write(&ow, &buf, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_ow_read(&ow, &buf, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, now);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf);
    only_changed(MEM, MEM);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_i2c_init_divisor_is_the_ceiling);
    RUN_TEST(test_i2c_init_out_of_reach_touches_nothing);
    RUN_TEST(test_i2c_write);
    RUN_TEST(test_i2c_read_acks_all_but_last);
    RUN_TEST(test_i2c_write_then_read_is_repeated_start);
    RUN_TEST(test_i2c_nothing_to_do_is_a_stop);
    RUN_TEST(test_i2c_address_nack);
    RUN_TEST(test_i2c_data_nack_stops_writing);
    RUN_TEST(test_i2c_stalled_bus_times_out_at_the_bound);
    RUN_TEST(test_i2c_last_poll_still_counts);
    RUN_TEST(test_spi_init);
    RUN_TEST(test_spi_xfer_full_duplex);
    RUN_TEST(test_spi_xfer_empty_still_pulses_select);
    RUN_TEST(test_spi_xfer_stall_releases_select);
    RUN_TEST(test_spi_last_poll_still_counts);
    RUN_TEST(test_ow_reset_presence);
    RUN_TEST(test_ow_write_lsb_first_slots);
    RUN_TEST(test_ow_read_samples_each_slot);
    RUN_TEST(test_ow_empty_buffers_do_nothing);
    return UNITY_END();
}
