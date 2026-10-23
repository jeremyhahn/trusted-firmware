/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/*
 * boot/boot_ota.c under the faults test_boot_ota.c let through (#163): each
 * test names the mutant it kills. Equivalent, and so not tested: the padding
 * loop's bound (boot_ota.c:31) and end's flush (boot_ota.c:136), because
 * tfw_ota_begin admits only whole pages and so the buffer is empty at end;
 * begin's zero check (boot_ota.c:62), which the trailer bound below it also
 * refuses; data's flush at `== page_size` against `>=` (boot_ota.c:109) and
 * end's `!=` against `<` (boot_ota.c:131), since neither count can pass its
 * bound; and abort's buf_used and declared_len (boot_ota.c:149-150), which
 * begin sets again before any data is accepted.
 */

#include <string.h>
#include "unity.h"
#include "boot_ota.h"

void setUp(void) { }
void tearDown(void) {}

#define PAGE 128u

static uint8_t  written[4096];
static unsigned pages;
static uint32_t first_addr;
static unsigned fail_after;

static tfw_status_t sink(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (fail_after > 0u && pages >= fail_after) {
        return TFW_ERR_INTERNAL;
    }
    if (pages == 0u) {
        first_addr = addr;
    }
    if (addr + len <= sizeof written) {
        memcpy(&written[addr], data, len);
    }
    pages++;
    return TFW_OK;
}

static void fresh(tfw_ota_t *o)
{
    memset(written, 0, sizeof written);
    pages = 0u; first_addr = 0xFFFFFFFFu; fail_after = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(o, sink, PAGE));
}

/* Kills `> TFW_BOOT_APP_SIZE` -> `>=` (boot_ota.c:62): an image that fills the
 * whole application region is the largest one admitted, not one too many. */
static void test_begin_admits_an_image_the_size_of_the_region(void)
{
    tfw_ota_t o;
    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, TFW_BOOT_APP_SIZE));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_RECEIVING, tfw_ota_state(&o));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_begin(&o, TFW_BOOT_APP_SIZE + PAGE));
}

/* Kills begin keeping `received` (boot_ota.c:79) and `buf_used`
 * (boot_ota.c:80): a second OTA_BEGIN inside a session starts the stream
 * again at offset 0, and the first page written is the new stream's first
 * page, at the region's start, with none of the old bytes in it. */
static void test_a_second_begin_restarts_the_stream_at_zero(void)
{
    tfw_ota_t o;
    uint8_t  old[16];
    uint8_t  chunk[64];

    fresh(&o);
    memset(old, 0xEE, sizeof old);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, old, (uint8_t)sizeof old));

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_ota_received(&o));
    for (uint32_t off = 0u; off < PAGE * 2u; off += sizeof chunk) {
        for (uint8_t i = 0u; i < sizeof chunk; i++) { chunk[i] = (uint8_t)(off + i); }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, off, chunk, (uint8_t)sizeof chunk));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT(2u, pages);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)TFW_OTA_STAGE_DEST, first_addr);
    for (uint16_t i = 0u; i < PAGE * 2u; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, written[i]);
    }
}

/* Kills data returning TFW_OK after a failed page write (boot_ota.c:113): the
 * chunk whose page the flash refused says so with the writer's own status,
 * and every chunk after it is refused as outside a session. */
static void test_the_chunk_whose_page_fails_returns_the_writers_status(void)
{
    tfw_ota_t o;
    uint8_t  chunk[64];

    fresh(&o);
    memset(chunk, 0x5A, sizeof chunk);
    fail_after = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 4u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, (uint8_t)sizeof chunk));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 64u, chunk, (uint8_t)sizeof chunk));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, PAGE, chunk, (uint8_t)sizeof chunk));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_ota_data(&o, PAGE + 64u, chunk, (uint8_t)sizeof chunk));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_FAILED, tfw_ota_state(&o));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_ota_data(&o, PAGE * 2u, chunk, (uint8_t)sizeof chunk));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_ota_end(&o));
}

/* Kills `len > remaining` -> `>=` (boot_ota.c:102): a chunk that ends exactly
 * at the declared length is the last one, and is accepted. */
static void test_a_chunk_ending_at_the_declared_length_is_accepted(void)
{
    tfw_ota_t o;
    uint8_t  chunk[PAGE];

    fresh(&o);
    memset(chunk, 0x33, sizeof chunk);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, (uint8_t)(PAGE - 1u)));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, PAGE - 1u, chunk, (uint8_t)(PAGE - 1u)));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, PAGE * 2u - 2u, chunk, 2u));
    TEST_ASSERT_EQUAL_UINT32(PAGE * 2u, tfw_ota_received(&o));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT(2u, pages);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_admits_an_image_the_size_of_the_region);
    RUN_TEST(test_a_second_begin_restarts_the_stream_at_zero);
    RUN_TEST(test_the_chunk_whose_page_fails_returns_the_writers_status);
    RUN_TEST(test_a_chunk_ending_at_the_declared_length_is_accepted);
    return UNITY_END();
}
