/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 UDS provisioning unit,
 * src/hal/esp32/prov_uds.c.
 *
 * The unit had no host build. It is built here over stand-ins for ESP-IDF's
 * eFuse key API, HMAC peripheral, RNG, entropy source, ADC lock and log
 * (test/mock/esp32/prov/), which this file implements, each call recorded in
 * one trace so the order is pinned as well as the arguments. It holds:
 *
 *   - present: the UDS is the HMAC_UP key block, present only when the part
 *     names one;
 *   - random: the entropy source only under ADC unit 1's lock, switched on
 *     before the 32 bytes are drawn and off after, the lock released; a held
 *     lock refused busy with nothing drawn;
 *   - burn: the first unused key block written once as HMAC_UP with the 32
 *     bytes, then confirmed as the block layer 0 finds and read- and
 *     write-protected; no free block refused with no slot and nothing
 *     written; a failed write, and each failed confirmation, refused as a
 *     crypto error; each outcome logged;
 *   - hmac: the HMAC peripheral keyed by the UDS block less KEY0, over the
 *     caller's message into the caller's buffer; no UDS, or a refusal, a
 *     crypto error.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "caps/provision_uds.h"

/* ------------------------------------------------------------------- trace */

/* One letter per call into the part, in order:
 *   A lock acquire  R lock release  E entropy on  D entropy off  F RNG draw
 *   U unused-block search  W key write  P purpose search
 *   r read-protect query  w write-protect query  H HMAC */
static char     trace[32];
static unsigned ntrace;

static void step(char c)
{
    if (ntrace < sizeof trace - 1u) {
        trace[ntrace++] = c;
        trace[ntrace]   = '\0';
    }
}

/* -------------------------------------------------------------------- log */

static char log_level;
static char log_tag[16];
static char log_msg[128];

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;

    log_level = level;
    (void)snprintf(log_tag, sizeof log_tag, "%s", tag);
    va_start(ap, fmt);
    (void)vsnprintf(log_msg, sizeof log_msg, fmt, ap);
    va_end(ap);
}

const char *esp_err_to_name(esp_err_t code)
{
    return code == ESP_ERR_INVALID_STATE ? "ESP_ERR_INVALID_STATE" : "UNKNOWN ERROR";
}

/* ---------------------------------------------------------------- ADC, RNG */

static esp_err_t  lock_err;
static adc_unit_t lock_unit;
static adc_unit_t release_unit;
static int        entropy_on;
static int        drawn_with_entropy;
static size_t     rand_len;

esp_err_t adc_lock_try_acquire(adc_unit_t adc_unit)
{
    step('A');
    lock_unit = adc_unit;
    return lock_err;
}

esp_err_t adc_lock_release(adc_unit_t adc_unit)
{
    step('R');
    release_unit = adc_unit;
    return ESP_OK;
}

void bootloader_random_enable(void)
{
    step('E');
    entropy_on = 1;
}

void bootloader_random_disable(void)
{
    step('D');
    entropy_on = 0;
}

void esp_fill_random(void *buf, size_t len)
{
    uint8_t *b = buf;
    size_t   i;

    step('F');
    drawn_with_entropy = entropy_on;
    rand_len           = len;
    for (i = 0u; i < len; i++) {
        b[i] = (uint8_t)(0xA5u ^ i);
    }
}

/* ------------------------------------------------------------------ eFuse */

static int               find_ok;
static esp_efuse_block_t found_block;
static esp_efuse_purpose_t find_purpose;
static esp_efuse_block_t unused_block;
static esp_err_t         write_err;
static esp_efuse_block_t write_block;
static esp_efuse_purpose_t write_purpose;
static uint8_t           write_key[40];
static size_t            write_len;
static bool              dis_read;
static bool              dis_write;
static esp_efuse_block_t dis_read_block;
static esp_efuse_block_t dis_write_block;

bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block)
{
    step('P');
    find_purpose = purpose;
    if (!find_ok) {
        return false;
    }
    *block = found_block;
    return true;
}

esp_efuse_block_t esp_efuse_find_unused_key_block(void)
{
    step('U');
    return unused_block;
}

