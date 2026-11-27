/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H573I-DK's pin table, checked on x86.
 *
 * Pins 0-3 are the kit's four user LEDs, at the banks and bits ST's BSP
 * names (stm32h573i_discovery.h): LD1 green PI9, LD2 orange PI8, LD3 red
 * PF1, LD4 blue PF4. Pins 4 and 5 are the Arduino header's D6 (PH10) and D5
 * (PH11), TIM5's two PWM channels. Pins 6-11 are its A0 to A5 (Zephyr's
 * arduino_r3_connector.dtsi for the board) at their ADC1 inputs (hal_stm32's
 * stm32h573iikxq-pinctrl.dtsi): A0 PB0 INP9, A1 PA4 INP18, A2 PA0 INP0,
 * A3 PA5 INP19, A4 PA6 INP3, A5 PF12 INP6. Pin 12 is D3 (PB5), the tone's
 * TIM3_CH2. Port 0 is GPIOF (bank 5), 1 GPIOI (8), 2 GPIOH (7), 3 GPIOA (0),
 * 4 GPIOB (1). No pin offers a compare unit.
 *
 * It also checks the flash map: tfw-boot, the application, the platform block
 * and the user EEPROM sit on whole 8 KiB sectors inside the part, clear of
 * each other, the platform block in bank 1's last sector.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_its_registered_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x43u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("stm32h573i-dk", tfw_board_name());
}

static void test_the_digital_pins_are_the_leds_d6_d5_and_d3_at_their_banks(void)
{
    static const struct { uint8_t pin; uint8_t bank; uint8_t bit; uint8_t led; } want[7] = {
        {  0u, 8u, 9u, 1u },    /* LD1 PI9 */
        {  1u, 8u, 8u, 1u },    /* LD2 PI8 */
        {  2u, 5u, 1u, 1u },    /* LD3 PF1 */
        {  3u, 5u, 4u, 1u },    /* LD4 PF4 */
        {  4u, 7u, 10u, 0u },   /* ARD_D6 PH10 */
        {  5u, 7u, 11u, 0u },   /* ARD_D5 PH11 */
        { 12u, 1u, 5u, 0u },    /* ARD_D3 PB5, the tone's TIM3_CH2 */
    };

    TEST_ASSERT_EQUAL_UINT8(13u, TFW_PIN_COUNT);
    for (uint8_t w = 0u; w < 7u; w++) {
        uint8_t      i = want[w].pin;
        tfw_pinref_t r;
        uint8_t      ch;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(want[w].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[w].bit, r.bit);
        TEST_ASSERT_EQUAL(want[w].led != 0u, (tfw_board_pin_flags(i) & TFW_PIN_LED) != 0u);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
    }
    TEST_ASSERT_EQUAL_UINT8(12u, TFW_TONE_PIN);
}

static void test_a0_to_a5_are_their_adc1_inputs(void)
{
    static const struct { uint8_t bank; uint8_t bit; uint8_t inp; } want[6] = {
        { 1u,  0u,  9u },   /* A0 PB0  INP9  */
        { 0u,  4u, 18u },   /* A1 PA4  INP18 */
        { 0u,  0u,  0u },   /* A2 PA0  INP0  */
        { 0u,  5u, 19u },   /* A3 PA5  INP19 */
        { 0u,  6u,  3u },   /* A4 PA6  INP3  */
        { 5u, 12u,  6u },   /* A5 PF12 INP6  */
    };

    TEST_ASSERT_EQUAL_UINT8(20u, TFW_ADC_COUNT);
    for (uint8_t i = 0u; i < 6u; i++) {
        uint8_t      pin = (uint8_t)(6u + i);
        tfw_pinref_t r;
        uint8_t      ch = 0xEEu;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(pin, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(pin, &ch));
        TEST_ASSERT_EQUAL_UINT8(want[i].inp, ch);
        TEST_ASSERT_TRUE(ch < TFW_ADC_COUNT);
        TEST_ASSERT_EQUAL_HEX8(TFW_PIN_ADC, tfw_board_pin_flags(pin));
    }
}

static void test_only_d6_and_d5_offer_a_pwm(void)
{
    /* The rows' TIM5 fields are the target's tfw_pwmref_t, which the host's
     * (test/mock/hal_types.h) does not share: here, that D6 and D5 have a
     * row and nothing else does. */
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        if (i != 4u && i != 5u) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(i, &p));
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
    }
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

