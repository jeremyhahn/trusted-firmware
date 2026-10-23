/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OTA state machine.
 *
 * What is interesting is not the happy path: it is the link dropping in the
 * middle, a chunk arriving out of order, a flash that stops accepting writes.
 * None of those is producible on a bench on demand and each is one line here.
 */

#include <string.h>
#include "unity.h"
#include "boot_ota.h"

void setUp(void) { }
void tearDown(void) {}

#define PAGE 128u

static uint8_t  written[4096];
static unsigned pages;
static uint32_t last_addr;
static unsigned fail_after;

static tfw_status_t sink(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (fail_after > 0u && pages >= fail_after) {
        return TFW_ERR_INTERNAL;
    }
    if (addr + len <= sizeof written) {
        memcpy(&written[addr], data, len);
    }
    last_addr = addr;
    pages++;
    return TFW_OK;
}

static void fresh(tfw_ota_t *o)
{
    memset(written, 0, sizeof written);
    pages = 0u; last_addr = 0u; fail_after = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(o, sink, PAGE));
}

/* ------------------------------------------------------------------ init */

static void test_init_refuses_a_null_writer(void)
{
    tfw_ota_t o;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_init(&o, 0, PAGE));
}

/* Every address computation assumes pages tile the region exactly. A page size
 * of 100 would leave the last page of a region straddling its end, and the
 * write would land half in the bootloader. */
static void test_init_refuses_a_page_size_that_is_not_a_power_of_two(void)
{
    tfw_ota_t o;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_init(&o, sink, 100u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_init(&o, sink, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_init(&o, sink, TFW_OTA_PAGE_MAX * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&o, sink, TFW_OTA_PAGE_MAX));
}

/* ----------------------------------------------------------------- begin */

static void test_begin_accepts_a_whole_number_of_pages(void)
{
    tfw_ota_t o;
    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 4u));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_RECEIVING, tfw_ota_state(&o));
}

/* An image shorter than a trailer has nowhere to carry a signature. Refusing
 * here is what stops the device erasing a working application to write
 * something that could never have booted. */
static void test_begin_refuses_an_image_too_short_to_carry_a_trailer(void)
{
    tfw_ota_t o;
    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_begin(&o, PAGE));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_begin(&o, 0u));
}

static void test_begin_refuses_a_partial_page_and_an_oversized_image(void)
{
    tfw_ota_t o;
    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_begin(&o, PAGE * 4u + 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_begin(&o, TFW_BOOT_APP_SIZE + PAGE));
}

/* ------------------------------------------------------------------ data */

static void test_a_whole_image_reaches_flash_in_order(void)
{
    tfw_ota_t o;
    uint8_t  chunk[32];
    uint32_t off;

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 4u));

    for (off = 0u; off < PAGE * 4u; off += 32u) {
        for (uint8_t i = 0u; i < 32u; i++) { chunk[i] = (uint8_t)(off + i); }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, off, chunk, 32u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_COMPLETE, tfw_ota_state(&o));
    TEST_ASSERT_EQUAL_UINT32(PAGE * 4u, tfw_ota_received(&o));
    TEST_ASSERT_EQUAL_UINT(4u, pages);
    TEST_ASSERT_EQUAL_UINT32(PAGE * 3u, last_addr);

    for (uint16_t i = 0u; i < PAGE * 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, written[i]);
    }
}

/*
 * A stream with no offset is a stream where one dropped frame silently shifts
 * everything after it -- and the image then fails to verify for a reason that
 * looks like a signing bug rather than a lost frame.
 */
static void test_a_chunk_at_the_wrong_offset_is_refused(void)
{
    tfw_ota_t o;
    uint8_t  chunk[16] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, 16u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_data(&o, 32u, chunk, 16u));  /* skipped */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_data(&o, 0u, chunk, 16u));   /* rewound */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 16u, chunk, 16u));         /* correct */
}

static void test_data_past_the_declared_length_is_refused(void)
{
    tfw_ota_t o;
    uint8_t  chunk[64] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    for (uint32_t off = 0u; off < PAGE * 2u; off += 64u) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, off, chunk, 64u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_ota_data(&o, PAGE * 2u, chunk, 1u));
}

static void test_data_outside_a_session_is_refused(void)
{
    tfw_ota_t o;
    uint8_t  chunk[8] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_ota_data(&o, 0u, chunk, 8u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_ota_end(&o));
}

static void test_an_empty_chunk_is_refused(void)
{
    tfw_ota_t o;
    uint8_t  chunk[8] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, tfw_ota_data(&o, 0u, chunk, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, tfw_ota_data(&o, 0u, 0, 8u));
}

