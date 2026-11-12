/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 1-Wire master (src/hal/cortex-m/mps2/hal_bus.c, tfw_hal_ow_*),
 * linked against a modelled GPIO pin and SysTick in place of hal_gpio.c and
 * hal_tick.c, as test_mps2_bus.c links the I2C master against a modelled
 * SBCon.
 *
 * The pin: the master either drives it low (output) or lets it float (input);
 * a pull-up makes a floating line high unless the model slave holds it low.
 * Time is the model SysTick's, in microseconds, advanced only by
 * tfw_hal_delay_us. Every change of the master's direction is logged with
 * that time, the line's level after it and whether interrupts were masked.
 *
 * The slave sees each low the master releases: one of 480 us or more is a
 * reset, which it answers with a presence pulse 15 us after the release, 60 us
 * long (Maxim AN126, Table 2: tPDH, tPDL). A shorter low is a time slot: a
 * low under 15 us is a 1, longer a 0, which it shifts in least significant
 * bit first; when it has bits to send, a short slot is a read and a 0 bit is
 * the line held low until 30 us after the master's fall.
 */
#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "sbcon.h"
#include "pl022.h"
#include "gpio_line.h"

#define OW_MASK  (1u << TFW_MPS2_OW_BIT)
#define MAX_EV   1024u

typedef struct {
    uint32_t t;        /* the model SysTick, us */
    uint8_t  out;      /* 1: the master drives the line low */
    uint8_t  level;    /* the line after the change */
    uint8_t  masked;   /* interrupts masked at the change */
} ow_ev_t;

static struct {
    uint32_t       now;
    uint8_t        out;
    uint8_t        masked;
    uint32_t       fell;           /* when the master last pulled the line low */
    /* the slave */
    uint8_t        present;
    uint32_t       hold_from;      /* the slave holds the line low in [from, to) */
    uint32_t       hold_to;
    const uint8_t *tx;             /* bytes it answers read slots with */
    uint8_t        txlen;
    unsigned       txbit;
    uint8_t        rx[8];          /* bytes the written slots carried */
    unsigned       rxbit;
    /* what the test reads */
    ow_ev_t        ev[MAX_EV];
    unsigned       nev;
} ow;

static uint8_t level(void)
{
    if (ow.out) {
        return 0u;
    }
    return (uint8_t)!(ow.now >= ow.hold_from && ow.now < ow.hold_to);
}

static void log_change(void)
{
    if (ow.nev < MAX_EV) {
        ow.ev[ow.nev].t = ow.now;
        ow.ev[ow.nev].out = ow.out;
        ow.ev[ow.nev].level = level();
        ow.ev[ow.nev].masked = ow.masked;
        ow.nev++;
    }
}

/* The master let go of a low it began at ow.fell: what did the slave see? */
static void slave_on_release(void)
{
    uint32_t low = ow.now - ow.fell;

    if (low >= 480u) {
        if (ow.present) {
            ow.hold_from = ow.now + 15u;
            ow.hold_to = ow.now + 75u;
        }
        ow.txbit = 0u;
        ow.rxbit = 0u;
        return;
    }
    if (ow.tx && ow.txbit < 8u * ow.txlen) {
        uint8_t bit = (uint8_t)((ow.tx[ow.txbit / 8u] >> (ow.txbit % 8u)) & 1u);

        ow.txbit++;
        if (!bit) {
            ow.hold_from = ow.now;
            ow.hold_to = ow.fell + 30u;
        }
        return;
    }
    if (ow.rxbit < 8u * sizeof ow.rx && low < 15u) {
        ow.rx[ow.rxbit / 8u] |= (uint8_t)(1u << (ow.rxbit % 8u));
    }
    ow.rxbit++;
}

/* ---- gpio_line.h and tfw_hal_delay_us, as hal_bus.c links them */

uint32_t tfw_mps2_gpio_lines(void)
{
    return level() ? OW_MASK : 0u;
}

