/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The rp2350 SoC's reset cause (src/hal/cortex-m/rp2350/soc_reset.c), on
 * x86, against fake WATCHDOG.REASON and POWMAN.CHIP_RESET words.
 *
 * Every bit is a literal copied from the vendor's headers (the pico-sdk's
 * watchdog.h and powman.h, WATCHDOG_REASON_*_BITS and
 * POWMAN_CHIP_RESET_*_BITS), not taken from the code under test.
 */

#include "unity.h"

#include "soc_reset.h"

void setUp(void) { }
void tearDown(void) { }

#define TIMER 0x00000001u
#define FORCE 0x00000002u

#define DOUBLE_TAP               0x00000001u
#define RESCUE_FLAG              0x00000010u
#define HAD_POR                  0x00010000u
#define HAD_BOR                  0x00020000u
#define HAD_RUN_LOW              0x00040000u
#define HAD_DP_RESET_REQ         0x00080000u
#define HAD_RESCUE               0x00200000u
#define HAD_WATCHDOG_POWMAN_ASYNC 0x00400000u
#define HAD_WATCHDOG_POWMAN      0x00800000u
#define HAD_WATCHDOG_SWCORE      0x01000000u
#define HAD_SWCORE_PD            0x02000000u
#define HAD_GLITCH_DETECT        0x04000000u
#define HAD_HZD_SYS_RESET_REQ    0x08000000u
#define HAD_WATCHDOG_RSM         0x10000000u

#define ASKED     1u
#define NOT_ASKED 0u

static void test_the_watchdog_timer_is_a_watchdog_whatever_else(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_rp2350_reset_decode(TIMER, HAD_POR, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_rp2350_reset_decode(TIMER, HAD_RUN_LOW, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_rp2350_reset_decode(TIMER, 0u, NOT_ASKED));
}

/* Nothing here forces the watchdog, so a forced trigger is no cause the HAL
 * has a value for, alone or beside the timer. */
static void test_a_forced_trigger_is_unknown(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_rp2350_reset_decode(FORCE, HAD_POR, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_rp2350_reset_decode(FORCE, HAD_POR, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_rp2350_reset_decode(TIMER | FORCE, HAD_POR, NOT_ASKED));
}

/* SYSRESETREQ clears REASON and leaves CHIP_RESET: the marker says it. */
static void test_a_requested_reset_is_software(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_rp2350_reset_decode(0u, HAD_POR, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_rp2350_reset_decode(0u, HAD_RUN_LOW, ASKED));
}

static void test_each_chip_reset_with_a_value_maps_to_it(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON, tfw_rp2350_reset_decode(0u, HAD_POR, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_rp2350_reset_decode(0u, HAD_BOR, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_rp2350_reset_decode(0u, HAD_RUN_LOW, NOT_ASKED));
}

/* DOUBLE_TAP and RESCUE_FLAG are flags, not sources: they change nothing. */
static void test_the_two_flags_are_not_a_source(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON,
                      tfw_rp2350_reset_decode(0u, HAD_POR | DOUBLE_TAP | RESCUE_FLAG, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_rp2350_reset_decode(0u, DOUBLE_TAP, NOT_ASKED));
}

static void test_a_chip_reset_with_no_value_is_unknown(void)
{
    static const uint32_t none[] = {
        0u, HAD_DP_RESET_REQ, HAD_RESCUE, HAD_WATCHDOG_POWMAN_ASYNC, HAD_WATCHDOG_POWMAN,
        HAD_WATCHDOG_SWCORE, HAD_SWCORE_PD, HAD_GLITCH_DETECT, HAD_HZD_SYS_RESET_REQ,
        HAD_WATCHDOG_RSM, HAD_POR | HAD_BOR, HAD_POR | HAD_RUN_LOW,
    };

    for (uint32_t i = 0u; i < sizeof none / sizeof none[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_rp2350_reset_decode(0u, none[i], NOT_ASKED));
    }
}

/* A take reads both words and writes neither. */
static void test_a_take_reads_both_and_writes_neither(void)
{
    const volatile uint32_t reason     = TIMER;
    const volatile uint32_t chip_reset = HAD_RUN_LOW;
    const volatile uint32_t no_reason  = 0u;

    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_rp2350_reset_take(&reason, &chip_reset, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_rp2350_reset_take(&no_reason, &chip_reset, NOT_ASKED));
    TEST_ASSERT_EQUAL_HEX32(TIMER, reason);
    TEST_ASSERT_EQUAL_HEX32(HAD_RUN_LOW, chip_reset);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_watchdog_timer_is_a_watchdog_whatever_else);
    RUN_TEST(test_a_forced_trigger_is_unknown);
    RUN_TEST(test_a_requested_reset_is_software);
    RUN_TEST(test_each_chip_reset_with_a_value_maps_to_it);
    RUN_TEST(test_the_two_flags_are_not_a_source);
    RUN_TEST(test_a_chip_reset_with_no_value_is_unknown);
    RUN_TEST(test_a_take_reads_both_and_writes_neither);
    return UNITY_END();
}
