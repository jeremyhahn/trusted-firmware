/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_sys.c, register-level (#163): the clock prescaler's timed
 * pair, the reset cause (tfw-boot's handoff first, the .init3 latch after),
 * the watchdog reset, the interrupt flag, the microsecond spin, free SRAM and
 * the watchdog arm and pet.
 *
 * cli, sei, wdt_enable, wdt_disable and wdt_reset are calls into this suite,
 * which logs them in order. tfw_hal_reset() never returns: the stand-in
 * wdt_enable() longjmps out of the 15 ms arm, the reset it asks for.
 *
 * tfw_hal_free_ram() subtracts two addresses as ints. A host's stack and its
 * .bss are far apart, and the int difference of two 64-bit addresses can
 * overflow, so the measure runs in a signal handler on a stack that is an
 * array in .bss beside __heap_start: every address it subtracts is then a
 * few kilobytes from the others.
 */
#define _DEFAULT_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"
#include "avr/wdt.h"

static uint8_t regs[TFW_AVR_REGS];

/* ------------------------------------------------------- CLKPR writes */

static uint8_t  clkpr_log[8];
static unsigned clkpr_n;
static int      clkpr_pending;

static void clkpr_flush(void)
{
    if (clkpr_pending) {
        TEST_ASSERT_TRUE(clkpr_n < 8u);
        clkpr_log[clkpr_n++] = regs[TFW_AVR_CLKPR];
        regs[TFW_AVR_CLKPR]  = 0xA5u;          /* a write that is skipped shows */
        clkpr_pending        = 0;
    }
}

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    clkpr_flush();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TFW_AVR_CLKPR, id, "hal_sys.c touched another register");
    clkpr_pending = 1;
    return &regs[id];
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_sys.c reached a register by address");
    return 0;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_sys.c made a word access");
    return 0;
}

/* ------------------------------------------------ calls, in order */

static char     calls[16];
static unsigned ncalls;
static jmp_buf  reset_jmp;
static int      in_reset;
static uint32_t delayed, delays;

static void log_call(char c)
{
    TEST_ASSERT_TRUE(ncalls < sizeof calls - 1u);
    calls[ncalls++] = c;
}

void cli(void) { log_call('c'); }
void sei(void) { log_call('s'); }
void wdt_disable(void) { log_call('d'); }
void wdt_reset(void) { log_call('p'); }

void wdt_enable(unsigned char timeout)
{
    log_call(timeout == WDTO_15MS ? 'R' : timeout == WDTO_8S ? 'A' : '?');
    if (in_reset && timeout == WDTO_15MS) {
        longjmp(reset_jmp, 1);
    }
}

void tfw_avr_delay_us(double us)
{
    TEST_ASSERT_TRUE_MESSAGE(us == 1.0, "a step other than 1 us");
    delayed += (uint32_t)us;
    delays++;
}

/* ------------------------------------------------ the reset cause */

uint8_t        tfw_hal_early_flags;
static uint8_t hand_ok, hand_val;

uint8_t tfw_boot_handoff_reset_cause(uint8_t *out)
{
    if (hand_ok) {
        *out = hand_val;
    }
    return hand_ok;
}

/* ------------------------------------------------------ free SRAM */

int  __heap_start;
int *__brkval;

/* Under 64 KiB, so a measure within it is not cut by the u16 narrowing. */
static _Alignas(16) unsigned char alt_stack[60000u];
static int     *const *heap_tops;
static uint32_t       *measured;
static unsigned        nmeasures;

/* AddressSanitizer's hook, read at start-up and an unused function in any
 * other build: no fake stack, so tfw_hal_free_ram()'s marker is a real frame
 * on alt_stack under test-asan too, not a fresh heap slot on every call. */
const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
    return "detect_stack_use_after_return=0";
}

/* Every measure from one handler frame: a sanitizer may place the frame
 * differently on each delivery, so &marker is the same only within one. */
static void measure(int sig)
{
    unsigned i;

    (void)sig;
    for (i = 0u; i < nmeasures; i++) {
        __brkval    = heap_tops[i];
        measured[i] = tfw_hal_free_ram();
    }
}

/* tfw_hal_free_ram() run on alt_stack, once with __brkval at each of tops. */
static void free_ram_with(int *const *tops, uint32_t *out, unsigned n)
{
    stack_t          ss, old;
    struct sigaction sa, old_sa;
    unsigned         i;

    for (i = 0u; i < n; i++) {
        out[i] = 0xDEADBEEFu;
    }
    heap_tops = tops;
    measured  = out;
    nmeasures = n;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp   = alt_stack;
    ss.ss_size = sizeof alt_stack;
    TEST_ASSERT_EQUAL_INT(0, sigaltstack(&ss, &old));
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = measure;
    sa.sa_flags   = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGUSR1, &sa, &old_sa));
    TEST_ASSERT_EQUAL_INT(0, raise(SIGUSR1));
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGUSR1, &old_sa, 0));
    TEST_ASSERT_EQUAL_INT(0, sigaltstack(&old, 0));
}

void setUp(void)
{
    memset(regs, 0x5A, sizeof regs);
    clkpr_n = 0u;
    clkpr_pending = 0;
    memset(calls, 0, sizeof calls);
    ncalls = 0u;
    in_reset = 0;
    delayed = delays = 0u;
    hand_ok = hand_val = 0u;
    tfw_hal_early_flags = 0u;
}

void tearDown(void) {}

/* ------------------------------------------------------------ tests */

static void test_init_unlocks_then_clears_the_prescaler(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_init());
    clkpr_flush();
    TEST_ASSERT_EQUAL_UINT(2u, clkpr_n);
    TEST_ASSERT_EQUAL_HEX8(0x80u, clkpr_log[0]);       /* CLKPCE alone */
    TEST_ASSERT_EQUAL_HEX8(0x00u, clkpr_log[1]);       /* divide by one */
    TEST_ASSERT_EQUAL_UINT(0u, ncalls);
}