void tfw_mps2_gpio_drive_low(uint32_t mask)
{
    if ((mask & OW_MASK) && !ow.out) {
        ow.out = 1u;
        ow.fell = ow.now;
        log_change();
    }
}

void tfw_mps2_gpio_release(uint32_t mask)
{
    if ((mask & OW_MASK) && ow.out) {
        ow.out = 0u;
        slave_on_release();
        log_change();
    }
}

uint32_t tfw_mps2_gpio_irq_save(void)
{
    uint32_t was = ow.masked;

    ow.masked = 1u;
    return was;
}

void tfw_mps2_gpio_irq_restore(uint32_t saved)
{
    ow.masked = (uint8_t)saved;
}

void tfw_hal_delay_us(uint16_t us)
{
    ow.now += us;
}

/* ---- the rest of hal_bus.c's links: the SBCon, the PL022 and MISC, unused */

uint32_t tfw_mps2_sb_lines(void)                  { return TFW_SB_SCL | TFW_SB_SDA; }
void     tfw_mps2_sb_release(uint32_t m)          { (void)m; }
void     tfw_mps2_sb_drive_low(uint32_t m)        { (void)m; }
uint32_t tfw_mps2_ssp_read(uint32_t off)          { (void)off; return 0u; }
void     tfw_mps2_ssp_write(uint32_t off, uint32_t v) { (void)off; (void)v; }
uint32_t tfw_mps2_misc_read(void)                 { return 0u; }
void     tfw_mps2_misc_write(uint32_t v)          { (void)v; }

static const tfw_pinref_t k_ow_pin = { TFW_MPS2_PORT_GPIO0, TFW_MPS2_OW_BIT };

void setUp(void)
{
    memset(&ow, 0, sizeof ow);
    ow.now = 1000u;
}
void tearDown(void) {}

/* A reset is the line low for 480 us, then released; the slave's presence
 * pulse is sampled 70 us after the release, and the slot ends 410 us later
 * (the AVR's timings). All of it with interrupts masked, and unmasked after. */
static void test_ow_reset_presence(void)
{
    uint8_t presence = 0xEEu;

    ow.present = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_reset(&k_ow_pin, &presence));
    TEST_ASSERT_EQUAL_UINT8(1u, presence);
    TEST_ASSERT_EQUAL_UINT(2u, ow.nev);
    TEST_ASSERT_EQUAL_UINT8(1u, ow.ev[0].out);
    TEST_ASSERT_EQUAL_UINT8(0u, ow.ev[1].out);
    TEST_ASSERT_EQUAL_UINT32(480u, ow.ev[1].t - ow.ev[0].t);
    TEST_ASSERT_EQUAL_UINT8(1u, ow.ev[0].masked);
    TEST_ASSERT_EQUAL_UINT8(1u, ow.ev[1].masked);
    TEST_ASSERT_EQUAL_UINT32(480u + 70u + 410u, ow.now - ow.ev[0].t);
    TEST_ASSERT_EQUAL_UINT8(0u, ow.masked);
    TEST_ASSERT_EQUAL_UINT8(1u, level());
}

/* Nobody on the line: it floats high through the sample, presence 0, and the
 * slot keeps its full length. */
static void test_ow_reset_no_presence(void)
{
    uint8_t presence = 0xEEu;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_reset(&k_ow_pin, &presence));
    TEST_ASSERT_EQUAL_UINT8(0u, presence);
    TEST_ASSERT_EQUAL_UINT(2u, ow.nev);
    TEST_ASSERT_EQUAL_UINT32(480u + 70u + 410u, ow.now - ow.ev[0].t);
    TEST_ASSERT_EQUAL_UINT8(0u, ow.masked);
}

/* 0xA5 goes out least significant bit first: 1 0 1 0 0 1 0 1. A 1 is low for
 * 6 us then 64 us released, a 0 low for 60 us then 10 us released: 70 us a
 * slot. Each slot is masked; between slots interrupts are taken. */
