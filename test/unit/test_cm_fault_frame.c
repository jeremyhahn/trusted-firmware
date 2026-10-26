/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M fault record (row cortex-m, #161): what tfw_cm_fault keeps of
 * the faulting frame across its reset, src/hal/cortex-m/hal_fault_frame.h.
 *
 * The frame is the core's basic exception frame, r0-r3, r12, LR, the return
 * address and xPSR from the stack pointer up (ARMv7-M ARM DDI 0403E, B1.5.6),
 * built here as words in a host array. The record is read back as a debugger
 * reads it: PC, LR, xPSR, in that order. A stack pointer whose frame would
 * leave the image's SRAM is not read, and the record says so.
 */
#include <stdint.h>
#include "unity.h"
#include "hal_mock.h"
#include "fault_frame_model.h"

/* A stack with room above and below the frame, so a frame at either edge is
 * still inside the array. */
static uint32_t stack[16];
static volatile uint32_t rec[3];

void setUp(void)
{
    tfw_mock_reset();
    for (unsigned i = 0; i < 16u; i++) {
        stack[i] = 0xA5A50000uL + i;
    }
    rec[0] = rec[1] = rec[2] = 0u;
}
void tearDown(void) {}

static uintptr_t at(unsigned word) { return (uintptr_t)&stack[word]; }

static void frame_at(unsigned word, uint32_t lr, uint32_t pc, uint32_t xpsr)
{
    stack[word + 5u] = lr;
    stack[word + 6u] = pc;
    stack[word + 7u] = xpsr;
}

static void test_the_record_keeps_pc_lr_and_xpsr(void)
{
    frame_at(4u, 0x00012345uL, 0xE0000000uL, 0x01000003uL);
    cm_model_fault_keep(rec, at(4u), at(0u), at(16u));
    TEST_ASSERT_EQUAL_HEX32(0xE0000000uL, rec[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00012345uL, rec[1]);
    TEST_ASSERT_EQUAL_HEX32(0x01000003uL, rec[2]);
}

static void test_the_record_words_are_pc_lr_xpsr(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, TFW_CM_FAULT_PC);
    TEST_ASSERT_EQUAL_UINT(1u, TFW_CM_FAULT_LR);
    TEST_ASSERT_EQUAL_UINT(2u, TFW_CM_FAULT_XPSR);
    TEST_ASSERT_EQUAL_UINT(3u, TFW_CM_FAULT_WORDS);
}

static void test_a_frame_ending_at_the_stack_top_is_kept(void)
{
    frame_at(8u, 0x11111111uL, 0x22222222uL, 0x33333333uL);
    cm_model_fault_keep(rec, at(8u), at(0u), at(16u));
    TEST_ASSERT_EQUAL_HEX32(0x22222222uL, rec[0]);
    TEST_ASSERT_EQUAL_HEX32(0x11111111uL, rec[1]);
    TEST_ASSERT_EQUAL_HEX32(0x33333333uL, rec[2]);
}

static void test_a_frame_at_the_sram_base_is_kept(void)
{
    frame_at(0u, 0x44444444uL, 0x55555555uL, 0x66666666uL);
    cm_model_fault_keep(rec, at(0u), at(0u), at(16u));
    TEST_ASSERT_EQUAL_HEX32(0x55555555uL, rec[0]);
}

static void assert_no_frame(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, rec[0]);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, rec[1]);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, rec[2]);
}

static void test_a_frame_running_past_the_stack_top_is_not_read(void)
{
    cm_model_fault_keep(rec, at(9u), at(0u), at(16u));
    assert_no_frame();
}

static void test_a_stack_pointer_above_the_stack_top_is_not_read(void)
{
    cm_model_fault_keep(rec, at(12u), at(0u), at(8u));
    assert_no_frame();
}

static void test_a_stack_pointer_below_the_sram_is_not_read(void)
{
    cm_model_fault_keep(rec, at(2u), at(4u), at(16u));
    assert_no_frame();
}

static void test_a_misaligned_stack_pointer_is_not_read(void)
{
    cm_model_fault_keep(rec, at(4u) + 2u, at(0u), at(16u));
    assert_no_frame();
}

static void test_an_unused_process_stack_is_not_read(void)
{
    /* PSP is 0 until something sets it, and neither image does. */
    cm_model_fault_keep(rec, 0u, at(0u), at(16u));
    assert_no_frame();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_record_keeps_pc_lr_and_xpsr);
    RUN_TEST(test_the_record_words_are_pc_lr_xpsr);
    RUN_TEST(test_a_frame_ending_at_the_stack_top_is_kept);
    RUN_TEST(test_a_frame_at_the_sram_base_is_kept);
    RUN_TEST(test_a_frame_running_past_the_stack_top_is_not_read);
    RUN_TEST(test_a_stack_pointer_above_the_stack_top_is_not_read);
    RUN_TEST(test_a_stack_pointer_below_the_sram_is_not_read);
    RUN_TEST(test_a_misaligned_stack_pointer_is_not_read);
    RUN_TEST(test_an_unused_process_stack_is_not_read);
    return UNITY_END();
}
