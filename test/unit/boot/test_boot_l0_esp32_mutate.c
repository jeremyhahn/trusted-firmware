/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for the ESP32 layer 0, boot/hal/esp32/boot_l0.c (#163).
 *
 * The unit is driven whole through call_start_cpu0, over ESP-IDF stand-ins in
 * boot_l0_esp32/ (this test's alone). This file is the rest of the unit's
 * world: the ESP-IDF calls it makes, the ES256 gate and the activate's
 * authorization (l0_gate.c stays out of the link), and the handoff build,
 * write and reset cause (boot_hal_mock.c stays out). bootloader_reset and the
 * loader do not return; they longjmp back here.
 *
 * Nothing is fused and no eFuse is modelled as burned: the secure version
 * read is always 0. Which key block holds the HMAC_UP purpose is a read the
 * test answers with a value it chose, and the HMAC peripheral is a recorded
 * call. A mutant that passes the gate a zero floor without the eFuse read is
 * killed by the read's record alone; one that keeps the read and passes some
 * other zero would need a burnt floor to tell apart.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "esp_efuse.h"
#include "esp_image_format.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "bootloader_init.h"
#include "bootloader_common.h"
#include "bootloader_utility.h"
#include "rom/hmac.h"
#include "tfw/boot.h"
#include "tfw/types.h"
#include "boot_measure.h"
#include "boot_hal.h"
#include "l0_gate.h"

void call_start_cpu0(void) __attribute__((__noreturn__));

#define RAN_RESET 1
#define RAN_LOAD  2

#define FACTORY_OFF 0x10000u
#define OTA0_OFF    0x110000u
#define OTA1_OFF    0x210000u
#define OTA2_OFF    0x310000u
#define TEST_OFF    0x410000u
#define SLOT_SIZE   0x100000u

/* One image per partition: what esp_image_verify reports and what the gate
 * says of its trailer. */
typedef struct {
    uint32_t     offset;
    esp_err_t    verify_rc;
    uint8_t      segment_count;
    uint32_t     seg_len[ESP_IMAGE_MAX_SEGMENTS];
    uint32_t     beyond;        /* the word after the segment headers */
    uint32_t     image_len;
    uint8_t      hash_appended;
    uint8_t      digest[32];
    uint32_t     secure_version;
    tfw_status_t gate_st;
    uint32_t     tr_version;
} img_t;

static jmp_buf            env;
static img_t              imgs[5];
static bootloader_state_t table;
static bool               table_ok;
static esp_err_t          init_rc;
static int                selected;
static char               seq[128];
static size_t             seq_n;
static char               logs[4096];
static size_t             logs_n;
static int                log_w;
static int                log_e;

static uint32_t verified[16];
static size_t   n_verified;
static int      verify_mode;

static uint32_t gate_base, gate_size, gate_len, gate_floor;
static uint8_t  gate_digest[32];

static tfw_status_t auth_read_st, authorized_st;
static int          n_auth_read, n_authorized;
static uint32_t     authorized_version;
static uint8_t      authorized_rec[TFW_BOOT_AUTH_LEN];

static esp_err_t              otadata_rc;
static esp_ota_select_entry_t otadata[2];
static int                    active;
static int                    n_otadata;

static bool              purpose_found;
static esp_efuse_block_t purpose_block;
static int               purpose_asked;
static int               hmac_rc;
static uint8_t           hmac_out[32];
static int               hmac_block;
static uint8_t           hmac_msg[32];
static size_t            hmac_len;

static uint8_t            built_tier, built_cdi[32], built_meas[32];
static uint32_t           built_version;
static tfw_reset_cause_t  built_cause;
static int                n_built;
static const uint8_t     *written;
static uint8_t            written_copy[TFW_BOOT_HANDOFF_LEN];
static uint8_t            written_len;

static rtc_retain_mem_t     mem;
static uint32_t             crc_ret;
static uint32_t             crc_init, crc_len;
static const uint8_t       *crc_buf;
static int                  n_mem_reset;
static esp_partition_pos_t  updated;
static bool                 updated_counter;

static const bootloader_state_t *loaded_bs;
static int                       loaded;

static void mark(char c)
{
    if (seq_n + 1u < sizeof seq) {
        seq[seq_n++] = c;
        seq[seq_n] = '\0';
    }
}

static img_t *img_at(uint32_t offset)
{
    size_t i;

    for (i = 0; i < sizeof imgs / sizeof imgs[0]; i++) {
        if (imgs[i].offset == offset) {
            return &imgs[i];
        }
    }
    TEST_FAIL_MESSAGE("no image at that offset");
    return NULL;
}

void l0_test_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    int     n;

    TEST_ASSERT_EQUAL_STRING("tfw-l0", tag);
    log_w += level == 'W';
    log_e += level == 'E';
    if (logs_n + 2u >= sizeof logs) {
        return;
    }
    logs[logs_n++] = level;
    logs[logs_n++] = ':';
    va_start(ap, fmt);
    n = vsnprintf(logs + logs_n, sizeof logs - logs_n, fmt, ap);
    va_end(ap);
    if (n > 0) {
        logs_n += (size_t)n;
        if (logs_n >= sizeof logs) {
            logs_n = sizeof logs - 1u;
        }
    }
    if (logs_n + 1u < sizeof logs) {
        logs[logs_n++] = '\n';
        logs[logs_n] = '\0';
    }
}

static int logged(const char *text)
{
    return strstr(logs, text) != NULL;
}

/* ---- ESP-IDF ---- */

esp_err_t bootloader_init(void)
{
    mark('I');
    return init_rc;
}

bool bootloader_utility_load_partition_table(bootloader_state_t *bs)
{
    mark('T');
    *bs = table;
    return table_ok;
}

int bootloader_utility_get_selected_boot_partition(const bootloader_state_t *bs)
{
    (void)bs;
    mark('s');
    return selected;
}

void bootloader_utility_load_boot_image(const bootloader_state_t *bs, int start_index)
{
    mark('L');
    loaded_bs = bs;
    loaded = start_index;
    TEST_ASSERT_EQUAL_UINT32(OTA0_OFF, bs->ota[0].offset);
    longjmp(env, RAN_LOAD);
}

void bootloader_reset(void)
{
    mark('X');
    longjmp(env, RAN_RESET);
}

esp_err_t esp_image_verify(esp_image_load_mode_t mode, const esp_partition_pos_t *part,
                           esp_image_metadata_t *data)
{
    img_t *im = img_at(part->offset);
    int    i;

    mark('V');
    verify_mode = (int)mode;
    TEST_ASSERT_EQUAL_UINT32(SLOT_SIZE, part->size);
    if (n_verified < 16u) {
        verified[n_verified++] = part->offset;
    }
    data->image.segment_count = im->segment_count;
    data->image.hash_appended = im->hash_appended;
    for (i = 0; i < ESP_IMAGE_MAX_SEGMENTS; i++) {
        data->segments[i].data_len = im->seg_len[i];
    }
    data->segment_data[0] = im->beyond;
    data->image_len = im->image_len;
    memcpy(data->image_digest, im->digest, 32u);
    data->secure_version = im->secure_version;
    return im->verify_rc;
}

uint32_t esp_efuse_read_secure_version(void)
{
    mark('S');
    return 0u;
}

bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block)
{
    mark('P');
    purpose_asked = (int)purpose;
    if (purpose_found) {
        *block = purpose_block;
    }
    return purpose_found;
}

