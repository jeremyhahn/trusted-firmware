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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ow_reset_presence);
    return UNITY_END();
}
