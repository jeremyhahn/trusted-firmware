/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mcxn94x SoC's reset cause (src/hal/cortex-m/mcxn94x/soc_reset.c), on
 * x86, against a fake CMC0 SRS word.
 *
 * Every bit is a literal copied from the vendor's header (the mcux-sdk's
 * MCXN947_cm33_core0.h, CMC_SRS_*_MASK), not taken from the code under test.
 */

#include "unity.h"

#include "soc_reset.h"

void setUp(void) { }
void tearDown(void) { }

#define WAKEUP 0x00000001u
#define POR    0x00000002u
#define VD     0x00000004u
#define WARM   0x00000010u
#define FATAL  0x00000020u
#define PIN    0x00000100u
#define DAP    0x00000200u
#define RSTACK 0x00000400u
#define LPACK  0x00000800u
#define SCG    0x00001000u
#define WWDT0  0x00002000u
#define SW     0x00004000u
#define LOCKUP 0x00008000u
#define CPU1   0x00010000u
#define VBAT   0x01000000u
#define WWDT1  0x02000000u
#define CDOG0  0x04000000u
#define CDOG1  0x08000000u
#define JTAG   0x10000000u
#define SECVIO 0x40000000u
#define TAMPER 0x80000000u

#define ASKED     1u
#define NOT_ASKED 0u

/* The register's word rules: the marker changes nothing but SW. */
static void assert_either_way(tfw_reset_cause_t want, uint32_t srs, const char *what)
{
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_mcxn_reset_decode(srs, NOT_ASKED), what);
    TEST_ASSERT_EQUAL_MESSAGE(want, tfw_mcxn_reset_decode(srs, ASKED), what);
}

static void test_each_source_with_a_value_maps_to_it(void)
{
    assert_either_way(TFW_RESET_POWER_ON, POR, "POR");
    assert_either_way(TFW_RESET_BROWNOUT, VD, "VD");
    assert_either_way(TFW_RESET_EXTERNAL, PIN, "PIN");
    assert_either_way(TFW_RESET_WATCHDOG, WWDT0, "WWDT0");
}

/* WARM and FATAL are the reset's class beside its source. */
static void test_the_class_flags_change_nothing(void)
{
    assert_either_way(TFW_RESET_POWER_ON, POR | FATAL, "POR, FATAL");
    assert_either_way(TFW_RESET_EXTERNAL, PIN | WARM, "PIN, WARM");
    assert_either_way(TFW_RESET_WATCHDOG, WWDT0 | WARM, "WWDT0, WARM");
    assert_either_way(TFW_RESET_WATCHDOG, WWDT0 | WARM | FATAL, "WWDT0, WARM, FATAL");
    assert_either_way(TFW_RESET_UNKNOWN, WARM, "WARM alone");
    assert_either_way(TFW_RESET_UNKNOWN, FATAL, "FATAL alone");
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_mcxn_reset_decode(SW | WARM, ASKED));
}

/* SW is the CPU's reset request, which a debugger can make too: it is the
 * firmware's own restart only when the firmware left its marker. */
static void test_a_reset_request_is_software_only_when_the_firmware_asked(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_mcxn_reset_decode(SW, ASKED));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_mcxn_reset_decode(SW, NOT_ASKED));
}

static void test_nothing_latched_is_unknown(void)
{
    assert_either_way(TFW_RESET_UNKNOWN, 0u, "nothing latched");
}

static void test_a_source_with_no_value_is_unknown(void)
{
    static const uint32_t none[] = {
        WAKEUP, DAP, RSTACK, LPACK, SCG, LOCKUP, CPU1, VBAT, WWDT1, CDOG0, CDOG1, JTAG,
        SECVIO, TAMPER, 1u << 3, 1u << 6, 1u << 7, 1u << 17, 1u << 23, 1u << 29,
    };

    for (uint32_t i = 0u; i < sizeof none / sizeof none[0]; i++) {
        assert_either_way(TFW_RESET_UNKNOWN, none[i], "a source with no value");
    }
}

/* TFW_RESET_WATCHDOG is what the update-request gate honours: WWDT0 beside
 * any source is not a watchdog reset. */
static void test_a_watchdog_with_any_other_source_is_unknown(void)
{
    for (uint32_t bit = 0u; bit < 32u; bit++) {
        uint32_t other = 1u << bit;

        if (other == WWDT0 || other == WARM || other == FATAL) {
            continue;
        }
        assert_either_way(TFW_RESET_UNKNOWN, WWDT0 | other, "WWDT0 with another source");
    }
}

/* SRS is read-only: a take reads it and writes nothing. */
static void test_a_take_reads_and_writes_nothing(void)
{
    const volatile uint32_t fake = WWDT0 | WARM;

    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_mcxn_reset_take(&fake, NOT_ASKED));
    TEST_ASSERT_EQUAL_HEX32(WWDT0 | WARM, fake);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_source_with_a_value_maps_to_it);
    RUN_TEST(test_the_class_flags_change_nothing);
    RUN_TEST(test_a_reset_request_is_software_only_when_the_firmware_asked);
    RUN_TEST(test_nothing_latched_is_unknown);
    RUN_TEST(test_a_source_with_no_value_is_unknown);
    RUN_TEST(test_a_watchdog_with_any_other_source_is_unknown);
    RUN_TEST(test_a_take_reads_and_writes_nothing);
    return UNITY_END();
}