void ets_hmac_enable(void)
{
    mark('e');
}

void ets_hmac_disable(void)
{
    mark('d');
}

int ets_hmac_calculate_message(ets_efuse_block_t key_block, const void *message, size_t message_len,
                               uint8_t *hmac)
{
    mark('H');
    hmac_block = (int)key_block;
    hmac_len = message_len;
    memcpy(hmac_msg, message, 32u);
    memcpy(hmac, hmac_out, 32u);   /* on a refusal too: the unit must clear it */
    return hmac_rc;
}

esp_err_t bootloader_common_read_otadata(const esp_partition_pos_t *ota_info,
                                         esp_ota_select_entry_t *two_otadata)
{
    mark('O');
    n_otadata++;
    TEST_ASSERT_EQUAL_UINT32(table.ota_info.offset, ota_info->offset);
    memcpy(two_otadata, otadata, sizeof otadata);
    return otadata_rc;
}

int bootloader_common_get_active_otadata(esp_ota_select_entry_t *two_otadata)
{
    (void)two_otadata;
    return active;
}

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void)
{
    return &mem;
}

void bootloader_common_reset_rtc_retain_mem(void)
{
    mark('R');
    n_mem_reset++;
    memset(&mem, 0, sizeof mem);
}

void bootloader_common_update_rtc_retain_mem(esp_partition_pos_t *partition, bool reboot_counter)
{
    mark('U');
    updated = *partition;
    updated_counter = reboot_counter;
}

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    mark('C');
    crc_init = crc;
    crc_buf = buf;
    crc_len = len;
    return crc_ret;
}

