/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 tone (src/hal/cortex-m/mps2/hal_tone.c) against a model: the
 * CMSDK APB timer TIMER0 (CTRL, VALUE, RELOAD, INTSTATUS/INTCLEAR), the NVIC's
 * set/clear-enable words and the FPGA I/O block's LED word, the tone's pin.
 * test/model/cortex-m/mps2_tone_model.h, forced in by mk/host.mk, points the
 * driver's register accesses at the model, so it compiles unchanged. Built for mps2-an505 (20 MHz, TIMER0 IRQ 3).
 */
#include <stdint.h>

static uint32_t m_timer[4];   /* CTRL, VALUE, RELOAD, INTSTATUS/INTCLEAR */
static uint32_t m_led;        /* FPGAIO LED */
static uint32_t m_iser, m_icer, m_icpr;
static uint32_t m_sink;       /* any other address */
static uint32_t m_primask;    /* the modelled PRIMASK */
static unsigned m_led_at;     /* fire a tone edge at this LED-word access */
static unsigned m_led_seen;   /* LED-word accesses so far */
static uint8_t m_edge_held;   /* an edge raised while masked */

#include "../../src/hal/cortex-m/mps2/hal_tone.c"
#include "../../src/hal/cortex-m/mps2/hal_gpio.c"

#include "unity.h"

volatile uint32_t *tfw_mps2_tone_model_reg(uintptr_t a)
{
    if (a >= TFW_MPS2_TIMER0_BASE && a < TFW_MPS2_TIMER0_BASE + 16u)
        return &m_timer[(a - TFW_MPS2_TIMER0_BASE) / 4u];
    if (a == (uintptr_t)TFW_MPS2_FPGAIO_BASE + 0x00u) {
        /* The armed access takes TIMER0's interrupt just before it: at once
         * when unmasked, at the unmasking restore otherwise. */
        if (m_led_at != 0u && ++m_led_seen == m_led_at) {
            m_led_at = 0u;
            if (m_primask)
                m_edge_held = 1u;
            else
                mps2_tone_model_edge();
        }
        return &m_led;
    }
    if (a == 0xE000E100u) return &m_iser;
    if (a == 0xE000E180u) return &m_icer;
    if (a == 0xE000E280u) return &m_icpr;
    return &m_sink;
}

#define M_CTRL      m_timer[0]
#define M_VALUE     m_timer[1]
#define M_RELOAD    m_timer[2]
#define M_INTSTATUS m_timer[3]

static const tfw_pinref_t PIN = TFW_PB(0u, 1u);   /* LED1, the row's pin */

void setUp(void)
{
    for (unsigned i = 0; i < 4u; i++)
        m_timer[i] = 0;
    m_led = 0;
    m_iser = m_icer = m_icpr = m_sink = 0;
    m_primask = 0;
    m_led_at = m_led_seen = 0;
    m_edge_held = 0;
}

uint32_t mps2_tone_model_irq_save(void)
{
    uint32_t was = m_primask;

    m_primask = 1u;
    return was;
}

void mps2_tone_model_irq_restore(uint32_t primask)
{
    m_primask = primask;
    if (!m_primask && m_edge_held) {
        m_edge_held = 0;
        mps2_tone_model_edge();
    }
}

void tearDown(void)
{
    (void)tfw_hal_tone_stop();
}

/* 1 kHz at 20 MHz: a half period of 10000 cycles, a reload of 9999. */
static void test_tone_1khz_reload(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
    TEST_ASSERT_EQUAL_UINT32(9999u, M_RELOAD);
    TEST_ASSERT_EQUAL_UINT32(9999u, M_VALUE);
    TEST_ASSERT_EQUAL_UINT32(TFW_MPS2_TIMER_CTRL_EN | TFW_MPS2_TIMER_CTRL_INTEN, M_CTRL);
}