static void test_reset_cause_prefers_the_handoff(void)
{
    hand_ok             = 1u;
    hand_val            = (uint8_t)TFW_RESET_BROWNOUT;
    tfw_hal_early_flags = 0x01u;                     /* the latch says power-on */
    TEST_ASSERT_EQUAL_INT(TFW_RESET_BROWNOUT, tfw_hal_reset_cause());

    hand_val = (uint8_t)TFW_RESET_UNKNOWN;          /* handed off, and unknown */
    TEST_ASSERT_EQUAL_INT(TFW_RESET_UNKNOWN, tfw_hal_reset_cause());
}

static void test_reset_cause_falls_back_to_the_latch(void)
{
    static const struct { uint8_t mcusr; tfw_reset_cause_t want; } c[] = {
        { 0x00u, TFW_RESET_UNKNOWN },  { 0x01u, TFW_RESET_POWER_ON },
        { 0x02u, TFW_RESET_EXTERNAL }, { 0x04u, TFW_RESET_BROWNOUT },
        { 0x08u, TFW_RESET_WATCHDOG }, { 0x09u, TFW_RESET_POWER_ON },
        { 0x0Fu, TFW_RESET_BROWNOUT }, { 0xF0u, TFW_RESET_UNKNOWN },
    };
    unsigned i;

    hand_val = (uint8_t)TFW_RESET_WATCHDOG;          /* written nowhere: not handed off */
    for (i = 0u; i < sizeof c / sizeof c[0]; i++) {
        tfw_hal_early_flags = c[i].mcusr;
        TEST_ASSERT_EQUAL_INT(c[i].want, tfw_hal_reset_cause());
    }
}

static void test_reset_disarms_then_arms_15ms_with_interrupts_off(void)
{
    in_reset = 1;
    if (setjmp(reset_jmp) == 0) {
        tfw_hal_reset();
        TEST_FAIL_MESSAGE("tfw_hal_reset returned");
    }
    TEST_ASSERT_EQUAL_STRING("cdR", calls);
    TEST_ASSERT_EQUAL_UINT(0u, clkpr_n);
}

static void test_irq_enable_is_sei(void)
{
    tfw_hal_irq_enable();
    TEST_ASSERT_EQUAL_STRING("s", calls);
}

static void test_delay_us_is_that_many_microseconds(void)
{
    static const uint16_t us[] = { 0u, 1u, 3u, 1000u, 65535u };
    unsigned              i;

    for (i = 0u; i < sizeof us / sizeof us[0]; i++) {
        delayed = delays = 0u;
        tfw_hal_delay_us(us[i]);
        TEST_ASSERT_EQUAL_UINT32(us[i], delayed);
        TEST_ASSERT_EQUAL_UINT32(us[i], delays);
    }
}

static void test_free_ram_measures_from_the_heap_top(void)
{
    /* Same frame each time, so &marker is the same: the answers differ by
     * exactly how far the heap top moved. */
    int *const tops[4] = { 0, &__heap_start, &__heap_start + 100, &__heap_start + 1000 };
    uint32_t   r[4];
    uint32_t   none, at, up, far;

    free_ram_with(tops, r, 4u);
    none = r[0]; at = r[1]; up = r[2]; far = r[3];
    TEST_ASSERT_TRUE(none <= 0xFFFFu);
    TEST_ASSERT_EQUAL_UINT32(none, at);              /* no heap = heap at its start */
    TEST_ASSERT_EQUAL_UINT32((none - 100u * sizeof(int)) & 0xFFFFu, up);
    TEST_ASSERT_EQUAL_UINT32((none - 1000u * sizeof(int)) & 0xFFFFu, far);
    TEST_ASSERT_TRUE(none != 0u);
}

static void test_free_ram_is_the_gap_to_the_stack(void)
{
    /* With the heap top ON the stack array, the answer is the bytes between
     * it and the measuring frame: positive and within the array. */
    int *const tops[2] = { (int *)(void *)alt_stack, (int *)(void *)(alt_stack + 4096) };
    uint32_t   r[2];
    uint32_t   low, high;

    free_ram_with(tops, r, 2u);
    low  = r[0];
    high = r[1];

    TEST_ASSERT_TRUE(low > 4096u);
    TEST_ASSERT_TRUE(low < sizeof alt_stack);
    TEST_ASSERT_EQUAL_UINT32(0u, low % sizeof(int));  /* marker is an int: aligned */
    TEST_ASSERT_EQUAL_UINT32(low - 4096u, high);
}

static void test_watchdog_arm_is_8s_and_pet_resets(void)
{
    tfw_hal_watchdog_arm();
    tfw_hal_watchdog_pet();
    tfw_hal_watchdog_pet();
    TEST_ASSERT_EQUAL_STRING("App", calls);
    TEST_ASSERT_EQUAL_UINT(0u, clkpr_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_unlocks_then_clears_the_prescaler);
    RUN_TEST(test_reset_cause_prefers_the_handoff);
    RUN_TEST(test_reset_cause_falls_back_to_the_latch);
    RUN_TEST(test_reset_disarms_then_arms_15ms_with_interrupts_off);
    RUN_TEST(test_irq_enable_is_sei);
    RUN_TEST(test_delay_us_is_that_many_microseconds);
    RUN_TEST(test_free_ram_measures_from_the_heap_top);
    RUN_TEST(test_free_ram_is_the_gap_to_the_stack);
    RUN_TEST(test_watchdog_arm_is_8s_and_pet_resets);
    return UNITY_END();
}
