/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * A6, the record over a store that fails: a head read, a put, a head put,
 * a get and an erase that return an error reach the record caller's return
 * value. The store here is a fake linked in place of hsm_store_ram.c.
 */

#include <string.h>

#include "unity.h"
#include "hsm_record.h"
#include "hsm_store.h"

static tfw_status_t g_head_get_st;
static tfw_status_t g_head_put_st;
static tfw_status_t g_put_st;
static tfw_status_t g_get_st;
static tfw_status_t g_erase_st;
static uint8_t      g_head_n;
static unsigned     g_puts;
static unsigned     g_head_puts;
static unsigned     g_entry_puts;

/* What the store was handed, and in which order. */
static unsigned             g_seq;
static unsigned             g_entry_put_seq;
static unsigned             g_head_put_seq;
static uint8_t              g_put_slot;
static tfw_hsm_entry_t      g_put_entry;
static tfw_hsm_store_head_t g_head_put;
static unsigned             g_gets;
static uint8_t              g_get_slot;

tfw_status_t tfw_hsm_store_head_get(tfw_hsm_store_head_t *out)
{
    if (g_head_get_st != TFW_OK) {
        return g_head_get_st;
    }
    out->n       = g_head_n;
    out->spare   = 0u;
    out->dropped = 7u;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_head_put(const tfw_hsm_store_head_t *head)
{
    g_head_put     = *head;
    g_head_put_seq = ++g_seq;
    g_puts++;
    g_head_puts++;
    return g_head_put_st;
}

tfw_status_t tfw_hsm_store_get(uint8_t slot, tfw_hsm_entry_t *out)
{
    (void)out;
    g_get_slot = slot;
    g_gets++;
    return g_get_st;
}

tfw_status_t tfw_hsm_store_put(uint8_t slot, const tfw_hsm_entry_t *e)
{
    g_put_slot      = slot;
    g_put_entry     = *e;
    g_entry_put_seq = ++g_seq;
    g_puts++;
    g_entry_puts++;
    return g_put_st;
}

tfw_status_t tfw_hsm_store_erase(void)
{
    return g_erase_st;
}

void setUp(void)
{
    g_head_get_st = TFW_OK;
    g_head_put_st = TFW_OK;
    g_put_st      = TFW_OK;
    g_get_st      = TFW_OK;
    g_erase_st    = TFW_OK;
    g_head_n      = 3u;
    g_puts        = 0u;
    g_head_puts   = 0u;
    g_entry_puts  = 0u;
    g_seq           = 0u;
    g_entry_put_seq = 0u;
    g_head_put_seq  = 0u;
    g_put_slot      = 0xFFu;
    g_gets          = 0u;
    g_get_slot      = 0xFFu;
    memset(&g_put_entry, 0xA5, sizeof(g_put_entry));
    memset(&g_head_put, 0xA5, sizeof(g_head_put));
}

void tearDown(void) {}

static void test_a_failed_head_read_fails_the_append_and_keeps_nothing(void)
{
    g_head_get_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, g_puts);
}

static void test_a_failed_head_read_fails_the_read(void)
{
    tfw_hsm_entry_t e;

    g_head_get_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_read(0u, &e));
}

static void test_a_failed_head_read_counts_as_none(void)
{
    g_head_get_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_hsm_record_dropped());
}

static void test_a_working_head_read_is_unchanged(void)
{
    tfw_hsm_entry_t e;

    TEST_ASSERT_EQUAL_UINT8(3u, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(7u, tfw_hsm_record_dropped());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(2u, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(3u, &e));
}

static void test_a_failed_erase_reaches_the_caller(void)
{
    g_erase_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_erase());
    g_erase_st = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_erase());
}

static void test_a_failed_entry_put_fails_the_append_and_leaves_the_head(void)
{
    g_put_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, g_entry_puts);
    TEST_ASSERT_EQUAL_UINT(0u, g_head_puts);
}

static void test_a_failed_head_put_reaches_the_append(void)
{
    g_head_put_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_append(TFW_HSM_REC_DECISION, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, g_entry_puts);
    TEST_ASSERT_EQUAL_UINT(1u, g_head_puts);
}

static void test_a_failed_get_reaches_the_read(void)
{
    tfw_hsm_entry_t e;

    g_get_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, tfw_hsm_record_read(0u, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(3u, &e));
}

static void test_a_full_record_whose_head_put_fails_is_still_no_slot(void)
{
    g_head_n      = TFW_HSM_RECORD_MAX;
    g_head_put_st = TFW_ERR_NO_STORE;
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, tfw_hsm_record_append(TFW_HSM_REC_ACTION, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, g_entry_puts);
    TEST_ASSERT_EQUAL_UINT(1u, g_head_puts);
}

static void test_an_append_puts_the_entry_at_the_count_then_the_head_that_counts_it(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_append(TFW_HSM_REC_ACTION, 0x21u, 0x05u,
                                                    0x11223344u));
    TEST_ASSERT_EQUAL_UINT(1u, g_entry_puts);
    TEST_ASSERT_EQUAL_UINT(1u, g_head_puts);
    TEST_ASSERT_EQUAL_UINT8(3u, g_put_slot);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, g_put_entry.at);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_REC_ACTION, g_put_entry.kind);
    TEST_ASSERT_EQUAL_HEX8(0x21u, g_put_entry.what);
    TEST_ASSERT_EQUAL_HEX8(0x05u, g_put_entry.result);
    TEST_ASSERT_EQUAL_HEX8(0u, g_put_entry.spare);
    TEST_ASSERT_EQUAL_UINT8(4u, g_head_put.n);
    TEST_ASSERT_EQUAL_HEX8(0u, g_head_put.spare);
    TEST_ASSERT_EQUAL_UINT(7u, g_head_put.dropped);
    /* The entry, then the head that counts it (hsm_store.h). */
    TEST_ASSERT_TRUE(g_entry_put_seq < g_head_put_seq);
}

static void test_a_read_past_the_count_never_reaches_the_store(void)
{
    tfw_hsm_entry_t e;
    tfw_hsm_entry_t was;

    memset(&e, 0xA5, sizeof(e));
    memcpy(&was, &e, sizeof(e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(3u, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(0xFFu, &e));
    TEST_ASSERT_EQUAL_UINT(0u, g_gets);
    TEST_ASSERT_EQUAL_MEMORY(&was, &e, sizeof(e));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(2u, &e));
    TEST_ASSERT_EQUAL_UINT(1u, g_gets);
    TEST_ASSERT_EQUAL_UINT8(2u, g_get_slot);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_head_read_fails_the_append_and_keeps_nothing);
    RUN_TEST(test_a_failed_head_read_fails_the_read);
    RUN_TEST(test_a_failed_head_read_counts_as_none);
    RUN_TEST(test_a_working_head_read_is_unchanged);
    RUN_TEST(test_a_failed_erase_reaches_the_caller);
    RUN_TEST(test_a_failed_entry_put_fails_the_append_and_leaves_the_head);
    RUN_TEST(test_a_failed_head_put_reaches_the_append);
    RUN_TEST(test_a_failed_get_reaches_the_read);
    RUN_TEST(test_a_full_record_whose_head_put_fails_is_still_no_slot);
    RUN_TEST(test_an_append_puts_the_entry_at_the_count_then_the_head_that_counts_it);
    RUN_TEST(test_a_read_past_the_count_never_reaches_the_store);
    return UNITY_END();
}
