/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response engine's port on mps2-an505 (src/hal/cortex-m/mps2/
 * hsm_port.c, row cortex-m, #628), unchanged, on the host. The mps2 boot test
 * directory's cm_regs.h sends every TFW_CM_REG to tfw_cm_model_reg, which this
 * suite defines as a register file of two words, the FPGA I/O LED word
 * (+0x00) and its button word (+0x08); any other address fails the test.
 *
 *   act      A2 and A5 light LED 0, A4 lights LED 1, each keeping what is
 *            already lit; A1, A3 and A6 are done and touch no register.
 *   sense    button 0 is S1 (tamper pin), button 1 is S5 (hot-plug), the
 *            word's other bits are not signals.
 *   key      the test envelopes' tenant key, SUIT_TENANT_PUB, byte for byte.
 *   measure  unsupported, touching no register.
 *   unknown  an action outside A1-A6 (0, 7, 0xFF) touches no register.
 *   reads    sense reads the button word once; each lit action does one
 *            read-modify-write of the LED word.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "core/hsm.h"
#include "core/hsm_port.h"
#include "soc_regs.h"
#include "suit_vectors.h"

static volatile uint32_t led;
static volatile uint32_t button;
static unsigned accesses;

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    accesses++;
    if (addr == TFW_MPS2_FPGAIO_BASE + 0x00u) {
        return &led;
    }
    if (addr == TFW_MPS2_FPGAIO_BASE + 0x08u) {
        return &button;
    }
    TEST_FAIL_MESSAGE("hsm_port.c touched a register outside the FPGA I/O LED and button words");
    return &led;
}

static uint8_t sig_bit(uint8_t sig)
{
    return (uint8_t)(1u << (sig - 1u));
}

void setUp(void)
{
    led      = 0u;
    button   = 0u;
    accesses = 0u;
}

void tearDown(void) {}

static void test_power_off_and_shutdown_light_led_0_and_keep_what_is_lit(void)
{
    static const uint8_t down[] = { TFW_HSM_ACT_POWER_OFF, TFW_HSM_ACT_SHUTDOWN };

    for (unsigned i = 0u; i < sizeof down; i++) {
        led = 0u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(down[i]));
        TEST_ASSERT_EQUAL_HEX32(0x1u, led);

        led = 0xF0u | 0x2u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(down[i]));
        TEST_ASSERT_EQUAL_HEX32(0xF0u | 0x2u | 0x1u, led);
    }
}

static void test_keys_wipe_lights_led_1_and_keeps_led_0(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(TFW_HSM_ACT_KEYS_WIPE));
    TEST_ASSERT_EQUAL_HEX32(0x2u, led);

    led = 0x1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(TFW_HSM_ACT_KEYS_WIPE));
    TEST_ASSERT_EQUAL_HEX32(0x3u, led);
}

static void test_phy_off_keys_lock_and_record_are_done_and_touch_nothing(void)
{
    static const uint8_t none[] = { TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_RECORD };

    for (unsigned i = 0u; i < sizeof none; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(none[i]));
    }
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
    TEST_ASSERT_EQUAL_HEX32(0u, led);
}

static void test_button_0_is_the_tamper_pin_and_button_1_hotplug(void)
{
    const uint8_t pin = sig_bit(TFW_HSM_SIG_TAMPER_PIN);
    const uint8_t hot = sig_bit(TFW_HSM_SIG_HOTPLUG);

    button = 0x0u;
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_hsm_port_sense());
    button = 0x1u;
    TEST_ASSERT_EQUAL_HEX8(pin, tfw_hsm_port_sense());
    button = 0x2u;
    TEST_ASSERT_EQUAL_HEX8(hot, tfw_hsm_port_sense());
    button = 0x3u;
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(pin | hot), tfw_hsm_port_sense());
    button = 0xFFFFFFFCu;
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_hsm_port_sense());
    button = 0xFFFFFFFDu;
    TEST_ASSERT_EQUAL_HEX8(pin, tfw_hsm_port_sense());
    TEST_ASSERT_EQUAL_HEX32(0u, led);
}

static void test_the_key_is_the_test_envelopes_tenant_key(void)
{
    const uint8_t *k = tfw_hsm_port_key();

    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_EQUAL_UINT(64u, sizeof SUIT_TENANT_PUB);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SUIT_TENANT_PUB, k, sizeof SUIT_TENANT_PUB);
    TEST_ASSERT_EQUAL_PTR(k, tfw_hsm_port_key());
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
}

static void test_measure_is_unsupported_and_touches_nothing(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hsm_port_measure(&set));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hsm_port_measure(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
}

static void test_an_action_outside_a1_to_a6_touches_no_register(void)
{
    static const uint8_t outside[] = { 0u, 7u, 0xFFu };

    for (unsigned i = 0u; i < sizeof outside; i++) {
        /* core/hsm_port.h specifies act for A1-A6 only: the return is not pinned. */
        (void)tfw_hsm_port_act(outside[i]);
    }
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
    TEST_ASSERT_EQUAL_HEX32(0u, led);
}

static void test_sense_and_each_lit_action_touch_one_register_once(void)
{
    static const uint8_t lit[] = {
        TFW_HSM_ACT_POWER_OFF, TFW_HSM_ACT_KEYS_WIPE, TFW_HSM_ACT_SHUTDOWN
    };

    button = 0x3u;
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(sig_bit(TFW_HSM_SIG_TAMPER_PIN) | sig_bit(TFW_HSM_SIG_HOTPLUG)),
                           tfw_hsm_port_sense());
    TEST_ASSERT_EQUAL_UINT(1u, accesses);

    for (unsigned i = 0u; i < sizeof lit; i++) {
        accesses = 0u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_port_act(lit[i]));
        TEST_ASSERT_EQUAL_UINT(1u, accesses);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_off_and_shutdown_light_led_0_and_keep_what_is_lit);
    RUN_TEST(test_keys_wipe_lights_led_1_and_keeps_led_0);
    RUN_TEST(test_phy_off_keys_lock_and_record_are_done_and_touch_nothing);
    RUN_TEST(test_button_0_is_the_tamper_pin_and_button_1_hotplug);
    RUN_TEST(test_the_key_is_the_test_envelopes_tenant_key);
    RUN_TEST(test_measure_is_unsupported_and_touches_nothing);
    RUN_TEST(test_an_action_outside_a1_to_a6_touches_no_register);
    RUN_TEST(test_sense_and_each_lit_action_touch_one_register_once);
    return UNITY_END();
}
