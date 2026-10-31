/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * A6, the record (docs/hsm-self-defence/m7-design.md slice 11a), over the RAM
 * store: an empty record, append and read-back, and what a full one does.
 */

#include <string.h>
#include "unity.h"
#include "hsm.h"
#include "hsm_record.h"
#include "hsm_store.h"

void setUp(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_erase());
}

void tearDown(void) {}

static void fill(void)
{
    for (uint8_t i = 0u; i < TFW_HSM_RECORD_MAX; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, i, TFW_OK, 100u + i));
    }
}

static void test_an_empty_record_holds_nothing(void)
{
    tfw_hsm_entry_t e;

    memset(&e, 0xA5, sizeof e);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_hsm_record_dropped());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(0u, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(0xFFu, &e));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, e.kind);
}

static void test_each_kind_reads_back_in_order(void)
{
    tfw_hsm_entry_t e;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, TFW_HSM_SIG_TAMPER_PIN,
                                                     TFW_OK, 10u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_append(TFW_HSM_REC_DECISION,
                                                     TFW_HSM_VERDICT_COMPROMISED, TFW_OK, 11u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_append(TFW_HSM_REC_ACTION, TFW_HSM_ACT_PHY_OFF,
                                                     TFW_ERR_UNSUPPORTED, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_hsm_record_count());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(0u, &e));
    TEST_ASSERT_EQUAL_UINT32(10u, e.at);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_REC_SIGNAL, e.kind);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_TAMPER_PIN, e.what);
    TEST_ASSERT_EQUAL_UINT8(TFW_OK, e.result);
    TEST_ASSERT_EQUAL_UINT8(0u, e.spare);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(1u, &e));
    TEST_ASSERT_EQUAL_UINT32(11u, e.at);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_REC_DECISION, e.kind);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_VERDICT_COMPROMISED, e.what);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(2u, &e));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, e.at);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_REC_ACTION, e.kind);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_PHY_OFF, e.what);
    TEST_ASSERT_EQUAL_UINT8(TFW_ERR_UNSUPPORTED, e.result);

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(3u, &e));
}

static void test_a_kind_not_defined_is_refused_and_not_kept(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hsm_record_append(0u, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hsm_record_append(4u, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hsm_record_append(0xFFu, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_hsm_record_dropped());
}

/* Full, the record keeps its first entries, refuses the next and counts it. */
static void test_a_full_record_keeps_the_first_and_counts_the_rest(void)
{
    tfw_hsm_entry_t e;

    fill();
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_RECORD_MAX, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_hsm_record_dropped());

    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, tfw_hsm_record_append(TFW_HSM_REC_ACTION,
                                                             TFW_HSM_ACT_SHUTDOWN, TFW_OK, 999u));
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL,
                                                             TFW_HSM_SIG_HOTPLUG, TFW_OK, 1000u));
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_RECORD_MAX, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(2u, tfw_hsm_record_dropped());

    for (uint8_t i = 0u; i < TFW_HSM_RECORD_MAX; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_read(i, &e));
        TEST_ASSERT_EQUAL_UINT8(TFW_HSM_REC_SIGNAL, e.kind);
        TEST_ASSERT_EQUAL_UINT8(i, e.what);
        TEST_ASSERT_EQUAL_UINT32(100u + i, e.at);
    }
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(TFW_HSM_RECORD_MAX, &e));
}

/* The dropped count stops at its top rather than wrapping to none. */
static void test_the_dropped_count_saturates(void)
{
    fill();
    for (uint32_t i = 0u; i < TFW_HSM_RECORD_DROPPED_MAX + 2u; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT,
                          tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, 1u, TFW_OK, i));
    }
    TEST_ASSERT_EQUAL_UINT16(TFW_HSM_RECORD_DROPPED_MAX, tfw_hsm_record_dropped());
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_RECORD_MAX, tfw_hsm_record_count());
}

/* A head past the bound reads as full, never as a slot past the store. */
static void test_a_head_past_the_bound_reads_as_full(void)
{
    tfw_hsm_store_head_t head = { 0xFFu, 0u, 0u };
    tfw_hsm_entry_t      e;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_store_head_put(&head));
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_RECORD_MAX, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(TFW_HSM_RECORD_MAX, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, 1u, TFW_OK, 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, tfw_hsm_record_dropped());
}

static void test_erase_empties_the_record_and_its_count(void)
{
    tfw_hsm_entry_t e;

    fill();
    (void)tfw_hsm_record_append(TFW_HSM_REC_SIGNAL, 1u, TFW_OK, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_record_erase());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hsm_record_count());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_hsm_record_dropped());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_record_read(0u, &e));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_store_get(0u, &e));
    TEST_ASSERT_EQUAL_UINT8(0u, e.kind);
}

/* The RAM store's own bound. */
static void test_the_store_refuses_a_slot_past_its_bound(void)
{
    tfw_hsm_entry_t e = { 1u, TFW_HSM_REC_SIGNAL, 1u, TFW_OK, 0u };

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_store_put(TFW_HSM_RECORD_MAX, &e));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_store_get(TFW_HSM_RECORD_MAX, &e));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_store_put(TFW_HSM_RECORD_MAX - 1u, &e));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_empty_record_holds_nothing);
    RUN_TEST(test_each_kind_reads_back_in_order);
    RUN_TEST(test_a_kind_not_defined_is_refused_and_not_kept);
    RUN_TEST(test_a_full_record_keeps_the_first_and_counts_the_rest);
    RUN_TEST(test_the_dropped_count_saturates);
    RUN_TEST(test_a_head_past_the_bound_reads_as_full);
    RUN_TEST(test_erase_empties_the_record_and_its_count);
    RUN_TEST(test_the_store_refuses_a_slot_past_its_bound);
    return UNITY_END();
}
