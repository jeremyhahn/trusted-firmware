/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's reset-time setup and watchdog (src/hal/cortex-m/nrf54l/
 * hal_soc.c) through tfw_cm_soc_early, tfw_cm_soc_watchdog_arm and
 * tfw_cm_soc_watchdog_pet (called by test/model/cortex-m/nrf54l_soc_entry.c)
 * on the register file (test/model/cortex-m/regfile_model.c), against the registers soc_regs.h names from the nrfx MDK:
 * the stop of a running WDT30 (TSEN's key, TASKS_STOP, the reload when it
 * will not stop), OSCILLATORS.PLL.FREQ at 128 MHz, the FICR trims copy_trims
 * writes, the device configuration word, errata 32, 40 and 31 on the first
 * revision only, the RESETREAS pin-reset fold, CLOCK.TASKS_XOSTART; on arm
 * WDT CRV for 8 s, RREN, CONFIG, INTEN and NMIEN off and TASKS_START; on pet
 * the reload value in RR[0]. A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "regfile_model.h"
#include "nrf54l_soc_entry.h"

/* The registers by address, for the register file's peek, which reads a word
 * without allocating it (a register the code never wrote reads 0). */
#define WDT_ADDR(off)        ((uintptr_t)TFW_NRF_WDT_BASE + (off))
#define WDT_START_ADDR       WDT_ADDR(0x000u)
#define WDT_STOP_ADDR        WDT_ADDR(0x004u)
#define WDT_EV_TIMEOUT_ADDR  WDT_ADDR(0x100u)
#define WDT_EV_STOPPED_ADDR  WDT_ADDR(0x104u)
#define WDT_INTEN_ADDR       WDT_ADDR(0x300u)
#define WDT_NMIEN_ADDR       WDT_ADDR(0x320u)
#define WDT_CRV_ADDR         WDT_ADDR(0x504u)
#define WDT_RREN_ADDR        WDT_ADDR(0x508u)
#define WDT_CONFIG_ADDR      WDT_ADDR(0x50Cu)
#define WDT_TSEN_ADDR        WDT_ADDR(0x520u)
#define WDT_RR0_ADDR         WDT_ADDR(0x600u)
#define PLL_FREQ_ADDR        0x50120800u
#define XOSTART_ADDR         0x5010E000u
#define RESETREAS_ADDR       0x5010E600u
#define DEVCFG_ADDR          0x50120440u
#define ERRATA32_ADDR        0x50120640u
#define ERRATA40_ADDR        0x5008A7ACu
#define ERRATA31_A_ADDR      0x50120624u
#define ERRATA31_B_ADDR      0x5012063Cu

/* 8 s of the 32.768 kHz clock, less one: the watchdog resets at zero. */
#define CRV_8S               (8uL * 32768uL - 1uL)

/* Two trims, each at an address no other test writes. */
#define TRIM0_ADDR           0x50120300u
#define TRIM0_DATA           0x12345678uL
#define TRIM1_ADDR           0x5008A100u
#define TRIM1_DATA           0x9ABCDEF0uL

static void trim_entry(uint32_t i, uint32_t addr, uint32_t data)
{
    tfw_nrf54l_model_poke(TFW_NRF_FICR_TRIMCNF + 8u * i, addr);
    tfw_nrf54l_model_poke(TFW_NRF_FICR_TRIMCNF + 8u * i + 4u, data);
}

static void rev1(void)
{
    TFW_NRF_FICR_PART     = 0x1CuL;
    TFW_NRF_FICR_REVISION = 0x01uL;
}

void setUp(void)
{
    regfile_model_reset();
    tfw_nrf54l_model_mark_clear();
    /* An erased table: copy_trims stops at the first entry. */
    trim_entry(0u, 0xFFFFFFFFuL, 0xFFFFFFFFuL);
}

void tearDown(void) {}

/* ---- tfw_cm_soc_early */

static void test_early_stopped_watchdog_is_left_alone(void)
{
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_TSEN_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_STOP_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_RR0_ADDR));
}

/* RUNSTATUS stays 1 on the register file, so this is the watchdog another
 * image started without STOPEN: enabled, stopped, and reloaded when it runs
 * on, its events left as they were. */