esp_err_t esp_efuse_write_key(esp_efuse_block_t block, esp_efuse_purpose_t purpose,
                              const void *key, size_t key_size_bytes)
{
    step('W');
    write_block   = block;
    write_purpose = purpose;
    write_len     = key_size_bytes;
    memcpy(write_key, key, key_size_bytes < sizeof write_key ? key_size_bytes : sizeof write_key);
    return write_err;
}

bool esp_efuse_get_key_dis_read(esp_efuse_block_t block)
{
    step('r');
    dis_read_block = block;
    return dis_read;
}

bool esp_efuse_get_key_dis_write(esp_efuse_block_t block)
{
    step('w');
    dis_write_block = block;
    return dis_write;
}

/* ------------------------------------------------------------------- HMAC */

static esp_err_t     hmac_err;
static hmac_key_id_t hmac_key;
static const void   *hmac_msg;
static size_t        hmac_len;
static uint8_t      *hmac_out;

esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message, size_t message_len,
                             uint8_t *hmac)
{
    unsigned i;

    step('H');
    hmac_key = key_id;
    hmac_msg = message;
    hmac_len = message_len;
    hmac_out = hmac;
    if (hmac_err != ESP_OK) {
        return hmac_err;
    }
    for (i = 0u; i < 32u; i++) {
        hmac[i] = (uint8_t)(0x30u + (unsigned)key_id + i);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ setup */

void setUp(void)
{
    memset(trace, 0, sizeof trace);
    ntrace = 0u;
    log_level = '\0';
    memset(log_tag, 0, sizeof log_tag);
    memset(log_msg, 0, sizeof log_msg);
    lock_err           = ESP_OK;
    lock_unit          = ADC_UNIT_2;
    release_unit       = ADC_UNIT_2;
    entropy_on         = 0;
    drawn_with_entropy = 0;
    rand_len           = 0u;
    find_ok            = 1;
    found_block        = EFUSE_BLK_KEY3;
    find_purpose       = ESP_EFUSE_KEY_PURPOSE_USER;
    unused_block       = EFUSE_BLK_KEY3;
    write_err          = ESP_OK;
    write_block        = EFUSE_BLK0;
    write_purpose      = ESP_EFUSE_KEY_PURPOSE_USER;
    memset(write_key, 0, sizeof write_key);
    write_len       = 0u;
    dis_read        = true;
    dis_write       = true;
    dis_read_block  = EFUSE_BLK0;
    dis_write_block = EFUSE_BLK0;
    hmac_err        = ESP_OK;
    hmac_key        = HMAC_KEY_MAX;
    hmac_msg        = NULL;
    hmac_len        = 0u;
    hmac_out        = NULL;
}

void tearDown(void) {}

static const uint8_t UDS[32] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78, 0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0,
};

/* ---------------------------------------------------------------- present */

static void test_present_when_the_part_names_an_hmac_up_block(void)
{
    TEST_ASSERT_EQUAL_INT(1, tfw_prov_uds_present());
    TEST_ASSERT_EQUAL_STRING("P", trace);
    TEST_ASSERT_EQUAL_INT(8, find_purpose);
}

static void test_absent_when_the_part_names_none(void)
{
    find_ok = 0;
    TEST_ASSERT_EQUAL_INT(0, tfw_prov_uds_present());
    TEST_ASSERT_EQUAL_STRING("P", trace);
    TEST_ASSERT_EQUAL_INT(8, find_purpose);
}

/* ----------------------------------------------------------------- random */

static void test_random_draws_32_bytes_under_the_lock_with_entropy_on(void)
{
    static const uint8_t want[32] = {
        0xA5, 0xA4, 0xA7, 0xA6, 0xA1, 0xA0, 0xA3, 0xA2, 0xAD, 0xAC, 0xAF, 0xAE, 0xA9, 0xA8, 0xAB, 0xAA,
        0xB5, 0xB4, 0xB7, 0xB6, 0xB1, 0xB0, 0xB3, 0xB2, 0xBD, 0xBC, 0xBF, 0xBE, 0xB9, 0xB8, 0xBB, 0xBA,
    };
    uint8_t uds[33];

    memset(uds, 0x11, sizeof uds);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_random(uds));
    TEST_ASSERT_EQUAL_STRING("AEFDR", trace);
    TEST_ASSERT_EQUAL_INT(0, lock_unit);
    TEST_ASSERT_EQUAL_INT(0, release_unit);
    TEST_ASSERT_EQUAL_INT(1, drawn_with_entropy);
    TEST_ASSERT_EQUAL_INT(0, entropy_on);
    TEST_ASSERT_EQUAL_size_t(32u, rand_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, uds, 32);
    TEST_ASSERT_EQUAL_HEX8(0x11, uds[32]);
}