/* Literals, not soc_regs.h: its `#include "cm_regs.h"` resolves only with
 * -Isrc/hal/cortex-m, which board-sweep's include path does not carry. The
 * flash base, the 8 KiB sector (soc_regs.h TFW_H5_FLASH_SECTOR) and the start
 * of bank 2 (base + TFW_H5_FLASH_BANK_SIZE, soc_regs.h lines 93-94), RM0481. */
#define H5_FLASH_BASE 0x08000000u
#define H5_SECTOR     0x2000u
#define H5_BANK2      0x08100000u

static void test_the_flash_map_is_the_pinned_one(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x08000000u, (uint32_t)TFW_BOOT_REGION_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00008000u, (uint32_t)TFW_BOOT_REGION_SIZE);
    TEST_ASSERT_EQUAL_HEX32(0x08008000u, (uint32_t)TFW_APP_REGION_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00020000u, (uint32_t)TFW_APP_REGION_SIZE);
    TEST_ASSERT_EQUAL_HEX32(0x080FE000u, (uint32_t)TFW_CM_PLATFORM_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00002000u, (uint32_t)TFW_CM_PLATFORM_SIZE);
    TEST_ASSERT_EQUAL_HEX32(0x081FE000u, (uint32_t)TFW_CM_USER_EE_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00002000u, (uint32_t)TFW_CM_USER_EE_SIZE);
    TEST_ASSERT_EQUAL_HEX32(0x08200000u, (uint32_t)TFW_FLASH_TOTAL);
}

static void test_each_region_is_whole_sectors_inside_the_part_and_clear_of_the_others(void)
{
    static const struct { const char *name; uint32_t base; uint32_t len; } r[4] = {
        { "boot",        (uint32_t)TFW_BOOT_REGION_BASE, (uint32_t)TFW_BOOT_REGION_SIZE },
        { "app",         (uint32_t)TFW_APP_REGION_BASE,  (uint32_t)TFW_APP_REGION_SIZE },
        { "platform",    (uint32_t)TFW_CM_PLATFORM_BASE, (uint32_t)TFW_CM_PLATFORM_SIZE },
        { "user EEPROM", (uint32_t)TFW_CM_USER_EE_BASE,  (uint32_t)TFW_CM_USER_EE_SIZE },
    };
    static const char *const pair[4][4] = {
        { 0, "boot overlaps app", "boot overlaps platform", "boot overlaps user EEPROM" },
        { 0, 0, "app overlaps platform", "app overlaps user EEPROM" },
        { 0, 0, 0, "platform overlaps user EEPROM" },
        { 0, 0, 0, 0 },
    };
    const uint32_t total = (uint32_t)TFW_FLASH_TOTAL;

    for (uint32_t i = 0u; i < 4u; i++) {
        TEST_ASSERT_TRUE_MESSAGE(r[i].len > 0u, r[i].name);
        TEST_ASSERT_TRUE_MESSAGE(r[i].base >= H5_FLASH_BASE, r[i].name);
        TEST_ASSERT_TRUE_MESSAGE(r[i].len <= total && r[i].base <= total - r[i].len, r[i].name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, r[i].base % H5_SECTOR, r[i].name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, r[i].len % H5_SECTOR, r[i].name);
        for (uint32_t j = i + 1u; j < 4u; j++) {
            TEST_ASSERT_TRUE_MESSAGE(r[i].base + r[i].len <= r[j].base ||
                                     r[j].base + r[j].len <= r[i].base, pair[i][j]);
        }
    }
    /* The platform block is bank 1's last sector; the user EEPROM is bank 2's. */
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(H5_BANK2, r[2].base + r[2].len, r[2].name);
    TEST_ASSERT_TRUE_MESSAGE(r[3].base >= H5_BANK2, r[3].name);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(total, r[3].base + r[3].len, r[3].name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_its_registered_id);
    RUN_TEST(test_the_digital_pins_are_the_leds_d6_d5_and_d3_at_their_banks);
    RUN_TEST(test_a0_to_a5_are_their_adc1_inputs);
    RUN_TEST(test_only_d6_and_d5_offer_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_flash_map_is_the_pinned_one);
    RUN_TEST(test_each_region_is_whole_sectors_inside_the_part_and_clear_of_the_others);
    return UNITY_END();
}