/* ---- the gate (l0_gate.c) ---- */

tfw_status_t tfw_l0_gate(uint32_t base, uint32_t slot_size, uint32_t esp_len,
                         const uint8_t image_digest[TFW_BOOT_MEASURE_LEN],
                         uint32_t floor, tfw_boot_trailer_t *out)
{
    img_t *im = img_at(base);

    mark('G');
    gate_base = base;
    gate_size = slot_size;
    gate_len = esp_len;
    gate_floor = floor;
    memcpy(gate_digest, image_digest, 32u);
    memset(out, 0, sizeof *out);
    out->image_version = im->tr_version;
    return im->gate_st;
}

tfw_status_t tfw_l0_auth_read(uint8_t rec[TFW_BOOT_AUTH_LEN])
{
    size_t i;

    mark('A');
    n_auth_read++;
    for (i = 0; i < TFW_BOOT_AUTH_LEN; i++) {
        rec[i] = (uint8_t)(0x40u + i);
    }
    return auth_read_st;
}

tfw_status_t tfw_l0_authorized(const tfw_boot_trailer_t *tr, const uint8_t rec[TFW_BOOT_AUTH_LEN])
{
    mark('Z');
    n_authorized++;
    authorized_version = tr->image_version;
    memcpy(authorized_rec, rec, TFW_BOOT_AUTH_LEN);
    return authorized_st;
}

/* ---- the handoff (boot_hal_mock.c, boot_handoff.c) ---- */

void tfw_boot_handoff_build(uint8_t *block, uint8_t tier, const uint8_t cdi[TFW_BOOT_MEASURE_LEN],
                            const uint8_t measurement[TFW_BOOT_MEASURE_LEN], uint32_t image_version,
                            tfw_reset_cause_t reset_cause)
{
    mark('B');
    n_built++;
    built_tier = tier;
    memcpy(built_cdi, cdi, 32u);
    memcpy(built_meas, measurement, 32u);
    built_version = image_version;
    built_cause = reset_cause;
    memset(block, 0xB7, TFW_BOOT_HANDOFF_LEN);
}

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    mark('W');
    written = block;
    written_len = len;
    memcpy(written_copy, block, TFW_BOOT_HANDOFF_LEN);
}

tfw_reset_cause_t tfw_boot_reset_cause(void)
{
    mark('Q');
    return TFW_RESET_WATCHDOG;
}

/* ---- the driver ---- */

static void good_image(img_t *im, uint32_t offset)
{
    size_t i;

    memset(im, 0, sizeof *im);
    im->offset = offset;
    im->verify_rc = ESP_OK;
    im->segment_count = 2u;
    im->seg_len[0] = 100u;
    im->seg_len[1] = 200u;
    im->image_len = 1024u;
    im->hash_appended = 1u;
    for (i = 0; i < 32u; i++) {
        im->digest[i] = (uint8_t)((offset >> 16) + i);
    }
    im->secure_version = 3u;
    im->gate_st = TFW_OK;
    im->tr_version = 3u;
}