static void test_random_refuses_busy_while_the_adc_is_held(void)
{
    uint8_t uds[32];
    uint8_t was[32];

    memset(uds, 0x11, sizeof uds);
    memcpy(was, uds, sizeof was);
    lock_err = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(8, tfw_prov_uds_random(uds));
    TEST_ASSERT_EQUAL_STRING("A", trace);
    TEST_ASSERT_EQUAL_INT(0, lock_unit);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(was, uds, 32);
}

static void test_random_refuses_on_any_lock_error(void)
{
    uint8_t uds[32];

    lock_err = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_prov_uds_random(uds));
    TEST_ASSERT_EQUAL_STRING("A", trace);
}

/* ------------------------------------------------------------------- burn */

static void test_burn_writes_the_first_free_block_and_confirms_it(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UWPrw", trace);
    TEST_ASSERT_EQUAL_INT(7, write_block);
    TEST_ASSERT_EQUAL_INT(8, write_purpose);
    TEST_ASSERT_EQUAL_size_t(32u, write_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(UDS, write_key, 32);
    TEST_ASSERT_EQUAL_INT(8, find_purpose);
    TEST_ASSERT_EQUAL_INT(7, dis_read_block);
    TEST_ASSERT_EQUAL_INT(7, dis_write_block);
    TEST_ASSERT_EQUAL_CHAR('I', log_level);
    TEST_ASSERT_EQUAL_STRING("tfw-prov", log_tag);
    TEST_ASSERT_EQUAL_STRING("prov: UDS burned into eFuse BLOCK7, read- and write-protected", log_msg);
}

static void test_burn_takes_whichever_block_is_free(void)
{
    unused_block = EFUSE_BLK_KEY0;
    found_block  = EFUSE_BLK_KEY0;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_INT(4, write_block);
    TEST_ASSERT_EQUAL_INT(4, dis_read_block);
    TEST_ASSERT_EQUAL_INT(4, dis_write_block);
    TEST_ASSERT_EQUAL_STRING("prov: UDS burned into eFuse BLOCK4, read- and write-protected", log_msg);
}

static void test_burn_refuses_no_slot_when_no_block_is_free(void)
{
    unused_block = EFUSE_BLK_KEY_MAX;
    TEST_ASSERT_EQUAL_INT(12, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("U", trace);
    TEST_ASSERT_EQUAL_size_t(0u, write_len);
    TEST_ASSERT_EQUAL_CHAR('E', log_level);
    TEST_ASSERT_EQUAL_STRING("prov: no unused eFuse key block for the UDS", log_msg);
}

static void test_burn_refuses_a_failed_write(void)
{
    write_err = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL_INT(14, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UW", trace);
    TEST_ASSERT_EQUAL_CHAR('E', log_level);
    TEST_ASSERT_EQUAL_STRING("prov: eFuse BLOCK7 burn failed: ESP_ERR_INVALID_STATE", log_msg);
}

static void test_burn_refuses_when_no_hmac_up_block_is_found_after(void)
{
    find_ok = 0;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UWP", trace);
    TEST_ASSERT_EQUAL_CHAR('E', log_level);
    TEST_ASSERT_EQUAL_STRING("prov: eFuse BLOCK7 is not a protected HMAC_UP key after the burn", log_msg);
}

static void test_burn_refuses_when_layer_0_would_find_another_block(void)
{
    found_block = EFUSE_BLK_KEY2;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UWP", trace);
    TEST_ASSERT_EQUAL_STRING("prov: eFuse BLOCK7 is not a protected HMAC_UP key after the burn", log_msg);
}

static void test_burn_refuses_a_readable_key(void)
{
    dis_read = false;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UWPr", trace);
    TEST_ASSERT_EQUAL_CHAR('E', log_level);
    TEST_ASSERT_EQUAL_STRING("prov: eFuse BLOCK7 is not a protected HMAC_UP key after the burn", log_msg);
}

static void test_burn_refuses_a_rewritable_key(void)
{
    dis_write = false;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_burn(UDS));
    TEST_ASSERT_EQUAL_STRING("UWPrw", trace);
    TEST_ASSERT_EQUAL_CHAR('E', log_level);
    TEST_ASSERT_EQUAL_STRING("prov: eFuse BLOCK7 is not a protected HMAC_UP key after the burn", log_msg);
}

/* ------------------------------------------------------------------- hmac */

static void test_hmac_keys_the_peripheral_by_the_uds_block(void)
{
    static const uint8_t want[32] = {
        0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
        0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52,
    };
    static const uint8_t msg[] = "tfw-deviceid-v1";
    uint8_t              out[32];

    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_hmac(msg, 15u, out));
    TEST_ASSERT_EQUAL_STRING("PH", trace);
    TEST_ASSERT_EQUAL_INT(8, find_purpose);
    TEST_ASSERT_EQUAL_INT(3, hmac_key);
    TEST_ASSERT_EQUAL_PTR(msg, hmac_msg);
    TEST_ASSERT_EQUAL_size_t(15u, hmac_len);
    TEST_ASSERT_EQUAL_PTR(out, hmac_out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

static void test_hmac_key_id_is_the_block_less_key0(void)
{
    static const uint8_t msg[1] = {0x5Au};
    uint8_t              out[32];

    found_block = EFUSE_BLK_KEY0;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_hmac(msg, 1u, out));
    TEST_ASSERT_EQUAL_INT(0, hmac_key);
    found_block = EFUSE_BLK_KEY5;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_prov_uds_hmac(msg, 1u, out));
    TEST_ASSERT_EQUAL_INT(5, hmac_key);
}

static void test_hmac_refuses_without_a_uds(void)
{
    uint8_t out[32];

    find_ok = 0;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_hmac(UDS, 32u, out));
    TEST_ASSERT_EQUAL_STRING("P", trace);
}