static void test_ow_write_byte_slots(void)
{
    static const uint8_t bits[8] = { 1u, 0u, 1u, 0u, 0u, 1u, 0u, 1u };
    const uint8_t b = 0xA5u;
    unsigned i;

    ow.present = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_write(&k_ow_pin, &b, 1u));
    TEST_ASSERT_EQUAL_UINT(16u, ow.nev);
    for (i = 0u; i < 8u; i++) {
        const ow_ev_t *fall = &ow.ev[2u * i];
        const ow_ev_t *rise = &ow.ev[2u * i + 1u];
        uint32_t next = (i + 1u < 8u) ? ow.ev[2u * i + 2u].t : ow.now;

        TEST_ASSERT_EQUAL_UINT8(1u, fall->out);
        TEST_ASSERT_EQUAL_UINT8(0u, rise->out);
        TEST_ASSERT_EQUAL_UINT8(1u, rise->level);
        TEST_ASSERT_EQUAL_UINT32(bits[i] ? 6u : 60u, rise->t - fall->t);
        TEST_ASSERT_EQUAL_UINT32(70u, next - fall->t);
        TEST_ASSERT_EQUAL_UINT8(1u, fall->masked);
        TEST_ASSERT_EQUAL_UINT8(1u, rise->masked);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, ow.rx[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, ow.masked);
}

/* The slave answers 0x3C then 0x81: every read slot is a 6 us low, the line
 * sampled 9 us after the release, inside the slave's 30 us hold for a 0. */
static void test_ow_read_bytes_from_slave(void)
{
    static const uint8_t ans[2] = { 0x3Cu, 0x81u };
    uint8_t rd[2] = { 0u, 0u };
    unsigned i;

    ow.tx = ans;
    ow.txlen = 2u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_read(&k_ow_pin, rd, 2u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ans, rd, 2u);
    TEST_ASSERT_EQUAL_UINT(32u, ow.nev);
    for (i = 0u; i < 16u; i++) {
        TEST_ASSERT_EQUAL_UINT32(6u, ow.ev[2u * i + 1u].t - ow.ev[2u * i].t);
        TEST_ASSERT_EQUAL_UINT8(1u, ow.ev[2u * i].masked);
    }
    TEST_ASSERT_EQUAL_UINT32(16u * 70u, ow.now - ow.ev[0].t);
    TEST_ASSERT_EQUAL_UINT8(0u, ow.masked);
}

/* Any pin but GPIO block 0's 1-Wire bit is TFW_ERR_RANGE, as the AVR refuses
 * a pin its table does not allow, and the line is never touched. */
static void test_ow_out_of_table_pin_is_range(void)
{
    static const tfw_pinref_t k_bad[] = {
        { 0u, 0u },                                    /* LED0 */
        { 1u, 8u },                                    /* the SPI select */
        { TFW_MPS2_PORT_GPIO0, TFW_MPS2_OW_BIT + 1u },
        { TFW_MPS2_PORT_GPIO0 + 1u, TFW_MPS2_OW_BIT },
    };
    uint8_t b = 0x5Au;
    size_t i;

    for (i = 0u; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_ow_reset(&k_bad[i], &b));
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_ow_write(&k_bad[i], &b, 1u));
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_ow_read(&k_bad[i], &b, 1u));
    }
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    TEST_ASSERT_EQUAL_UINT(0u, ow.nev);
    TEST_ASSERT_EQUAL_UINT32(1000u, ow.now);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ow_reset_presence);
    RUN_TEST(test_ow_reset_no_presence);
    RUN_TEST(test_ow_write_byte_slots);
    RUN_TEST(test_ow_read_bytes_from_slave);
    RUN_TEST(test_ow_out_of_table_pin_is_range);
    return UNITY_END();
}