void setUp(void)
{
    good_image(&imgs[0], FACTORY_OFF);
    good_image(&imgs[1], OTA0_OFF);
    good_image(&imgs[2], OTA1_OFF);
    good_image(&imgs[3], OTA2_OFF);
    good_image(&imgs[4], TEST_OFF);
    memset(&table, 0, sizeof table);
    table.ota_info.offset = 0xd000u;
    table.ota_info.size = 0x2000u;
    table.factory.offset = FACTORY_OFF;
    table.factory.size = SLOT_SIZE;
    table.ota[0].offset = OTA0_OFF;
    table.ota[0].size = SLOT_SIZE;
    table.ota[1].offset = OTA1_OFF;
    table.ota[1].size = SLOT_SIZE;
    table.app_count = 2u;
    table_ok = true;
    init_rc = ESP_OK;
    selected = 0;
    seq_n = 0u;
    seq[0] = '\0';
    logs_n = 0u;
    logs[0] = '\0';
    log_w = 0;
    log_e = 0;
    n_verified = 0u;
    verify_mode = -1;
    gate_base = gate_size = gate_len = gate_floor = 0xFFFFFFFFu;
    memset(gate_digest, 0, sizeof gate_digest);
    auth_read_st = TFW_OK;
    authorized_st = TFW_OK;
    n_auth_read = 0;
    n_authorized = 0;
    authorized_version = 0u;
    memset(authorized_rec, 0, sizeof authorized_rec);
    otadata_rc = ESP_OK;
    memset(otadata, 0, sizeof otadata);
    otadata[0].ota_seq = 1u;
    otadata[0].ota_state = ESP_OTA_IMG_VALID;
    active = 0;
    n_otadata = 0;
    purpose_found = false;
    purpose_block = EFUSE_BLK_KEY3;
    purpose_asked = -1;
    hmac_rc = 0;
    memset(hmac_out, 0xC5, sizeof hmac_out);
    hmac_block = -1;
    memset(hmac_msg, 0, sizeof hmac_msg);
    hmac_len = 0u;
    built_tier = 0xFFu;
    memset(built_cdi, 0xEE, sizeof built_cdi);
    memset(built_meas, 0, sizeof built_meas);
    built_version = 0u;
    built_cause = TFW_RESET_UNKNOWN;
    n_built = 0;
    written = NULL;
    memset(written_copy, 0, sizeof written_copy);
    written_len = 0u;
    memset(&mem, 0, sizeof mem);
    mem.crc = 0x12345678u;
    crc_ret = 0x12345678u;
    crc_init = crc_len = 0u;
    crc_buf = NULL;
    n_mem_reset = 0;
    memset(&updated, 0, sizeof updated);
    updated_counter = true;
    loaded_bs = NULL;
    loaded = 0x7777;
}

void tearDown(void)
{
}

/* Leaves stale bytes where the unit's stack frames will be, so a buffer the
 * unit fails to clear is seen not to be clear. */
static void __attribute__((noinline)) dirty_stack(void)
{
    volatile uint8_t junk[8192];
    size_t           i;

    for (i = 0; i < sizeof junk; i++) {
        junk[i] = 0xA5u;
    }
}

static int __attribute__((noinline)) run(void)
{
    int r;

    dirty_stack();
    r = setjmp(env);
    if (r == 0) {
        call_start_cpu0();
    }
    return r;
}

static void assert_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, p[i]);
    }
}

static void assert_verified(const uint32_t *offs, size_t n)
{
    TEST_ASSERT_EQUAL_size_t(n, n_verified);
    if (n > 0u) {
        TEST_ASSERT_EQUAL_HEX32_ARRAY(offs, verified, n);
    }
}

static void pending(uint32_t seq_no)
{
    otadata[0].ota_seq = seq_no;
    otadata[0].ota_state = ESP_OTA_IMG_PENDING_VERIFY;
    active = 0;
}

/* ---- call_start_cpu0 ---- */

static void test_a_failed_init_resets_before_the_partition_table(void)
{
    init_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_EQUAL_STRING("IX", seq);
}

static void test_no_partition_table_resets(void)
{
    table_ok = false;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_EQUAL_STRING("ITX", seq);
    TEST_ASSERT_TRUE(logged("E:no partition table\n"));
}

static void test_no_selected_partition_resets_without_a_check(void)
{
    selected = INVALID_INDEX;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_EQUAL_STRING("ITsX", seq);
}

static void test_a_good_chosen_slot_is_handed_off_and_loaded(void)
{
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_NOT_NULL(loaded_bs);
    TEST_ASSERT_EQUAL_STRING("ITsOVSGPQBCUWL", seq);
    TEST_ASSERT_EQUAL_INT(ESP_IMAGE_VERIFY, verify_mode);
    TEST_ASSERT_EQUAL_HEX32(OTA0_OFF, gate_base);
    TEST_ASSERT_EQUAL_HEX32(SLOT_SIZE, gate_size);
    TEST_ASSERT_EQUAL_UINT32(0u, gate_floor);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[1].digest, gate_digest, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[1].digest, built_meas, 32);
    TEST_ASSERT_EQUAL_UINT32(3u, built_version);
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, built_cause);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, built_tier);
    assert_zero(built_cdi, 32u);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_HANDOFF_LEN, written_len);
    TEST_ASSERT_EQUAL_HEX8(0xB7, written_copy[0]);
    TEST_ASSERT_EQUAL_HEX8(0xB7, written_copy[TFW_BOOT_HANDOFF_LEN - 1u]);
    TEST_ASSERT_NOT_NULL(written);
    assert_zero(written, TFW_BOOT_HANDOFF_LEN);   /* the static block, cleared after */
    TEST_ASSERT_TRUE(logged("I:app at 0x110000 verified, measurement "
                            "1112131415161718191a1b1c1d1e1f20"
                            "2122232425262728292a2b2c2d2e2f30\n"));
}