static void test_hmac_refuses_what_the_peripheral_refuses(void)
{
    uint8_t out[32];

    hmac_err = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_prov_uds_hmac(UDS, 32u, out));
    TEST_ASSERT_EQUAL_STRING("PH", trace);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_present_when_the_part_names_an_hmac_up_block);
    RUN_TEST(test_absent_when_the_part_names_none);
    RUN_TEST(test_random_draws_32_bytes_under_the_lock_with_entropy_on);
    RUN_TEST(test_random_refuses_busy_while_the_adc_is_held);
    RUN_TEST(test_random_refuses_on_any_lock_error);
    RUN_TEST(test_burn_writes_the_first_free_block_and_confirms_it);
    RUN_TEST(test_burn_takes_whichever_block_is_free);
    RUN_TEST(test_burn_refuses_no_slot_when_no_block_is_free);
    RUN_TEST(test_burn_refuses_a_failed_write);
    RUN_TEST(test_burn_refuses_when_no_hmac_up_block_is_found_after);
    RUN_TEST(test_burn_refuses_when_layer_0_would_find_another_block);
    RUN_TEST(test_burn_refuses_a_readable_key);
    RUN_TEST(test_burn_refuses_a_rewritable_key);
    RUN_TEST(test_hmac_keys_the_peripheral_by_the_uds_block);
    RUN_TEST(test_hmac_key_id_is_the_block_less_key0);
    RUN_TEST(test_hmac_refuses_without_a_uds);
    RUN_TEST(test_hmac_refuses_what_the_peripheral_refuses);
    return UNITY_END();
}
