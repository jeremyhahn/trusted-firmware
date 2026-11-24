/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/cortex-m/hal_sys.c on the host (row cortex-m, #458): free SRAM,
 * the reset cause (tfw-boot's handoff first, the reset handler's latch
 * after, every cause tfw-boot hands off, UNKNOWN included, back unchanged),
 * the reset, the watchdog arm and pet, and tfw_hal_init(), which is TFW_OK.
 *
 * The linker symbol __heap_start is a variable here; the core's reset, the
 * SoC's watchdog and the latch are test/model/cortex-m/cm_sys_model.c, which
 * reports each call to this suite to log in order. The reset never returns:
 * the model longjmps out.
 *
 * tfw_hal_free_ram() is the stack address less __heap_start, cut to 32 bits.
 * A host's stack and its .bss are far apart, so the suite checks the answer
 * modulo 2^32: against the caller's frame address, and against itself from
 * a frame a known depth further down the stack.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "cm_sys_model.h"

/* ------------------------------------------------ calls, in order */

static char    calls[16];
static unsigned ncalls;

void cm_sys_model_call(char c)
{
    TEST_ASSERT_TRUE(ncalls < sizeof calls - 1u);
    calls[ncalls++] = c;
}

/* ------------------------------------------------ the reset cause */

static uint8_t hand_ok, hand_val;

uint8_t tfw_boot_handoff_reset_cause(uint8_t *out)
{
    if (hand_ok) {
        *out = hand_val;
    }
    return hand_ok;
}

/* ------------------------------------------------------ free SRAM */

uint32_t __heap_start;

/* AddressSanitizer's hook, read at start-up and an unused function in any
 * other build: no fake stack, so tfw_hal_free_ram()'s marker is a real frame
 * below its caller's under test-asan too, not a heap slot. */
const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
    return "detect_stack_use_after_return=0";
}

/* The answer and what it must be near: this frame's address less
 * __heap_start. The marker lies in the callee's frame, just below. */
__attribute__((noinline)) static uint32_t free_ram_here(uint32_t *frame_gap)
{
    *frame_gap = (uint32_t)((uintptr_t)__builtin_frame_address(0) - (uintptr_t)&__heap_start);
    return tfw_hal_free_ram();
}

#define DEEPER 4096u

/* The same measure from DEEPER bytes further down the stack. */
__attribute__((noinline)) static uint32_t free_ram_deeper(void)
{
    volatile unsigned char pad[DEEPER];
    uint32_t               gap;

    pad[0]          = 1u;
    pad[DEEPER - 1] = 1u;
    return free_ram_here(&gap) + (uint32_t)(pad[0] - pad[DEEPER - 1]);
}

void setUp(void)
{
    memset(calls, 0, sizeof calls);
    ncalls   = 0u;
    hand_ok  = hand_val = 0u;
    cm_sys_model_latched = TFW_RESET_UNKNOWN;
}

void tearDown(void) {}

/* ------------------------------------------------------------ tests */

static void test_free_ram_is_the_stack_less_the_heap_start(void)
{
    uint32_t gap  = 0u;
    uint32_t got = free_ram_here(&gap);

    /* The marker is below the frame, by at most a frame and its redzones. */
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)(gap - got) < 1024u, "not the stack less __heap_start");
    TEST_ASSERT_TRUE(got != 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, got % sizeof(uint32_t)); /* marker is a word */
    TEST_ASSERT_EQUAL_UINT(0u, ncalls);
}

static void test_a_deeper_stack_leaves_less_free_ram(void)
{
    uint32_t gap;
    uint32_t shallow = free_ram_here(&gap);
    uint32_t deep    = free_ram_deeper();
    uint32_t used    = (uint32_t)(shallow - deep);

    TEST_ASSERT_TRUE_MESSAGE(used >= DEEPER, "the deeper frame did not count");
    TEST_ASSERT_TRUE(used < DEEPER + 1024u);
}

static void test_reset_cause_prefers_the_handoff(void)
{
    hand_ok  = 1u;
    hand_val = (uint8_t)TFW_RESET_WATCHDOG;
    cm_sys_model_latched = TFW_RESET_POWER_ON;
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_STRING("", calls);              /* the latch not asked */
}

/* Each cause on the wire, UNKNOWN (0) to PANIC (6), with the latch holding
 * another: what tfw-boot handed off comes back as it was, the latch unasked. */
static void test_every_handed_off_cause_comes_back_unchanged(void)
{
    int c;

    for (c = (int)TFW_RESET_UNKNOWN; c <= (int)TFW_RESET_PANIC; c++) {
        memset(calls, 0, sizeof calls);
        ncalls               = 0u;
        hand_ok              = 1u;
        hand_val             = (uint8_t)c;
        cm_sys_model_latched = (tfw_reset_cause_t)((c + 1) % 7);
        TEST_ASSERT_EQUAL_INT_MESSAGE(c, tfw_hal_reset_cause(), "handed-off cause changed");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", calls, "the latch was asked");
    }
}

static void test_init_is_ok(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_init());
    TEST_ASSERT_EQUAL_STRING("", calls);
}

static void test_reset_cause_falls_back_to_the_latch(void)
{
    hand_val = (uint8_t)TFW_RESET_WATCHDOG;           /* written nowhere: not handed off */
    cm_sys_model_latched = TFW_RESET_SOFTWARE;
    TEST_ASSERT_EQUAL_INT(TFW_RESET_SOFTWARE, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_STRING("l", calls);
}

static void test_reset_is_the_system_reset(void)
{
    if (setjmp(cm_sys_model_reset) == 0) {
        tfw_hal_reset();
        TEST_FAIL_MESSAGE("tfw_hal_reset returned");
    }
    TEST_ASSERT_EQUAL_STRING("R", calls);
}

static void test_watchdog_arm_and_pet_are_the_socs(void)
{
    tfw_hal_watchdog_arm();
    tfw_hal_watchdog_pet();
    tfw_hal_watchdog_pet();
    TEST_ASSERT_EQUAL_STRING("App", calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_free_ram_is_the_stack_less_the_heap_start);
    RUN_TEST(test_a_deeper_stack_leaves_less_free_ram);
    RUN_TEST(test_reset_cause_prefers_the_handoff);
    RUN_TEST(test_reset_cause_falls_back_to_the_latch);
    RUN_TEST(test_every_handed_off_cause_comes_back_unchanged);
    RUN_TEST(test_init_is_ok);
    RUN_TEST(test_reset_is_the_system_reset);
    RUN_TEST(test_watchdog_arm_and_pet_are_the_socs);
    return UNITY_END();
}
