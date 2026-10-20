/*
 * tfw_boot_measure on the ESP32 measure (boot/hal/esp32/measure_esp32.c,
 * #163): the application region read through the boot HAL's flash, not a
 * caller's reader. Same link as test_boot_measure_esp32.
 *
 * The oracle is sha256_small over the mock flash at TFW_BOOT_APP_BASE: every
 * whole-word length hashes exactly the region's first len bytes, a byte either
 * side of the span does not move it, and a zero length or one that is not
 * whole words is refused.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_measure.h"
#include "sha256.h"
#include "esp_sha_mock.h"

#define SPAN 512u

static const int classic = CONFIG_IDF_TARGET_ESP32;

static uint8_t *app(void)
{
    return tfw_boot_mock_flash() + TFW_BOOT_APP_BASE;
}

void setUp(void)
{
    uint32_t i;

    tfw_boot_mock_reset();
    TEST_ASSERT_TRUE(TFW_BOOT_APP_BASE + SPAN + 1u <= tfw_boot_mock_flash_size());
    for (i = 0u; i < SPAN + 1u; i++) {
        app()[i] = (uint8_t)(i * 29u + 7u);
    }
    tfw_esp_sha_mock_reset(classic);
}
void tearDown(void) { }

static void test_every_word_length_hashes_the_app_region(void)
{
    uint8_t  want[32], have[32];
    uint32_t len;

    for (len = 4u; len <= SPAN; len += 4u) {
        tfw_sha256(app(), (uint16_t)len, want);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(len, have));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, have, sizeof want);
    }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

static void test_bytes_outside_the_span_do_not_move_it(void)
{
    uint8_t before[32], after[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(SPAN, before));
    app()[SPAN] ^= 0xFFu;
    if (TFW_BOOT_APP_BASE > 0u) {
        app()[-1] ^= 0xFFu;
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(SPAN, after));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, after, sizeof before);
}

static void test_every_byte_inside_the_span_moves_it(void)
{
    uint8_t  base[32], have[32];
    uint32_t i;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(64u, base));
    for (i = 0u; i < 64u; i++) {
        app()[i] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(64u, have));
        TEST_ASSERT_FALSE(memcmp(base, have, sizeof have) == 0);
        app()[i] ^= 0x01u;
    }
}

static void test_a_length_not_whole_words_is_refused(void)
{
    uint8_t  have[32];
    uint32_t len;

    for (len = 0u; len < 64u; len++) {
        if (len == 0u || len % 4u != 0u) {
            TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure(len, have));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_word_length_hashes_the_app_region);
    RUN_TEST(test_bytes_outside_the_span_do_not_move_it);
    RUN_TEST(test_every_byte_inside_the_span_moves_it);
    RUN_TEST(test_a_length_not_whole_words_is_refused);
    return UNITY_END();
}
