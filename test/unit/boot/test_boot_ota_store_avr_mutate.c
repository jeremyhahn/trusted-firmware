/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/avr/boot_ota_store_avr.c on the host (#75, #163): the external
 * store's bus for tfw-boot, register-level, against test/mock/avr_regs's
 * <avr/io.h>, whose every register access is tfw_avr_reg() below. Under the
 * host board's (the 328P's) profile and, as test_boot_ota_store_avr_mutate_2560,
 * the 2560's, whose selects are on three ports.
 *
 * At every access the state the previous write left is checked: no select
 * (the store's, the WIZnet's, the SD socket's, the hardware SS) is ever
 * driven low except the store's inside a command. Each SPI byte is the
 * peripheral's exchange: SPDR written, SPSR polled until SPIF, SPDR read;
 * the byte is taken at the poll and the slave's reply put in its place.
 *
 * The mutants this kills: a select, SS or an SPI pin left out of open or
 * close, |= or &= turned into =, the level and direction written in the
 * wrong order, SPCR's mode wrong or left on, the store selected late or
 * released early or never, a command byte or a read byte skipped or added,
 * the poll for SPIF dropped, the read length narrowed to a byte.
 */
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "avr/io.h"

#define PIN_REG(io)    PIN_REG_(io)
#define PIN_REG_(p, b) ((unsigned)(#p[0] - 'A'))
#define PIN_BIT(io)    PIN_BIT_(io)
#define PIN_BIT_(p, b) ((uint8_t)(1u << (b)))

#define DDR_OF(port) ((port) + (unsigned)TFW_AVR_DDRA)
#define SPI_OUT      ((uint8_t)((1u << TFW_SPI_BIT_MOSI) | (1u << TFW_SPI_BIT_SCK) | (1u << TFW_SPI_BIT_SS)))
#define MAX_XFER     400u
#define GUARD        0xA5u

enum { SEL_STORE, SEL_ETH, SEL_SD, SEL_SS, SEL_COUNT };

static unsigned sel_reg[SEL_COUNT];
static uint8_t  sel_bit[SEL_COUNT];

static uint8_t  regs[TFW_AVR_REGS];
static uint8_t  spsr;
static int      armed;          /* 0 idle, 1 SPDR written, 2 exchanged */
static int      in_cmd;
static uint8_t  sent[MAX_XFER];
static unsigned nsent;
static unsigned violations;
static unsigned misreads;
static unsigned bad_exchanges;

static uint8_t reply(unsigned n)
{
    return (uint8_t)(0x5Au ^ (n * 13u));
}

/* No select driven low but the store's, and that only inside a command. */
static void check(void)
{
    unsigned s;

    for (s = 0u; s < SEL_COUNT; s++) {
        int driven = (regs[DDR_OF(sel_reg[s])] & sel_bit[s]) != 0u;
        int high   = (regs[sel_reg[s]] & sel_bit[s]) != 0u;

        if (driven && !high && !(s == SEL_STORE && in_cmd)) {
            violations++;
        }
    }
}

static int selected(unsigned s)
{
    return (regs[DDR_OF(sel_reg[s])] & sel_bit[s]) != 0u && (regs[sel_reg[s]] & sel_bit[s]) == 0u;
}

static int held_high(unsigned s)
{
    return (regs[DDR_OF(sel_reg[s])] & sel_bit[s]) != 0u && (regs[sel_reg[s]] & sel_bit[s]) != 0u;
}

/* One byte on the bus: only the store selected, the others held high, the
 * peripheral on in mode 0 as master. */
static void exchange(void)
{
    if (!selected(SEL_STORE) || !held_high(SEL_ETH) || !held_high(SEL_SD)
        || regs[TFW_AVR_SPCR] != (uint8_t)((1u << SPE) | (1u << MSTR)) || nsent >= MAX_XFER) {
        bad_exchanges++;
    }
    if (nsent < MAX_XFER) {
        sent[nsent] = regs[TFW_AVR_SPDR];
    }
    regs[TFW_AVR_SPDR] = reply(nsent);
    nsent++;
}

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    check();
    if (id == TFW_AVR_SPDR) {
        if (armed == 0) {
            armed = 1;
        } else if (armed == 2) {
            armed = 0;
        } else {
            /* Read before SPIF: the byte the master wrote, not the slave's. */
            misreads++;
            armed = 0;
        }
        return &regs[TFW_AVR_SPDR];
    }
    if (id == TFW_AVR_SPSR) {
        spsr = 0u;
        if (armed == 1) {
            exchange();
            armed = 2;
            spsr = (uint8_t)(1u << SPIF);
        }
        return &spsr;
    }
    return &regs[id];
}

static void fill_ports(uint8_t pattern)
{
    unsigned s;

    memset(regs, pattern, sizeof regs);
    for (s = 0u; s < SEL_COUNT; s++) {
        regs[sel_reg[s]] &= (uint8_t)~sel_bit[s];
        regs[DDR_OF(sel_reg[s])] &= (uint8_t)~sel_bit[s];
    }
    regs[TFW_AVR_DDRB] &= (uint8_t)~SPI_OUT;
    regs[TFW_AVR_SPCR] = 0u;
}

