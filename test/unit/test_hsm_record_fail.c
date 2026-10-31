/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * A6, the record over a store that fails: a head read and an erase that
 * return an error reach the record caller's return value. The store here is
 * a fake linked in place of hsm_store_ram.c.
 */

#include "unity.h"
#include "hsm_record.h"
#include "hsm_store.h"

static tfw_status_t g_head_get_st;
static tfw_status_t g_erase_st;
static unsigned     g_puts;

tfw_status_t tfw_hsm_store_head_get(tfw_hsm_store_head_t *out)
{
    if (g_head_get_st != TFW_OK) {
        return g_head_get_st;
    }
    out->n       = 3u;
    out->spare   = 0u;
    out->dropped = 7u;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_head_put(const tfw_hsm_store_head_t *head)
{
    (void)head;
    g_puts++;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_get(uint8_t slot, tfw_hsm_entry_t *out)
{
    (void)slot;
    (void)out;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_put(uint8_t slot, const tfw_hsm_entry_t *e)
{
    (void)slot;
    (void)e;
    g_puts++;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_erase(void)
{
    return g_erase_st;
}

void setUp(void)
{
    g_head_get_st = TFW_OK;
    g_erase_st    = TFW_OK;
    g_puts        = 0u;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_head_read_fails_the_append_and_keeps_nothing);
    RUN_TEST(test_a_failed_head_read_fails_the_read);
    RUN_TEST(test_a_failed_head_read_counts_as_none);
    RUN_TEST(test_a_working_head_read_is_unchanged);
    RUN_TEST(test_a_failed_erase_reaches_the_caller);
    return UNITY_END();
}
