/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 open-drain line (gpio_line.h) as src/hal/cortex-m/mps2/hal_gpio.c
 * drives it, on a model of GPIO block 0's registers behind cm_regs.h's
 * TFW_CM_REG (-DTFW_CM_MODEL_REGFILE). test_mps2_onewire.c models the line
 * above these helpers; this suite runs the helpers themselves.
 *
 * The model keeps DATAOUT and OUTEN, and takes OUTENSET and OUTENCLR as the
 * block does: a write sets or clears those OUTEN bits and reads back 0. A
 * write lands when the next register is looked up, or at settle(), and each
 * one is logged in order with DATAOUT as it stood: so an output enabled
 * while its DATAOUT bit was still 1, a glitch high on a pulled-up line, shows
 * in the log. DATA reads the line: low where an output drives a 0, high
 * (pulled up) everywhere else.
 */
#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "gpio_line.h"

#define OW_MASK  (1u << TFW_MPS2_OW_BIT)
#define MAX_EV   16u

#define A_DATA     (TFW_MPS2_GPIO0_BASE + 0x00u)
#define A_DATAOUT  (TFW_MPS2_GPIO0_BASE + 0x04u)
#define A_OUTENSET (TFW_MPS2_GPIO0_BASE + 0x10u)
#define A_OUTENCLR (TFW_MPS2_GPIO0_BASE + 0x14u)

typedef enum { EV_DATAOUT, EV_OUTENSET, EV_OUTENCLR } ev_kind_t;

typedef struct {
    ev_kind_t kind;
    uint32_t  value;    /* the word written */
    uint32_t  dataout;  /* DATAOUT when it landed */
} gpio_ev_t;

static struct {
    uint32_t  data;
    uint32_t  dataout;
    uint32_t  outen;
    uint32_t  outenset;  /* a write not yet landed */
    uint32_t  outenclr;
    uint32_t  seen_dataout;
    gpio_ev_t ev[MAX_EV];
    unsigned  nev;
} g;

static void log_ev(ev_kind_t kind, uint32_t value)
{
    TEST_ASSERT_TRUE_MESSAGE(g.nev < MAX_EV, "event log full");
    g.ev[g.nev].kind = kind;
    g.ev[g.nev].value = value;
    g.ev[g.nev].dataout = g.dataout;
    g.nev++;
}

static void settle(void)
{
    if (g.dataout != g.seen_dataout) {
        log_ev(EV_DATAOUT, g.dataout);
        g.seen_dataout = g.dataout;
    }
    if (g.outenset != 0u) {
        log_ev(EV_OUTENSET, g.outenset);
        g.outen |= g.outenset;
        g.outenset = 0u;
    }
    if (g.outenclr != 0u) {
        log_ev(EV_OUTENCLR, g.outenclr);
        g.outen &= ~g.outenclr;
        g.outenclr = 0u;
    }
}

volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    settle();
    switch (addr) {
    case A_DATA:
        g.data = ~(g.outen & ~g.dataout);
        return &g.data;
    case A_DATAOUT:
        return &g.dataout;
    case A_OUTENSET:
        return &g.outenset;
    case A_OUTENCLR:
        return &g.outenclr;
    default:
        TEST_FAIL_MESSAGE("a register outside GPIO block 0's line");
        return &g.data;
    }
}

/* The block as a previous user left it: every DATAOUT bit 1, other outputs on. */
static void preset(uint32_t dataout, uint32_t outen)
{
    memset(&g, 0, sizeof g);
    g.dataout = dataout;
    g.seen_dataout = dataout;
    g.outen = outen;
}

void setUp(void) { memset(&g, 0, sizeof g); }
void tearDown(void) {}

/* drive_low clears the mask's DATAOUT bit, then enables the mask's output:
 * the output comes on at 0 and never drives the line high. */
static void test_drive_low_clears_dataout_before_output(void)
{
    preset(0xFFFFFFFFu, 0x000000F0u);
    tfw_mps2_gpio_drive_low(OW_MASK);
    settle();

    TEST_ASSERT_EQUAL_UINT(2u, g.nev);
    TEST_ASSERT_EQUAL(EV_DATAOUT, g.ev[0].kind);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu & ~OW_MASK, g.ev[0].value);
    TEST_ASSERT_EQUAL(EV_OUTENSET, g.ev[1].kind);
    TEST_ASSERT_EQUAL_HEX32(OW_MASK, g.ev[1].value);
    TEST_ASSERT_EQUAL_HEX32(0u, g.ev[1].dataout & OW_MASK);
    TEST_ASSERT_EQUAL_HEX32(0x000000F0u | OW_MASK, g.outen);
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mps2_gpio_lines() & OW_MASK);
}

/* release writes OUTENCLR with the mask and nothing else: the output goes
 * off, the pull-up takes the line high, DATAOUT is not touched. */
static void test_release_disables_output_only(void)
{
    preset(0xFFFFFFFFu & ~OW_MASK, 0x000000F0u | OW_MASK);
    tfw_mps2_gpio_release(OW_MASK);
    settle();

    TEST_ASSERT_EQUAL_UINT(1u, g.nev);
    TEST_ASSERT_EQUAL(EV_OUTENCLR, g.ev[0].kind);
    TEST_ASSERT_EQUAL_HEX32(OW_MASK, g.ev[0].value);
    TEST_ASSERT_EQUAL_HEX32(0x000000F0u, g.outen);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu & ~OW_MASK, g.dataout);
    TEST_ASSERT_EQUAL_HEX32(OW_MASK, tfw_mps2_gpio_lines() & OW_MASK);
}

/* A pulse leaves the other bits of DATAOUT and OUTEN as it found them, with
 * them at 1 and then at 0. */
static void test_pulse_keeps_unrelated_bits(void)
{
    static const uint32_t k_dataout[] = { 0xA5A5A5A5u, 0x5A5A5A5Au };
    static const uint32_t k_outen[] = { 0x0000FF00u, 0xFFFF00FEu };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        preset(k_dataout[i], k_outen[i] & ~OW_MASK);
        tfw_mps2_gpio_drive_low(OW_MASK);
        settle();
        TEST_ASSERT_EQUAL_HEX32(k_dataout[i] & ~OW_MASK, g.dataout);
        TEST_ASSERT_EQUAL_HEX32((k_outen[i] & ~OW_MASK) | OW_MASK, g.outen);
        tfw_mps2_gpio_release(OW_MASK);
        settle();
        TEST_ASSERT_EQUAL_HEX32(k_dataout[i] & ~OW_MASK, g.dataout);
        TEST_ASSERT_EQUAL_HEX32(k_outen[i] & ~OW_MASK, g.outen);
    }
}

/* The line reads back through DATA: low while driven, high once released. */
static void test_lines_follow_the_pin(void)
{
    preset(0xFFFFFFFFu, 0u);
    TEST_ASSERT_EQUAL_HEX32(OW_MASK, tfw_mps2_gpio_lines() & OW_MASK);
    tfw_mps2_gpio_drive_low(OW_MASK);
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mps2_gpio_lines() & OW_MASK);
    tfw_mps2_gpio_release(OW_MASK);
    TEST_ASSERT_EQUAL_HEX32(OW_MASK, tfw_mps2_gpio_lines() & OW_MASK);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_drive_low_clears_dataout_before_output);
    RUN_TEST(test_release_disables_output_only);
    RUN_TEST(test_pulse_keeps_unrelated_bits);
    RUN_TEST(test_lines_follow_the_pin);
    return UNITY_END();
}