static void test_early_running_watchdog_stopped_then_reloaded(void)
{
    TFW_NRF_WDT_RUNSTATUS  = 1u;
    TFW_NRF_WDT_EV_TIMEOUT = 1u;
    TFW_NRF_WDT_EV_STOPPED = 1u;
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0x6E524635uL, regfile_model_peek(WDT_TSEN_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(WDT_STOP_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x6E524635uL, regfile_model_peek(WDT_RR0_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(WDT_EV_TIMEOUT_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(WDT_EV_STOPPED_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_START_ADDR));
}

static void test_early_pll_at_128mhz_and_crystal_started(void)
{
    TFW_NRF_PLL_FREQ = 3u;   /* CK64M, the reset value */
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(PLL_FREQ_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(XOSTART_ADDR));
}

static void test_early_copies_trims_to_the_erased_entry(void)
{
    trim_entry(0u, TRIM0_ADDR, TRIM0_DATA);
    trim_entry(1u, TRIM1_ADDR, TRIM1_DATA);
    trim_entry(2u, 0xFFFFFFFFuL, 0xFFFFFFFFuL);
    trim_entry(3u, ERRATA40_ADDR, 0x55555555uL);   /* past the end */
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(TRIM0_DATA, regfile_model_peek(TRIM0_ADDR));
    TEST_ASSERT_EQUAL_HEX32(TRIM1_DATA, regfile_model_peek(TRIM1_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ERRATA40_ADDR));
}

static void test_early_trims_end_at_a_zero_address(void)
{
    trim_entry(0u, TRIM0_ADDR, TRIM0_DATA);
    trim_entry(1u, 0u, TRIM1_DATA);
    trim_entry(2u, TRIM1_ADDR, TRIM1_DATA);
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(TRIM0_DATA, regfile_model_peek(TRIM0_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TRIM1_ADDR));
}

static void test_early_device_configuration_when_unset(void)
{
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0xC8u, regfile_model_peek(DEVCFG_ADDR));
}

static void test_early_device_configuration_kept_when_set(void)
{
    tfw_nrf54l_model_poke(DEVCFG_ADDR, 0x64u);
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0x64u, regfile_model_peek(DEVCFG_ADDR));
}

static void test_early_errata_on_the_first_revision(void)
{
    rev1();
    TFW_NRF_FICR_ERRATA32_TRIM = 0x180A1D00uL;
    tfw_nrf54l_model_poke(ERRATA31_B_ADDR, 0xFFFFFFFFuL);
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0x1EA9E040uL, regfile_model_peek(ERRATA32_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x040A0078uL, regfile_model_peek(ERRATA40_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x34u, regfile_model_peek(ERRATA31_A_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0xFFF7FFFFuL, regfile_model_peek(ERRATA31_B_ADDR));
}

static void test_early_errata32_not_past_its_trim(void)
{
    rev1();
    TFW_NRF_FICR_ERRATA32_TRIM = 0x180A1D01uL;
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ERRATA32_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x040A0078uL, regfile_model_peek(ERRATA40_ADDR));
}

static void test_early_no_errata_on_a_later_revision(void)
{
    TFW_NRF_FICR_PART     = 0x1CuL;
    TFW_NRF_FICR_REVISION = 0x02uL;
    tfw_nrf54l_model_poke(ERRATA31_B_ADDR, 0xFFFFFFFFuL);
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ERRATA32_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ERRATA40_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ERRATA31_A_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, regfile_model_peek(ERRATA31_B_ADDR));
}

/* RESETREAS is write-1-to-clear: a pin reset clears every other reason. */
static void test_early_pin_reset_clears_the_other_reasons(void)
{
    TFW_NRF_RESETREAS = 0x5u;
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEuL, regfile_model_peek(RESETREAS_ADDR));
}

static void test_early_other_reset_reasons_left(void)
{
    TFW_NRF_RESETREAS = 0x4u;
    tfw_nrf54l_model_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0x4u, regfile_model_peek(RESETREAS_ADDR));
}

/* ---- tfw_cm_soc_watchdog_arm and _pet */

static void test_arm_programs_and_starts_the_watchdog(void)
{
    TFW_NRF_WDT_INTEN = 0x1u;
    TFW_NRF_WDT_NMIEN = 0x1u;
    tfw_nrf54l_model_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_nrf54l_model_mark());
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_INTEN_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_NMIEN_ADDR));
    TEST_ASSERT_EQUAL_HEX32(CRV_8S, regfile_model_peek(WDT_CRV_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x1u, regfile_model_peek(WDT_RREN_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x41u, regfile_model_peek(WDT_CONFIG_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(WDT_START_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_RR0_ADDR));
}

/* Running, its registers are locked: arming again only reloads it. */
static void test_arm_when_running_is_a_pet(void)
{
    TFW_NRF_WDT_RUNSTATUS = 1u;
    tfw_nrf54l_model_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_nrf54l_model_mark());
    TEST_ASSERT_EQUAL_HEX32(0x6E524635uL, regfile_model_peek(WDT_RR0_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_CRV_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_CONFIG_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_START_ADDR));
}

static void test_pet_writes_the_reload_value(void)
{
    tfw_nrf54l_model_watchdog_pet();
    TEST_ASSERT_EQUAL_HEX32(0x6E524635uL, regfile_model_peek(WDT_RR0_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(WDT_START_ADDR));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_stopped_watchdog_is_left_alone);
    RUN_TEST(test_early_running_watchdog_stopped_then_reloaded);
    RUN_TEST(test_early_pll_at_128mhz_and_crystal_started);
    RUN_TEST(test_early_copies_trims_to_the_erased_entry);
    RUN_TEST(test_early_trims_end_at_a_zero_address);
    RUN_TEST(test_early_device_configuration_when_unset);
    RUN_TEST(test_early_device_configuration_kept_when_set);
    RUN_TEST(test_early_errata_on_the_first_revision);
    RUN_TEST(test_early_errata32_not_past_its_trim);
    RUN_TEST(test_early_no_errata_on_a_later_revision);
    RUN_TEST(test_early_pin_reset_clears_the_other_reasons);
    RUN_TEST(test_early_other_reset_reasons_left);
    RUN_TEST(test_arm_programs_and_starts_the_watchdog);
    RUN_TEST(test_arm_when_running_is_a_pet);
    RUN_TEST(test_pet_writes_the_reload_value);
    return UNITY_END();
}