static void test_no_slot_passing_resets_after_every_slot_in_order(void)
{
    static const uint32_t order[] = { OTA1_OFF, OTA0_OFF, FACTORY_OFF, OTA2_OFF };

    table.ota[2].offset = OTA2_OFF;
    table.ota[2].size = SLOT_SIZE;
    table.app_count = 3u;
    selected = 1;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[1].verify_rc = ESP_FAIL;
    imgs[2].verify_rc = ESP_FAIL;
    imgs[3].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    assert_verified(order, 4u);
    TEST_ASSERT_EQUAL_INT(0, n_built);
    TEST_ASSERT_NULL(written);
    TEST_ASSERT_TRUE(logged("E:no app passed the gate\n"));
    TEST_ASSERT_TRUE(logged("E:app at 0x310000 failed verification: no CDI\n"));
}

/* ---- first_passing: ESP-IDF's fallback order ---- */

static void test_the_first_passing_slot_below_is_loaded(void)
{
    static const uint32_t order[] = { OTA1_OFF, OTA0_OFF };

    selected = 1;
    imgs[2].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    assert_verified(order, 2u);
    TEST_ASSERT_EQUAL_INT(1, n_built);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[1].digest, built_meas, 32);
}

static void test_the_factory_slot_passes_after_the_chosen_one(void)
{
    static const uint32_t order[] = { OTA0_OFF, FACTORY_OFF };

    imgs[1].gate_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
    assert_verified(order, 2u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[0].digest, built_meas, 32);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_OFF, updated.offset);
}

static void test_a_slot_above_passes_when_none_below_does(void)
{
    static const uint32_t order[] = { OTA0_OFF, FACTORY_OFF, OTA1_OFF };

    imgs[0].verify_rc = ESP_FAIL;
    imgs[1].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, loaded);
    assert_verified(order, 3u);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);   /* a slot above is never on trial */
}

/* Downward stops at the factory slot: the test app is not in that order. */
static void test_the_test_app_is_not_below_the_factory_slot(void)
{
    static const uint32_t order[] = { OTA0_OFF, FACTORY_OFF, OTA1_OFF };

    table.test.offset = TEST_OFF;
    table.test.size = SLOT_SIZE;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[1].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, loaded);
    assert_verified(order, 3u);
}

/* Upward stops at app_count, whatever the table holds past it. */
static void test_no_slot_past_the_app_count_is_checked(void)
{
    static const uint32_t order[] = { OTA0_OFF, FACTORY_OFF, OTA1_OFF };

    table.ota[2].offset = OTA2_OFF;
    table.ota[2].size = SLOT_SIZE;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[1].verify_rc = ESP_FAIL;
    imgs[2].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    assert_verified(order, 3u);
}

static void test_a_chosen_factory_slot_falls_back_upward(void)
{
    static const uint32_t order[] = { FACTORY_OFF, OTA0_OFF, OTA1_OFF };

    selected = FACTORY_INDEX;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[1].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, loaded);
    assert_verified(order, 3u);
    TEST_ASSERT_EQUAL_INT(0, n_otadata);   /* the factory slot is never on trial */
}

static void test_the_test_app_is_checked_alone(void)
{
    static const uint32_t order[] = { TEST_OFF };

    table.test.offset = TEST_OFF;
    table.test.size = SLOT_SIZE;
    selected = TEST_APP_INDEX;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(TEST_APP_INDEX, loaded);
    assert_verified(order, 1u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[4].digest, built_meas, 32);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);   /* the test app is never on trial */
}

static void test_a_refused_test_app_falls_back_to_nothing(void)
{
    static const uint32_t order[] = { TEST_OFF };

    table.test.offset = TEST_OFF;
    table.test.size = SLOT_SIZE;
    selected = TEST_APP_INDEX;
    imgs[4].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    assert_verified(order, 1u);
}

static void test_a_slot_not_in_the_table_is_not_checked(void)
{
    static const uint32_t order[] = { FACTORY_OFF, OTA1_OFF };

    selected = FACTORY_INDEX;
    table.ota[0].size = 0u;
    imgs[0].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, loaded);
    assert_verified(order, 2u);
}