void setUp(void)
{
    sel_reg[SEL_STORE] = PIN_REG(TFW_OTA_STORE_CS_IO);
    sel_bit[SEL_STORE] = PIN_BIT(TFW_OTA_STORE_CS_IO);
    sel_reg[SEL_ETH]   = PIN_REG(TFW_ETH_CS_IO);
    sel_bit[SEL_ETH]   = PIN_BIT(TFW_ETH_CS_IO);
    sel_reg[SEL_SD]    = PIN_REG(TFW_SD_CS_IO);
    sel_bit[SEL_SD]    = PIN_BIT(TFW_SD_CS_IO);
    sel_reg[SEL_SS]    = TFW_AVR_PORTB;
    sel_bit[SEL_SS]    = (uint8_t)(1u << TFW_SPI_BIT_SS);
    fill_ports(0u);
    armed         = 0;
    in_cmd        = 0;
    nsent         = 0u;
    violations    = 0u;
    misreads      = 0u;
    bad_exchanges = 0u;
}

void tearDown(void)
{
    check();
    TEST_ASSERT_EQUAL_UINT(0u, violations);
    TEST_ASSERT_EQUAL_UINT(0u, misreads);
    TEST_ASSERT_EQUAL_UINT(0u, bad_exchanges);
}

static tfw_status_t cmd(const uint8_t *c, uint8_t clen, uint8_t *in, uint16_t ilen)
{
    tfw_status_t st;

    in_cmd = 1;
    st     = tfw_boot_store_cmd(c, clen, in, ilen);
    check();
    in_cmd = 0;
    return st;
}

/* Open over a pattern with the bus's bits clear: exactly those bits set,
 * every other bit as it was. */
static void open_over(uint8_t pattern)
{
    uint8_t  want[TFW_AVR_REGS];
    unsigned s;

    fill_ports(pattern);
    memcpy(want, regs, sizeof want);
    for (s = 0u; s < SEL_COUNT; s++) {
        want[sel_reg[s]] |= sel_bit[s];
        want[DDR_OF(sel_reg[s])] |= sel_bit[s];
    }
    want[TFW_AVR_DDRB] |= SPI_OUT;
    want[TFW_AVR_SPCR] = (uint8_t)((1u << SPE) | (1u << MSTR));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, regs, TFW_AVR_SPCR + 1u);
    TEST_ASSERT_EQUAL_UINT(0u, nsent);
}

static void test_open_holds_every_select_high_and_starts_the_master(void)
{
    open_over(0x00u);
    open_over(0xFFu);
    open_over(0x3Cu);
}

static void test_close_returns_every_bus_pin_to_reset_and_the_rest_untouched(void)
{
    static const uint8_t patterns[] = { 0x00u, 0xFFu, 0xC3u };
    uint8_t              want[TFW_AVR_REGS];
    unsigned             i;

    for (i = 0u; i < sizeof patterns; i++) {
        fill_ports(patterns[i]);
        memcpy(want, regs, sizeof want);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
        tfw_boot_store_close();
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, regs, TFW_AVR_SPCR + 1u);
    }
}

/* The command out, then the reads, all under one select; the replies to the
 * command's own bytes are dropped, the rest land in order and no further. */
static void test_a_command_writes_then_reads_under_one_select(void)
{
    static const uint8_t c[] = { 0x03u, 0x01u, 0x02u, 0x03u };
    uint8_t              in[1 + 5 + 1];
    unsigned             i;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
    memset(in, GUARD, sizeof in);
    TEST_ASSERT_EQUAL(TFW_OK, cmd(c, (uint8_t)sizeof c, &in[1], 5u));
    TEST_ASSERT_EQUAL_UINT(sizeof c + 5u, nsent);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(c, sent, sizeof c);
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, sent[sizeof c + i]);
        TEST_ASSERT_EQUAL_HEX8(reply((unsigned)sizeof c + i), in[1 + i]);
    }
    TEST_ASSERT_EQUAL_HEX8(GUARD, in[0]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, in[6]);
    TEST_ASSERT_TRUE(held_high(SEL_STORE));
}

/* A command with nothing to read, and one with nothing at all. */
static void test_empty_halves_send_nothing_extra(void)
{
    static const uint8_t c[] = { 0x06u };
    uint8_t              in[1] = { GUARD };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
    TEST_ASSERT_EQUAL(TFW_OK, cmd(c, 1u, in, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, nsent);
    TEST_ASSERT_EQUAL_HEX8(0x06u, sent[0]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, in[0]);
    TEST_ASSERT_TRUE(held_high(SEL_STORE));

    TEST_ASSERT_EQUAL(TFW_OK, cmd(c, 0u, in, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, nsent);
    TEST_ASSERT_EQUAL_HEX8(GUARD, in[0]);
    TEST_ASSERT_TRUE(held_high(SEL_STORE));
}

/* A read longer than a byte's range, as a page of the copy is: all of it. */
static void test_a_read_past_255_bytes_is_read_whole(void)
{
    static const uint8_t c[] = { 0x03u, 0x00u, 0x10u, 0x00u };
    static uint8_t       in[300 + 1];
    unsigned             i;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
    memset(in, GUARD, sizeof in);
    TEST_ASSERT_EQUAL(TFW_OK, cmd(c, (uint8_t)sizeof c, in, 300u));
    TEST_ASSERT_EQUAL_UINT(sizeof c + 300u, nsent);
    for (i = 0u; i < 300u; i++) {
        TEST_ASSERT_EQUAL_HEX8(reply((unsigned)sizeof c + i), in[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(GUARD, in[300]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_holds_every_select_high_and_starts_the_master);
    RUN_TEST(test_close_returns_every_bus_pin_to_reset_and_the_rest_untouched);
    RUN_TEST(test_a_command_writes_then_reads_under_one_select);
    RUN_TEST(test_empty_halves_send_nothing_extra);
    RUN_TEST(test_a_read_past_255_bytes_is_read_whole);
    return UNITY_END();
}
