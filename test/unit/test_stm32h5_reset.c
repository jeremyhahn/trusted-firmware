/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stm32h5 SoC's reset cause (src/hal/cortex-m/stm32h5/soc_reset.c), on
 * x86, against a fake RCC_RSR word.
 *
 * Every bit is a literal copied from the vendor's header (cmsis-device-h5's
 * stm32h573xx.h, RCC_RSR_*_Pos), not taken from the code under test: a cause
 * read from the wrong bit reports a watchdog bite to the host of a device
 * whose reset pin was only pressed.
 */

#include "unity.h"

#include "soc_reset.h"

void setUp(void) { }
void tearDown(void) { }

#define RMVF     (1u << 23)
#define PINRSTF  (1u << 26)
#define BORRSTF  (1u << 27)
#define SFTRSTF  (1u << 28)
#define IWDGRSTF (1u << 29)
#define WWDGRSTF (1u << 30)
#define LPWRRSTF (1u << 31)

/* Whether the reset handler found tfw_cm_system_reset's marker. */
#define ASKED     1u
#define NOT_ASKED 0u

/* The register's word rules: the marker changes nothing but SFTRSTF. */
static void assert_either_way(tfw_reset_cause_t want, uint32_t rsr, const char *what)
{
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_h5_reset_decode(rsr, NOT_ASKED), what);
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_h5_reset_decode(rsr, ASKED), what);
}

static void test_each_flag_with_a_value_maps_to_it(void)
{
    assert_either_way(TFW_RESET_EXTERNAL, PINRSTF, "PINRSTF");
    assert_either_way(TFW_RESET_POWER_ON, BORRSTF, "BORRSTF");
    assert_either_way(TFW_RESET_WATCHDOG, IWDGRSTF, "IWDGRSTF");
    assert_either_way(TFW_RESET_WATCHDOG, WWDGRSTF, "WWDGRSTF");
    assert_either_way(TFW_RESET_WATCHDOG, IWDGRSTF | WWDGRSTF, "both watchdogs");
}

/* An internal reset may drive NRST and latch PINRSTF beside its own flag
 * (RM0481, to confirm): the pin's flag beside one cause is that cause. */
static void test_the_pin_beside_a_cause_is_that_cause(void)
{
    assert_either_way(TFW_RESET_POWER_ON, BORRSTF | PINRSTF, "BORRSTF and PINRSTF");
    assert_either_way(TFW_RESET_WATCHDOG, IWDGRSTF | PINRSTF, "IWDGRSTF and PINRSTF");
    assert_either_way(TFW_RESET_WATCHDOG, WWDGRSTF | PINRSTF, "WWDGRSTF and PINRSTF");
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_h5_reset_decode(SFTRSTF | PINRSTF, ASKED));
}

/* SFTRSTF is the CPU's reset request, which a debugger can make too: it is
 * the firmware's own restart only when the firmware left its marker. */
static void test_a_reset_request_is_software_only_when_the_firmware_asked(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_h5_reset_decode(SFTRSTF, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_h5_reset_decode(SFTRSTF, NOT_ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_h5_reset_decode(SFTRSTF | PINRSTF, NOT_ASKED));
}

/* No flag at all: a power-up sets BORRSTF, so an empty word is no record. */
static void test_nothing_latched_is_unknown(void)
{
    assert_either_way(TFW_RESET_UNKNOWN, 0u, "nothing latched");
}

static void test_a_flag_with_no_value_is_unknown(void)
{
    assert_either_way(TFW_RESET_UNKNOWN, LPWRRSTF, "LPWRRSTF");
    assert_either_way(TFW_RESET_UNKNOWN, LPWRRSTF | PINRSTF, "LPWRRSTF and PINRSTF");
}

/* Bits 0-22, 24 and 25 the header does not define, and RMVF, which reads 0. */
static void test_a_bit_the_header_does_not_define_is_unknown(void)
{
    for (uint32_t bit = 0u; bit < 26u; bit++) {
        assert_either_way(TFW_RESET_UNKNOWN, 1u << bit, "an undefined bit");
    }
}

/* TFW_RESET_WATCHDOG is what the update-request gate honours: a watchdog
 * flag beside any bit but the pin's is not a watchdog reset. */
static void test_a_watchdog_with_any_other_bit_is_unknown(void)
{
    for (uint32_t bit = 0u; bit < 32u; bit++) {
        uint32_t other = 1u << bit;

        if (other == IWDGRSTF || other == WWDGRSTF || other == PINRSTF) {
            continue;
        }
        assert_either_way(TFW_RESET_UNKNOWN, IWDGRSTF | other, "IWDGRSTF with another bit");
        assert_either_way(TFW_RESET_UNKNOWN, WWDGRSTF | other, "WWDGRSTF with another bit");
    }
}

static void test_two_causes_with_values_are_unknown(void)
{
    assert_either_way(TFW_RESET_UNKNOWN, BORRSTF | SFTRSTF, "BORRSTF and SFTRSTF");
    assert_either_way(TFW_RESET_UNKNOWN, BORRSTF | IWDGRSTF, "BORRSTF and IWDGRSTF");
    assert_either_way(TFW_RESET_UNKNOWN, SFTRSTF | IWDGRSTF, "SFTRSTF and IWDGRSTF");
}

/* A take decodes what was latched, then writes RMVF and nothing else. */
static void test_a_take_decodes_then_writes_rmvf(void)
{
    static const uint32_t latched[] = {
        0u, PINRSTF, BORRSTF | PINRSTF, IWDGRSTF | PINRSTF, SFTRSTF, LPWRRSTF, 0xFFFFFFFFu,
    };

    for (uint32_t i = 0u; i < sizeof latched / sizeof latched[0]; i++) {
        volatile uint32_t fake = latched[i];

        TEST_ASSERT_EQUAL(tfw_h5_reset_decode(latched[i], ASKED), tfw_h5_reset_take(&fake, ASKED));
        TEST_ASSERT_EQUAL_HEX32(RMVF, fake);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_flag_with_a_value_maps_to_it);
    RUN_TEST(test_the_pin_beside_a_cause_is_that_cause);
    RUN_TEST(test_a_reset_request_is_software_only_when_the_firmware_asked);
    RUN_TEST(test_nothing_latched_is_unknown);
    RUN_TEST(test_a_flag_with_no_value_is_unknown);
    RUN_TEST(test_a_bit_the_header_does_not_define_is_unknown);
    RUN_TEST(test_a_watchdog_with_any_other_bit_is_unknown);
    RUN_TEST(test_two_causes_with_values_are_unknown);
    RUN_TEST(test_a_take_decodes_then_writes_rmvf);
    return UNITY_END();
}