/* ---- check_slot ---- */

static void test_no_appended_digest_is_refused_before_the_gate(void)
{
    imgs[1].hash_appended = 0u;
    selected = 0;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[2].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_EQUAL_STRING("ITsOVVVX", seq);
    TEST_ASSERT_TRUE(logged("E:app at 0x110000 has no appended digest to bind: refused, no CDI\n"));
}

/* Header 24 + (8 + 100) + (8 + 200) = 340; padded with the checksum to 352;
 * the digest ends at 384. */
static void test_the_digest_end_is_the_image_format_s(void)
{
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_UINT32(384u, gate_len);
}

/* 24 + 8 = 32 already a multiple of 16: the checksum byte still takes a
 * whole block, 48, and the digest ends at 80. */
static void test_a_segment_end_on_a_block_still_pads_a_block(void)
{
    imgs[1].segment_count = 1u;
    imgs[1].seg_len[0] = 0u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_UINT32(80u, gate_len);
}

/* 24 + 8 + 15 = 47, one short of a block: the checksum byte fills it, 48,
 * and the digest ends at 80. */
static void test_a_segment_end_one_short_of_a_block_pads_to_it(void)
{
    imgs[1].segment_count = 1u;
    imgs[1].seg_len[0] = 15u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_UINT32(80u, gate_len);
}

static void test_no_segments_is_the_header_alone(void)
{
    imgs[1].segment_count = 0u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_UINT32(64u, gate_len);
}

static void test_segments_past_the_maximum_are_not_counted(void)
{
    size_t i;

    imgs[1].segment_count = ESP_IMAGE_MAX_SEGMENTS + 1;
    for (i = 0; i < ESP_IMAGE_MAX_SEGMENTS; i++) {
        imgs[1].seg_len[i] = 8u;
    }
    imgs[1].beyond = 0x1000u;
    /* 24 + 16 * 16 = 280, padded to 288, +32 */
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_UINT32(320u, gate_len);
}

static void test_a_digest_ending_at_the_image_length_is_bound(void)
{
    imgs[1].image_len = 384u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
}

static void test_a_digest_ending_past_the_image_length_is_refused(void)
{
    imgs[1].image_len = 383u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_OFF, gate_base);
}

static void test_a_gate_refusal_logs_its_status_and_hands_off_nothing(void)
{
    imgs[1].gate_st = TFW_ERR_CRYPTO;
    imgs[1].tr_version = 3u;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[2].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_TRUE(logged("E:app at 0x110000 refused by the trailer gate (status 14): no CDI\n"));
    TEST_ASSERT_EQUAL_INT(0, n_built);
}

static void test_a_gate_refusal_keeps_its_status_when_the_versions_differ(void)
{
    imgs[1].gate_st = TFW_ERR_RANGE;
    imgs[1].tr_version = 4u;
    imgs[0].verify_rc = ESP_FAIL;
    imgs[2].verify_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_RESET, run());
    TEST_ASSERT_TRUE(logged("(status 11)"));
}

static void test_a_trailer_version_not_the_secure_version_is_refused(void)
{
    imgs[1].tr_version = 4u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
    TEST_ASSERT_TRUE(logged("E:app at 0x110000 refused by the trailer gate (status 16): no CDI\n"));
}

static void test_the_handed_off_version_is_the_trailer_s(void)
{
    imgs[1].tr_version = 9u;
    imgs[1].secure_version = 9u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_UINT32(9u, built_version);
}

/* ---- on_trial and the activate's authorization ---- */

static void test_a_trial_slot_with_its_authorization_is_loaded(void)
{
    pending(1u);
    imgs[1].tr_version = 5u;
    imgs[1].secure_version = 5u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_STRING("ITsOVSGAZPQBCUWL", seq);
    TEST_ASSERT_EQUAL_UINT32(5u, authorized_version);
    TEST_ASSERT_EQUAL_HEX8(0x40, authorized_rec[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40 + TFW_BOOT_AUTH_LEN - 1u, authorized_rec[TFW_BOOT_AUTH_LEN - 1u]);
    TEST_ASSERT_TRUE(logged("I:app at 0x110000 on trial, activate authorized\n"));
}

static void test_a_trial_with_no_record_falls_back_unasked(void)
{
    pending(1u);
    auth_read_st = TFW_ERR_NOT_PROVISIONED;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
    TEST_ASSERT_EQUAL_INT(1, n_auth_read);
    TEST_ASSERT_EQUAL_INT(0, n_authorized);
    TEST_ASSERT_EQUAL_INT(1, n_built);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[0].digest, built_meas, 32);
}

