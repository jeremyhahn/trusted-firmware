/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/avr/recovery_halt.c, register-level (#163): with BOOT_OTA=0 a
 * refusing device stops -- interrupts off, then power-down, forever -- and a
 * requested session returns at once, touching nothing.
 *
 * cli() and sleep_mode() are calls into this suite. sleep_mode() checks SMCR
 * at the moment the part would sleep, then disturbs the mode bits so the next
 * pass must set them again; the third sleep leaves the endless loop by
 * longjmp, the only way out a test has.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "avr/io.h"
#include "boot_recovery.h"

static uint8_t regs[TFW_AVR_REGS];
static char    calls[16];
static unsigned ncalls, accesses;
static uint8_t smcr_at_sleep[3];
static jmp_buf out;

static void log_call(char c)
{
    TEST_ASSERT_TRUE(ncalls < sizeof calls - 1u);
    calls[ncalls++] = c;
}

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TFW_AVR_SMCR, id, "recovery_halt.c touched a register other than SMCR");
    accesses++;
    return &regs[id];
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("recovery_halt.c reached a register by address");
    return 0;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("recovery_halt.c made a word access");
    return 0;
}

void cli(void) { log_call('c'); }
void sei(void) { log_call('i'); }

void sleep_mode(void)
{
    unsigned n = 0u;
    unsigned i;

    for (i = 0u; i < ncalls; i++) {
        n += calls[i] == 's';
    }
    log_call('s');
    smcr_at_sleep[n] = regs[TFW_AVR_SMCR];
    if (n == 0u) {
        regs[TFW_AVR_SMCR] = 0x0Eu;      /* every mode bit set, nothing else */
    } else if (n == 1u) {
        regs[TFW_AVR_SMCR] = 0x00u;      /* idle */
    } else {
        longjmp(out, 1);
    }
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    memset(calls, 0, sizeof calls);
    memset(smcr_at_sleep, 0xEE, sizeof smcr_at_sleep);
    ncalls = accesses = 0u;
}

void tearDown(void) {}

static void test_recovery_stops_interrupts_off_in_power_down(void)
{
    tfw_boot_platform_t plat;

    memset(&plat, 0, sizeof plat);
    regs[TFW_AVR_SMCR] = 0xFBu;          /* every bit but SM1 */
    if (setjmp(out) == 0) {
        tfw_boot_recovery(&plat, TFW_ERR_RANGE);
    }
    TEST_ASSERT_EQUAL_STRING("csss", calls);    /* cli once, before any sleep */
    TEST_ASSERT_EQUAL_HEX8(0xF5u, smcr_at_sleep[0]);   /* SM = 010, SE and the rest kept */
    TEST_ASSERT_EQUAL_HEX8(0x04u, smcr_at_sleep[1]);   /* SM set again on each pass */
    TEST_ASSERT_EQUAL_HEX8(0x04u, smcr_at_sleep[2]);
}

static void test_recovery_ignores_its_reason(void)
{
    if (setjmp(out) == 0) {
        tfw_boot_recovery(NULL, TFW_OK);
    }
    TEST_ASSERT_EQUAL_STRING("csss", calls);
    TEST_ASSERT_EQUAL_HEX8(0x04u, smcr_at_sleep[0]);
}

static void test_session_open_returns_at_once(void)
{
    tfw_boot_platform_t             plat;
    const tfw_boot_session_limits_t lim = { 1u, 2u, 3u, 4u };

    memset(&plat, 0xA5, sizeof plat);
    regs[TFW_AVR_SMCR] = 0x5Au;
    tfw_boot_session_open(&plat, &lim);
    tfw_boot_session_open(NULL, NULL);
    TEST_ASSERT_EQUAL_UINT(0u, ncalls);
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, regs[TFW_AVR_SMCR]);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, &plat, sizeof plat);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_recovery_stops_interrupts_off_in_power_down);
    RUN_TEST(test_recovery_ignores_its_reason);
    RUN_TEST(test_session_open_returns_at_once);
    return UNITY_END();
}
