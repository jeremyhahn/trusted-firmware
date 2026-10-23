/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for tfwsim's software tier (tools/tfwsim/tier_soft.c, #163).
 * The tier has no options and nothing to seed: the software provider holds no
 * key and the device it stands in for starts erased. Each test kills a fault
 * that would make `tfwsim` differ from that device or from its usage line:
 * an option consumed (main.c would skip the next argument, or -- answering -1
 * -- exit with nothing printed), an option advertised it does not take, and
 * any state written into the freshly reset mock before startup.
 */
#include <string.h>
#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "tier.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

/* Mutants: tfwsim_tier_option returning 1, 2 or -1. The element tier's own
 * options and tfwsim's shared ones are each unknown here, alone and with a
 * value after them, and the arguments are left as they were. */
static void test_no_option_is_consumed(void)
{
    static const char *const names[] = {
        "--provision-element", "--no-element", "--eeprom", "--eeprom-bin",
        "--image-hash", "--pty", "--eth", "-", "",
    };
    char  value[] = "00";
    char  name[32];
    char *argv[3];
    size_t i;

    for (i = 0u; i < sizeof names / sizeof names[0]; i++) {
        strcpy(name, names[i]);
        argv[0] = name;
        argv[1] = value;
        argv[2] = NULL;
        TEST_ASSERT_EQUAL_INT(0, tfwsim_tier_option(1, argv));
        TEST_ASSERT_EQUAL_INT(0, tfwsim_tier_option(2, argv));
        TEST_ASSERT_EQUAL_STRING(names[i], argv[0]);
        TEST_ASSERT_EQUAL_PTR(value, argv[1]);
        TEST_ASSERT_EQUAL_STRING("00", argv[1]);
    }
}

/* Mutant: a usage string naming other options (or NULL, which main.c's "%s"
 * would crash on). The usage line advertises exactly what the tier takes:
 * the soft root's UDS. */
static void test_the_usage_names_the_uds_option(void)
{
    const char *u = tfwsim_tier_usage();

    TEST_ASSERT_NOT_NULL(u);
    TEST_ASSERT_EQUAL_STRING(" [--uds HEX64]", u);
}

/* Mutants: a seed that provisions the EEPROM, lays a handoff block (which
 * would change the reset cause the firmware reads) or writes through the
 * HAL. The mock reads exactly as tfw_mock_reset left it. */
static void test_the_seed_leaves_the_reset_device_as_it_was(void)
{
    static uint8_t ee[0x10000];
    uint8_t  ho[TFW_BOOT_HANDOFF_LEN];
    size_t   ee_n = tfw_mock_eeprom_size();
    uint16_t lo = 0u, hi = 0u;

    TEST_ASSERT_TRUE(ee_n <= sizeof ee);
    memcpy(ee, tfw_mock_eeprom(), ee_n);
    memcpy(ho, tfw_mock_handoff(), sizeof ho);

    tfwsim_tier_seed();

    TEST_ASSERT_EQUAL_MEMORY(ee, tfw_mock_eeprom(), ee_n);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, tfw_mock_eeprom(), ee_n);
    TEST_ASSERT_EQUAL_MEMORY(ho, tfw_mock_handoff(), sizeof ho);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00u, tfw_mock_handoff(), sizeof ho);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_INT(TFW_RESET_POWER_ON, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_size_t(0u, tfw_mock_tx_pending());
}

/* A second seed is the first: the tier holds no state across calls. */
static void test_a_second_seed_is_the_first(void)
{
    tfwsim_tier_seed();
    tfwsim_tier_seed();
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, tfw_mock_eeprom(), tfw_mock_eeprom_size());
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00u, tfw_mock_handoff(), TFW_BOOT_HANDOFF_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_option_is_consumed);
    RUN_TEST(test_the_usage_names_the_uds_option);
    RUN_TEST(test_the_seed_leaves_the_reset_device_as_it_was);
    RUN_TEST(test_a_second_seed_is_the_first);
    return UNITY_END();
}