static void test_a_trial_not_authorized_falls_back(void)
{
    char want[96];

    pending(1u);
    authorized_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
    TEST_ASSERT_EQUAL_INT(1, n_authorized);
    (void)snprintf(want, sizeof want, "on trial has no activate authorization (status %d): no CDI\n",
                   (int)TFW_ERR_CRYPTO);
    TEST_ASSERT_TRUE(logged(want));
}

/* Only the chosen slot is on trial: the one below it is not asked. */
static void test_the_fallback_below_a_trial_is_not_on_trial(void)
{
    selected = 1;
    pending(2u);
    authorized_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_INT(1, n_auth_read);
}

static void test_a_valid_active_entry_is_no_trial(void)
{
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_otadata);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);
}

static void test_a_pending_entry_for_another_slot_is_no_trial(void)
{
    pending(2u);
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);
}

/* Three slots tell ota_seq - 1 from ota_seq + 1 and from ota_seq. */
static void test_the_sequence_names_the_slot_modulo_the_count(void)
{
    table.ota[2].offset = OTA2_OFF;
    table.ota[2].size = SLOT_SIZE;
    table.app_count = 3u;
    pending(4u);
    authorized_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_authorized);
    TEST_ASSERT_EQUAL_INT(FACTORY_INDEX, loaded);
}

static void test_the_second_entry_active_is_read(void)
{
    otadata[0].ota_seq = 2u;
    otadata[1].ota_seq = 1u;
    otadata[1].ota_state = ESP_OTA_IMG_PENDING_VERIFY;
    active = 1;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_auth_read);
}

static void test_no_active_entry_is_no_trial(void)
{
    pending(1u);
    active = -1;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);
}

static void test_unreadable_ota_data_is_no_trial(void)
{
    pending(1u);
    otadata_rc = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_otadata);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);
}

static void test_no_ota_data_partition_reads_nothing(void)
{
    pending(1u);
    table.ota_info.size = 0u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, n_otadata);
    TEST_ASSERT_EQUAL_INT(0, n_auth_read);
}

static void test_no_ota_apps_reads_nothing(void)
{
    pending(1u);
    table.app_count = 0u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_INT(0, n_otadata);
}

/* ---- derive_cdi ---- */

static void test_no_hmac_up_key_is_the_none_tier_with_no_cdi(void)
{
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, purpose_asked);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, built_tier);
    assert_zero(built_cdi, 32u);
    TEST_ASSERT_EQUAL_INT(1, log_w);
    TEST_ASSERT_TRUE(logged("W:no HMAC_UP key in eFuse: this part has no UDS\n"));
}

static void test_the_cdi_is_the_hmac_of_the_measurement(void)
{
    purpose_found = true;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_STRING("ITsOVSGPeHdQBCUWL", seq);
    TEST_ASSERT_EQUAL_INT(ETS_EFUSE_BLOCK_KEY0 + 3, hmac_block);
    TEST_ASSERT_EQUAL_size_t(32u, hmac_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(imgs[1].digest, hmac_msg, 32);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, built_tier);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hmac_out, built_cdi, 32);
    TEST_ASSERT_EQUAL_INT(0, log_w);
    TEST_ASSERT_TRUE(logged("I:cdi from eFuse BLOCK7 through the HMAC peripheral\n"));
}

static void test_a_refused_hmac_is_the_none_tier_with_the_cdi_cleared(void)
{
    purpose_found = true;
    hmac_rc = 1;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(0, loaded);
    TEST_ASSERT_EQUAL_STRING("ITsOVSGPeHdQBCUWL", seq);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, built_tier);
    assert_zero(built_cdi, 32u);
    TEST_ASSERT_TRUE(logged("E:HMAC peripheral refused key block 7\n"));
}

/* ---- retain_mem_prepare ---- */

static void test_a_valid_retained_record_is_kept_and_updated(void)
{
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, crc_init);
    TEST_ASSERT_EQUAL_PTR(&mem, crc_buf);
    TEST_ASSERT_EQUAL_UINT32(offsetof(rtc_retain_mem_t, custom), crc_len);
    TEST_ASSERT_EQUAL_INT(0, n_mem_reset);
    TEST_ASSERT_EQUAL_HEX32(OTA0_OFF, updated.offset);
    TEST_ASSERT_EQUAL_HEX32(SLOT_SIZE, updated.size);
    TEST_ASSERT_FALSE(updated_counter);
}

