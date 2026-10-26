/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's reset cause (src/hal/cortex-m/nrf54l/soc_reset.c), on x86,
 * against a fake RESETREAS word.
 *
 * Every bit is a literal copied from the vendor's header (the nrfx MDK's
 * nrf54l15_types.h, RESET_RESETREAS_*_Pos), not taken from the code under
 * test: a cause read from the wrong bit reports a watchdog bite to the host
 * of a device whose reset pin was only pressed.
 */

#include "unity.h"

#include "soc_reset.h"

void setUp(void) { }
void tearDown(void) { }

#define RESETPIN   (1uL << 0)
#define DOG0       (1uL << 1)
#define DOG1       (1uL << 2)
#define CTRLAPSOFT (1uL << 3)
#define CTRLAPHARD (1uL << 4)
#define CTRLAPPIN  (1uL << 5)
#define SREQ       (1uL << 6)
#define LOCKUP     (1uL << 7)
#define OFF        (1uL << 8)
#define LPCOMP     (1uL << 9)
#define DIF        (1uL << 10)
#define GRTC       (1uL << 11)
#define NFC        (1uL << 12)
#define SECTAMPER  (1uL << 13)

/* Whether the reset handler found tfw_cm_system_reset's marker. */
#define ASKED     1u
#define NOT_ASKED 0u

/* The register's word rules: the marker changes nothing but SREQ. */
static void assert_either_way(tfw_reset_cause_t want, uint32_t resetreas, const char *what)
{
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_nrf54l_reset_decode(resetreas, NOT_ASKED), what);
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_nrf54l_reset_decode(resetreas, ASKED), what);
}

static void test_nothing_latched_is_power_on(void)
{
    assert_either_way(TFW_RESET_POWER_ON, 0u, "nothing latched");
}

static void test_each_bit_with_a_value_maps_to_it(void)
{
    assert_either_way(TFW_RESET_EXTERNAL, RESETPIN, "RESETPIN");
    assert_either_way(TFW_RESET_WATCHDOG, DOG0, "DOG0");
    assert_either_way(TFW_RESET_WATCHDOG, DOG1, "DOG1");
}

/* Both watchdogs and nothing else is still nothing but a watchdog. */
static void test_both_watchdogs_are_a_watchdog(void)
{
    assert_either_way(TFW_RESET_WATCHDOG, DOG0 | DOG1, "DOG0 and DOG1");
}

/* SREQ is the CPU's reset request, which a debugger can make too: it is the
 * firmware's own restart only when the firmware left its marker. */
static void test_a_reset_request_is_software_only_when_the_firmware_asked(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_nrf54l_reset_decode(SREQ, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_nrf54l_reset_decode(SREQ, NOT_ASKED));
}

/* The bits the HAL has no value for: the debugger's three resets, a CPU
 * lockup, the five wakes from System OFF and the tamper detector. */
static void test_a_bit_with_no_value_is_unknown(void)
{
    static const uint32_t none[] = {
        CTRLAPSOFT, CTRLAPHARD, CTRLAPPIN, LOCKUP,
        OFF, LPCOMP, DIF, GRTC, NFC, SECTAMPER,
    };

    for (uint32_t i = 0u; i < sizeof none / sizeof none[0]; i++) {
        assert_either_way(TFW_RESET_UNKNOWN, none[i], "a bit with no value of its own");
    }
}

/* Bits 14-31 the MDK does not define. */
static void test_a_bit_the_mdk_does_not_define_is_unknown(void)
{
    for (uint32_t bit = 14u; bit < 32u; bit++) {
        assert_either_way(TFW_RESET_UNKNOWN, 1uL << bit, "an undefined bit");
    }
}

/* TFW_RESET_WATCHDOG is what the update-request gate honours: a watchdog bit
 * beside any other bit is not a watchdog reset. */
static void test_a_watchdog_with_any_other_bit_is_unknown(void)
{
    for (uint32_t bit = 0u; bit < 32u; bit++) {
        uint32_t other = 1uL << bit;

        if (other == DOG0 || other == DOG1) {
            continue;
        }
        assert_either_way(TFW_RESET_UNKNOWN, DOG0 | other, "DOG0 with another bit");
        assert_either_way(TFW_RESET_UNKNOWN, DOG1 | other, "DOG1 with another bit");
    }
}

static void test_two_causes_with_values_are_unknown(void)
{
    assert_either_way(TFW_RESET_UNKNOWN, RESETPIN | SREQ, "RESETPIN and SREQ");
    assert_either_way(TFW_RESET_UNKNOWN, SREQ | (1uL << 31), "SREQ and bit 31");
    assert_either_way(TFW_RESET_UNKNOWN, RESETPIN | (1uL << 14), "RESETPIN and bit 14");
}

/* RESETREAS is write-1-to-clear (system_nrf54l.c clears every bit but the
 * pin's by writing ~RESETPIN): a take writes all ones. The fake holds what
 * was written; the register after it is what was latched less those ones. */
static void test_a_take_decodes_then_clears_every_bit(void)
{
    static const uint32_t latched[] = { 0u, RESETPIN, DOG0, SREQ, LOCKUP, 1uL << 31, 0xFFFFFFFFuL };

    for (uint32_t i = 0u; i < sizeof latched / sizeof latched[0]; i++) {
        volatile uint32_t fake = latched[i];

        TEST_ASSERT_EQUAL(tfw_nrf54l_reset_decode(latched[i], ASKED),
                          tfw_nrf54l_reset_take(&fake, ASKED));
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, fake);
        TEST_ASSERT_EQUAL_HEX32(0u, latched[i] & ~fake);
    }
}

/* A watchdog reset followed by a power cycle: the second boot reads what the
 * first left, so it must not be told a watchdog bit. */
static void test_a_later_reset_is_not_reported_as_an_earlier_one(void)
{
    volatile uint32_t fake = DOG0;
    uint32_t          written;

    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_nrf54l_reset_take(&fake, NOT_ASKED));
    written = fake;
    fake    = DOG0 & ~written;        /* the hardware's write-1-to-clear */
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON, tfw_nrf54l_reset_take(&fake, NOT_ASKED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_latched_is_power_on);
    RUN_TEST(test_each_bit_with_a_value_maps_to_it);
    RUN_TEST(test_both_watchdogs_are_a_watchdog);
    RUN_TEST(test_a_reset_request_is_software_only_when_the_firmware_asked);
    RUN_TEST(test_a_bit_with_no_value_is_unknown);
    RUN_TEST(test_a_bit_the_mdk_does_not_define_is_unknown);
    RUN_TEST(test_a_watchdog_with_any_other_bit_is_unknown);
    RUN_TEST(test_two_causes_with_values_are_unknown);
    RUN_TEST(test_a_take_decodes_then_clears_every_bit);
    RUN_TEST(test_a_later_reset_is_not_reported_as_an_earlier_one);
    return UNITY_END();
}