/* ------------------------------------------------------------------- end */

/*
 * THE INTERRUPTED UPDATE. The tail never arrived; the last page would otherwise
 * be measured over whatever it happened to hold, and the signature check would
 * fail saying "bad signature" rather than "truncated transfer".
 */
static void test_a_truncated_transfer_is_refused_at_end(void)
{
    tfw_ota_t o;
    uint8_t  chunk[64] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 4u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, 64u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_FAILED, tfw_ota_state(&o));
}

/* A flash that stops accepting writes partway -- a sagging supply, a worn page.
 * The session fails and says so; it does not report a committed image. */
static void test_a_failing_flash_fails_the_session(void)
{
    tfw_ota_t o;
    uint8_t  chunk[PAGE];

    fresh(&o);
    memset(chunk, 0xAB, sizeof chunk);
    fail_after = 2u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 4u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, (uint8_t)(PAGE - 1u)));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, PAGE - 1u, chunk, PAGE - 1u));
    /* the third page write is the one that fails */
    (void)tfw_ota_data(&o, (PAGE * 2u) - 2u, chunk, PAGE - 1u);
    (void)tfw_ota_data(&o, (PAGE * 3u) - 3u, chunk, PAGE - 1u);
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_FAILED, tfw_ota_state(&o));
}

/* An image of exactly two pages, sent in chunks smaller than a page after an
 * explicit init, reaches flash as exactly two page writes: the chunks are
 * gathered into whole pages, never written one by one. No padding is pinned
 * here; tfw_ota_begin admits whole pages only (see the next test). */
static void test_a_two_page_image_lands_as_exactly_two_page_writes(void)
{
    tfw_ota_t o;
    uint8_t  chunk[64];

    fresh(&o);
    memset(chunk, 0x11, sizeof chunk);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&o, sink, PAGE));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    for (uint32_t off = 0u; off < PAGE * 2u; off += 64u) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, off, chunk, 64u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT(2u, pages);
}

/* tfw_ota_begin admits only whole pages and tfw_ota_data flushes each page as
 * it fills, so at end the buffer is always empty: flush's padding loop
 * (boot_ota.c:32) and end's flush failure (boot_ota.c:138-139) are never
 * reached. A writer that refuses every further page proves end writes none. */
static void test_end_writes_nothing_after_the_last_whole_page(void)
{
    tfw_ota_t o;
    uint8_t  chunk[64];

    fresh(&o);
    memset(chunk, 0x22, sizeof chunk);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    for (uint32_t off = 0u; off < PAGE * 2u; off += 64u) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, off, chunk, 64u));
    }
    TEST_ASSERT_EQUAL_UINT(2u, pages);
    fail_after = pages;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_end(&o));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_COMPLETE, tfw_ota_state(&o));
    TEST_ASSERT_EQUAL_UINT(2u, pages);
}

static void test_abort_returns_to_idle_and_keeps_no_progress(void)
{
    tfw_ota_t o;
    uint8_t  chunk[16] = {0};

    fresh(&o);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_begin(&o, PAGE * 2u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_data(&o, 0u, chunk, 16u));
    tfw_ota_abort(&o);
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_IDLE, tfw_ota_state(&o));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_ota_received(&o));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_ota_data(&o, 0u, chunk, 16u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_refuses_a_null_writer);
    RUN_TEST(test_init_refuses_a_page_size_that_is_not_a_power_of_two);
    RUN_TEST(test_begin_accepts_a_whole_number_of_pages);
    RUN_TEST(test_begin_refuses_an_image_too_short_to_carry_a_trailer);
    RUN_TEST(test_begin_refuses_a_partial_page_and_an_oversized_image);
    RUN_TEST(test_a_whole_image_reaches_flash_in_order);
    RUN_TEST(test_a_chunk_at_the_wrong_offset_is_refused);
    RUN_TEST(test_data_past_the_declared_length_is_refused);
    RUN_TEST(test_data_outside_a_session_is_refused);
    RUN_TEST(test_an_empty_chunk_is_refused);
    RUN_TEST(test_a_truncated_transfer_is_refused_at_end);
    RUN_TEST(test_a_failing_flash_fails_the_session);
    RUN_TEST(test_a_two_page_image_lands_as_exactly_two_page_writes);
    RUN_TEST(test_end_writes_nothing_after_the_last_whole_page);
    RUN_TEST(test_abort_returns_to_idle_and_keeps_no_progress);
    return UNITY_END();
}