static void test_a_retained_record_with_a_bad_crc_is_reset(void)
{
    crc_ret = 0x12345679u;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_mem_reset);
    TEST_ASSERT_EQUAL_STRING("ITsOVSGPQBCRUWL", seq);
}

/* A cold boot's all-zero header: its CRC is all ones, and that is refused
 * even where it checks. */
static void test_an_all_ones_crc_is_reset_even_where_it_checks(void)
{
    mem.crc = UINT32_MAX;
    crc_ret = UINT32_MAX;
    TEST_ASSERT_EQUAL_INT(RAN_LOAD, run());
    TEST_ASSERT_EQUAL_INT(1, n_mem_reset);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_init_resets_before_the_partition_table);
    RUN_TEST(test_no_partition_table_resets);
    RUN_TEST(test_no_selected_partition_resets_without_a_check);
    RUN_TEST(test_a_good_chosen_slot_is_handed_off_and_loaded);
    RUN_TEST(test_no_slot_passing_resets_after_every_slot_in_order);
    RUN_TEST(test_the_first_passing_slot_below_is_loaded);
    RUN_TEST(test_the_factory_slot_passes_after_the_chosen_one);
    RUN_TEST(test_a_slot_above_passes_when_none_below_does);
    RUN_TEST(test_the_test_app_is_not_below_the_factory_slot);
    RUN_TEST(test_no_slot_past_the_app_count_is_checked);
    RUN_TEST(test_a_chosen_factory_slot_falls_back_upward);
    RUN_TEST(test_the_test_app_is_checked_alone);
    RUN_TEST(test_a_refused_test_app_falls_back_to_nothing);
    RUN_TEST(test_a_slot_not_in_the_table_is_not_checked);
    RUN_TEST(test_no_appended_digest_is_refused_before_the_gate);
    RUN_TEST(test_the_digest_end_is_the_image_format_s);
    RUN_TEST(test_a_segment_end_on_a_block_still_pads_a_block);
    RUN_TEST(test_a_segment_end_one_short_of_a_block_pads_to_it);
    RUN_TEST(test_no_segments_is_the_header_alone);
    RUN_TEST(test_segments_past_the_maximum_are_not_counted);
    RUN_TEST(test_a_digest_ending_at_the_image_length_is_bound);
    RUN_TEST(test_a_digest_ending_past_the_image_length_is_refused);
    RUN_TEST(test_a_gate_refusal_logs_its_status_and_hands_off_nothing);
    RUN_TEST(test_a_gate_refusal_keeps_its_status_when_the_versions_differ);
    RUN_TEST(test_a_trailer_version_not_the_secure_version_is_refused);
    RUN_TEST(test_the_handed_off_version_is_the_trailer_s);
    RUN_TEST(test_a_trial_slot_with_its_authorization_is_loaded);
    RUN_TEST(test_a_trial_with_no_record_falls_back_unasked);
    RUN_TEST(test_a_trial_not_authorized_falls_back);
    RUN_TEST(test_the_fallback_below_a_trial_is_not_on_trial);
    RUN_TEST(test_a_valid_active_entry_is_no_trial);
    RUN_TEST(test_a_pending_entry_for_another_slot_is_no_trial);
    RUN_TEST(test_the_sequence_names_the_slot_modulo_the_count);
    RUN_TEST(test_the_second_entry_active_is_read);
    RUN_TEST(test_no_active_entry_is_no_trial);
    RUN_TEST(test_unreadable_ota_data_is_no_trial);
    RUN_TEST(test_no_ota_data_partition_reads_nothing);
    RUN_TEST(test_no_ota_apps_reads_nothing);
    RUN_TEST(test_no_hmac_up_key_is_the_none_tier_with_no_cdi);
    RUN_TEST(test_the_cdi_is_the_hmac_of_the_measurement);
    RUN_TEST(test_a_refused_hmac_is_the_none_tier_with_the_cdi_cleared);
    RUN_TEST(test_a_valid_retained_record_is_kept_and_updated);
    RUN_TEST(test_a_retained_record_with_a_bad_crc_is_reset);
    RUN_TEST(test_an_all_ones_crc_is_reset_even_where_it_checks);
    return UNITY_END();
}