/* The reload arithmetic for several frequencies, and its bound. */
static void test_tone_reload_table(void)
{
    TEST_ASSERT_EQUAL_UINT32(9999u,    tfw_mps2_tone_reload(20000000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(22726u,   tfw_mps2_tone_reload(20000000u, 440u));
    TEST_ASSERT_EQUAL_UINT32(4999999u, tfw_mps2_tone_reload(20000000u, 2u));
    TEST_ASSERT_EQUAL_UINT32(12499u,   tfw_mps2_tone_reload(25000000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(499u,     tfw_mps2_tone_reload(20000000u, 20000u));
    TEST_ASSERT_EQUAL_UINT32(0u,       tfw_mps2_tone_reload(20000000u, 20001u));
    TEST_ASSERT_EQUAL_UINT32(0u,       tfw_mps2_tone_reload(20000000u, 0u));
}

/* Start arms TIMER0's interrupt in the NVIC; every interrupt clears the
 * timer's and flips the pin, and only the pin. */
static void test_tone_toggles_per_interrupt(void)
{
    m_led = 0x1u;   /* LED0 on: the tone must leave it alone */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
    TEST_ASSERT_EQUAL_UINT32(1uL << TFW_MPS2_TIMER0_IRQ, m_iser);
    TEST_ASSERT_EQUAL_UINT32(0x1u, m_led);
    for (unsigned i = 1; i <= 4u; i++) {
        M_INTSTATUS = 0xAAu;
        mps2_tone_model_edge();
        TEST_ASSERT_EQUAL_UINT32(1u, M_INTSTATUS);   /* INTCLEAR written */
        TEST_ASSERT_EQUAL_UINT32((i & 1u) ? 0x3u : 0x1u, m_led);
    }
}

/* A DIGITAL_WRITE to LED0 while the tone runs on LED1, TIMER0's interrupt
 * landing at each of the write's LED-word accesses in turn (the second is
 * the store, between it and the read): LED1's edge stays in place, and the
 * write ends with PRIMASK as it found it. */
static void test_gpio_write_keeps_tone_edge(void)
{
    static const tfw_pinref_t LED0 = TFW_PB(0u, 0u);

    for (unsigned at = 1; at <= 2u; at++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
        m_led_at = at;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED0, 1u));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x3u, m_led, "LED0 set, LED1's edge kept");
        TEST_ASSERT_EQUAL_UINT32(0u, m_primask);
        TEST_ASSERT_EQUAL_UINT8(0u, m_edge_held);

        m_led_seen = 0;
        m_led_at = at;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED0, 0u));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x0u, m_led, "LED0 clear, LED1's edge kept");
    }
}

/* A second start retargets: the new reload, the timer still running. */
static void test_tone_retarget(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
    mps2_tone_model_edge();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 2000u));
    TEST_ASSERT_EQUAL_UINT32(4999u, M_RELOAD);
    TEST_ASSERT_EQUAL_UINT32(4999u, M_VALUE);
    TEST_ASSERT_EQUAL_UINT32(TFW_MPS2_TIMER_CTRL_EN | TFW_MPS2_TIMER_CTRL_INTEN, M_CTRL);
    TEST_ASSERT_EQUAL_UINT32(0u, m_led);   /* restarted from low */
}

/* Stop disables the timer and its interrupt and leaves the pin low. */
static void test_tone_stop_leaves_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
    mps2_tone_model_edge();
    TEST_ASSERT_EQUAL_UINT32(0x2u, m_led);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT32(0u, M_CTRL);
    TEST_ASSERT_EQUAL_UINT32(1uL << TFW_MPS2_TIMER0_IRQ, m_icer);
    TEST_ASSERT_EQUAL_UINT32(0u, m_led);
    mps2_tone_model_edge();   /* a late interrupt does not raise the pin */
    TEST_ASSERT_EQUAL_UINT32(0u, m_led);
}

/* 0 Hz stops, as on the AVR. */
static void test_tone_zero_hz_stops(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&PIN, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, M_CTRL);
}

/* A frequency above the bound is refused and the timer is not touched. */
static void test_tone_range_refused(void)
{
    M_RELOAD = 0x1234u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&PIN, 65535u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&PIN, 20001u));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, M_RELOAD);
    TEST_ASSERT_EQUAL_UINT32(0u, M_CTRL);
    TEST_ASSERT_EQUAL_UINT32(0u, m_iser);
}

/* Only an LED bit can carry the tone. */
static void test_tone_bad_pin_refused(void)
{
    const tfw_pinref_t ncs = TFW_PB(1u, 8u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&ncs, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, M_CTRL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_1khz_reload);
    RUN_TEST(test_tone_reload_table);
    RUN_TEST(test_tone_toggles_per_interrupt);
    RUN_TEST(test_gpio_write_keeps_tone_edge);
    RUN_TEST(test_tone_retarget);
    RUN_TEST(test_tone_stop_leaves_pin_low);
    RUN_TEST(test_tone_zero_hz_stops);
    RUN_TEST(test_tone_range_refused);
    RUN_TEST(test_tone_bad_pin_refused);
    return UNITY_END();
}
