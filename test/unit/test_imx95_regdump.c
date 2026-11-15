/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The i.MX 95 M7's bench register dump (TFW_CM_REG_DUMP, hal_soc.c's
 * step_dump, layout in soc_regs.h) against the register model: after each
 * step mark, LPUART7's registers as the M7 reads them, and the polls the mark
 * waited for STAT.TDRE, land in the board's MU5 SRAM area for the System
 * Manager's `md`. Built with -DTFW_CM_STEP_MARKS=1 -DTFW_CM_REG_DUMP=1
 * (mk/host.mk); the default build has neither.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "hal_early.h"
#include "board_profile.h"
#include "soc_regs.h"
#include "imx95_model.h"

#define VERID 0x04040007u
#define PARAM 0x00000202u
#define POLLS 1000000u          /* hal_soc.c STEP_POLLS */

static struct tfw_imx95_model *m;

int32_t tfw_imx95_console_route(void);
int32_t tfw_imx95_console_route(void)
{
    return 0;
}

static uint32_t slot(uint32_t n, uint32_t word)
{
    m = tfw_imx95_model_get();
    return m->dump.words[TFW_IMX95_REGDUMP_HDR_WORDS +
                         (n % TFW_IMX95_REGDUMP_SLOTS) * TFW_IMX95_REGDUMP_SLOT_WORDS + word];
}

void setUp(void)
{
    tfw_imx95_model_reset();
    tfw_imx95_model_reset_mark_clear();
    m = tfw_imx95_model_get();
    m->dump.base    = TFW_IMX95_REGDUMP_BASE;
    m->lpuart.verid = VERID;
    m->lpuart.param = PARAM;
}

void tearDown(void)
{
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
    TEST_ASSERT_EQUAL_UINT32(0u, m->primask);
}

/* The area sits past the SM's three SMT channels and inside the 1 KiB SRAM
 * (bench record section 14), and the model maps all of it. */
static void test_the_area_fits_between_the_channels_and_the_sram_end(void)
{
    uint32_t words = TFW_IMX95_REGDUMP_HDR_WORDS +
                     TFW_IMX95_REGDUMP_SLOTS * TFW_IMX95_REGDUMP_SLOT_WORDS;

    TEST_ASSERT_TRUE(TFW_IMX95_REGDUMP_BASE >= TFW_IMX95_SCMI_SHM_BASE + 3u * 0x80u);
    TEST_ASSERT_TRUE(TFW_IMX95_REGDUMP_BASE + 4u * words <= TFW_IMX95_SCMI_SHM_BASE + 0x400u);
    TEST_ASSERT_TRUE(words <= TFW_IMX95_MODEL_DUMP_WORDS);
}

static void test_each_mark_records_the_registers_after_it(void)
{
    static const char marks[] = "RDMPU";
    uint32_t i;

    for (i = 0u; marks[i] != '\0'; i++) {
        TFW_CM_STEP((uint8_t)marks[i]);
    }
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_REGDUMP_MAGIC, m->dump.words[0]);
    TEST_ASSERT_EQUAL_UINT32(5u, m->dump.words[1]);
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_HEX32((uint32_t)marks[i] | (i << 8), slot(i, 0u));
        TEST_ASSERT_EQUAL_HEX32(VERID, slot(i, 1u));
        TEST_ASSERT_EQUAL_HEX32(PARAM, slot(i, 2u));
        TEST_ASSERT_EQUAL_HEX32(0x19000008u, slot(i, 4u));   /* 115200 at 24 MHz */
        TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_LPUART_STAT_TDRE | TFW_IMX95_LPUART_STAT_TC,
                                slot(i, 5u) & (TFW_IMX95_LPUART_STAT_TDRE | TFW_IMX95_LPUART_STAT_TC));
        TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_LPUART_CTRL_TE, slot(i, 6u));
        TEST_ASSERT_EQUAL_UINT32(0u, slot(i, 7u));
    }
    TEST_ASSERT_EQUAL_HEX32(m->lpuart.global, slot(0u, 3u));
}

/* A transmitter whose TDRE never sets: the slot says so, and the core goes on. */
static void test_a_stuck_transmitter_shows_in_the_polls(void)
{
    m->lpuart.busy = 1u;
    TFW_CM_STEP('R');
    TEST_ASSERT_EQUAL_UINT32(POLLS, slot(0u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0u, slot(0u, 5u) & TFW_IMX95_LPUART_STAT_TDRE);
}

/* 'R' restarts the count; past the last slot the records wrap. */
static void test_r_restarts_and_records_wrap(void)
{
    uint32_t i;

    TFW_CM_STEP('R');
    TFW_CM_STEP('D');
    TFW_CM_STEP('R');
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u, m->dump.words[1]);
    for (i = 1u; i <= TFW_IMX95_REGDUMP_SLOTS; i++) {
        TFW_CM_STEP('U');
    }
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(TFW_IMX95_REGDUMP_SLOTS + 1u, m->dump.words[1]);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)'U' | (TFW_IMX95_REGDUMP_SLOTS << 8), slot(0u, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_area_fits_between_the_channels_and_the_sram_end);
    RUN_TEST(test_each_mark_records_the_registers_after_it);
    RUN_TEST(test_a_stuck_transmitter_shows_in_the_polls);
    RUN_TEST(test_r_restarts_and_records_wrap);
    return UNITY_END();
}
