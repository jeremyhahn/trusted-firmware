/*
 * A single-slot stager (the 328P, the 32U4) links OTA_ACTIVATE without the rest
 * of ota_ctl.c (#77): OTA_END does not write the MARK, so ACTIVATE is the only
 * way to install a staged image. This binary is linked in that shape -- the
 * stager, ota_prev.c and ota_activate.c, no ota_ctl.c -- by mk/host.mk.
 */

#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "ota_ctl.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "sha256.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */

static uint8_t img[TFW_OTA_STORE_STAGE_LEN];

void setUp(void)    { tfw_nor_model_reset(); }
void tearDown(void) { }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void build(uint32_t total, uint32_t image_len)
{
    tfw_sha256_t c;
    uint8_t     *t = &img[total - TRAILER];
    uint8_t      meas[32];
    uint32_t     i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < image_len; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    for (i = 0u; i < image_len; i += 1024u) {
        tfw_sha256_update(&c, &img[i], (uint16_t)(image_len - i < 1024u ? image_len - i : 1024u));
    }
    tfw_sha256_final(&c, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[8], 3u);
    put32(&t[12], image_len);
    memcpy(&t[16], meas, 32u);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n)
{
    uint8_t      out[44];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, sizeof out);
    return fn(&r, &w);
}

static void stage(uint32_t total, uint32_t image_len)
{
    uint8_t  in[4u + CHUNK];
    uint32_t off;
    uint8_t  n;

    build(total, image_len);
    put32(in, total);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_begin, in, 4u));
    for (off = 0u; off < total; off += CHUNK) {
        n = (uint8_t)(total - off < CHUNK ? total - off : CHUNK);
        put32(in, off);
        memcpy(&in[4], &img[off], n);
        TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_data, in, (uint8_t)(4u + n)));
    }
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_end, NULL, 0u));
}

static void put_good_block(void)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 1u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

/* OTA_END alone leaves the store unmarked: nothing installs it at reset. */
static void test_a_staged_image_is_not_marked_without_activate(void)
{
    put_good_block();
    stage(512u, 200u);
    TEST_ASSERT_FALSE(marked());
}

/* OTA_ACTIVATE on the single-slot link writes the MARK. */
static void test_activate_marks_the_staged_image(void)
{
    uint8_t m0 = 0u;

    put_good_block();
    stage(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_otactl_cmd_activate, &m0, 1u));
    TEST_ASSERT_TRUE(marked());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_staged_image_is_not_marked_without_activate);
    RUN_TEST(test_activate_marks_the_staged_image);
    return UNITY_END();
}
