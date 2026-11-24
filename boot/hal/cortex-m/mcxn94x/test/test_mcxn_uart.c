/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled LPUART on the MCXN947 (boot/hal/cortex-m/mcxn94x/
 * mcxn_uart.c, row mcxn947), against a register model: an LP_FLEXCOMM unit's
 * 4 KiB of registers in RAM. The suite checks the registers start writes and
 * their values, that a rate the clock cannot divide to writes none, that a
 * byte is read only when one has arrived and an error flag is cleared on the
 * way, that putc waits for TDRE and stop for TC (a timer signal sets each
 * flag while the call spins), and that stop leaves the unit in its reset.
 *
 * The offsets and bits are pinned as literals: they are the SDK device
 * header's, and a value computed from soc_regs.h would move with it.
 */
/* sigaction and setitimer. */
#define _GNU_SOURCE
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#include "unity.h"
#include "mcxn_uart.h"
#include "mcxn94x/lpuart_baud.h"

#define CLK      12000000uL
#define GLOBAL   (0x008u / 4u)
#define BAUD     (0x010u / 4u)
#define STAT     (0x014u / 4u)
#define CTRL     (0x018u / 4u)
#define DATA     (0x01Cu / 4u)
#define PSELID   (0xFF8u / 4u)

#define STAT_FE   (1uL << 17)
#define STAT_NF   (1uL << 18)
#define STAT_OR   (1uL << 19)
#define STAT_RDRF (1uL << 21)
#define STAT_TC   (1uL << 22)
#define STAT_TDRE (1uL << 23)
#define CTRL_RE   (1uL << 18)
#define CTRL_TE   (1uL << 19)
#define GLOBAL_RST (1uL << 1)

static volatile uint32_t fc[0x1000u / 4u];

/* The flags a spinning call waits on can only change from outside it: a 2 ms
 * timer signal records DATA and CTRL, then sets TDRE at tick 1 and TC at
 * tick 2; from tick 10 every flag is set so a broken wait cannot hang. */
static volatile sig_atomic_t ticks;
static volatile uint32_t seen_data[3], seen_ctrl[3];
static struct sigaction old_action;

static void on_tick(int sig)
{
    (void)sig;
    ticks++;
    if (ticks <= 2) {
        seen_data[ticks] = fc[DATA];
        seen_ctrl[ticks] = fc[CTRL];
    }
    if (ticks == 1) {
        fc[STAT] |= STAT_TDRE;
    } else if (ticks == 2) {
        fc[STAT] |= STAT_TC;
    } else if (ticks >= 10) {
        fc[STAT] = 0xFFFFFFFFu;
    }
}

static void arm(void)
{
    struct sigaction sa;
    struct itimerval it = {{0, 2000}, {0, 2000}};

    sa.sa_handler = on_tick;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGALRM, &sa, &old_action));
    TEST_ASSERT_EQUAL_INT(0, setitimer(ITIMER_REAL, &it, NULL));
}

static void disarm(void)
{
    struct itimerval it = {{0, 0}, {0, 0}};

    TEST_ASSERT_EQUAL_INT(0, setitimer(ITIMER_REAL, &it, NULL));
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGALRM, &old_action, NULL));
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < sizeof fc / sizeof fc[0]; i++) {
        fc[i] = 0u;
    }
    ticks = 0;
}

void tearDown(void) {}

/* 115200 from 12 MHz: the unit is an LPUART, out of its reset, at the BAUD
 * lpuart_baud.c gives, the error flags written to clear, receiver and
 * transmitter on and no interrupt enabled. */
static void test_start_sets_the_unit_up_as_a_polled_lpuart(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_mcxn_lpuart_baud(CLK, 115200u, &reg));
    fc[GLOBAL] = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_mcxn_uart_start(fc, CLK, 115200u));
    TEST_ASSERT_EQUAL_HEX32(1u, fc[PSELID]);
    TEST_ASSERT_EQUAL_HEX32(0u, fc[GLOBAL]);
    TEST_ASSERT_EQUAL_HEX32(reg, fc[BAUD]);
    TEST_ASSERT_EQUAL_HEX32(STAT_OR | STAT_NF | STAT_FE, fc[STAT]);
    TEST_ASSERT_EQUAL_HEX32(CTRL_RE | CTRL_TE, fc[CTRL]);
}

/* A rate no setting comes within 3% of writes no register. */
static void test_a_rate_the_clock_cannot_divide_writes_nothing(void)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_mcxn_uart_start(fc, CLK, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_mcxn_uart_start(fc, CLK, 6000000u));
    for (i = 0u; i < sizeof fc / sizeof fc[0]; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, fc[i]);
    }
}

/* No byte until RDRF; then the low byte of DATA. */
static void test_getc_reads_only_an_arrived_byte(void)
{
    uint8_t b = 0x11u;

    fc[DATA] = 0x1A5u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mcxn_uart_getc(fc, &b));
    TEST_ASSERT_EQUAL_HEX8(0x11u, b);
    fc[STAT] = STAT_RDRF | STAT_TDRE;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mcxn_uart_getc(fc, &b));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b);
}

/* An overrun, noise or framing flag is written back to clear it, and only
 * those flags: an overrun left set would stop the receiver for good. */
static void test_getc_clears_an_error_flag(void)
{
    uint8_t b = 0u;

    fc[STAT] = STAT_OR | STAT_FE | STAT_TDRE;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mcxn_uart_getc(fc, &b));
    TEST_ASSERT_EQUAL_HEX32(STAT_OR | STAT_FE, fc[STAT]);

    fc[STAT] = STAT_NF | STAT_RDRF;
    fc[DATA] = 0x42u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mcxn_uart_getc(fc, &b));
    TEST_ASSERT_EQUAL_HEX8(0x42u, b);
    TEST_ASSERT_EQUAL_HEX32(STAT_NF, fc[STAT]);
}

/* With room in the transmitter the byte goes to DATA. */
static void test_putc_writes_data(void)
{
    fc[STAT] = STAT_TDRE;
    tfw_mcxn_uart_putc(fc, 0x7Eu);
    TEST_ASSERT_EQUAL_HEX32(0x7Eu, fc[DATA]);
}

/* Once the shifter is empty: receiver and transmitter off, the LPUART held
 * in its reset, the unit no longer an LPUART. */
static void test_stop_leaves_the_unit_in_reset(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_mcxn_uart_start(fc, CLK, 115200u));
    fc[STAT] = STAT_TC | STAT_TDRE;
    tfw_mcxn_uart_stop(fc);
    TEST_ASSERT_EQUAL_HEX32(0u, fc[CTRL]);
    TEST_ASSERT_EQUAL_HEX32(GLOBAL_RST, fc[GLOBAL]);
    TEST_ASSERT_EQUAL_HEX32(0u, fc[PSELID]);
}

/* No room yet: putc spins until TDRE and only then writes, so DATA still
 * holds the byte before it when the flag is set. */
static void test_putc_waits_for_room_before_it_writes(void)
{
    fc[DATA] = 0x11u;
    arm();
    tfw_mcxn_uart_putc(fc, 0x7Eu);
    disarm();
    TEST_ASSERT_TRUE(ticks >= 1);
    TEST_ASSERT_EQUAL_HEX32(0x11u, seen_data[1]);
    TEST_ASSERT_EQUAL_HEX32(0x7Eu, fc[DATA]);
}

/* An empty buffer is not an empty shifter: stop keeps the unit running
 * through TDRE and resets it only on TC, so the last byte goes out whole. */
static void test_stop_waits_for_the_shifter_not_the_buffer(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_mcxn_uart_start(fc, CLK, 115200u));
    fc[STAT] = 0u;
    arm();
    tfw_mcxn_uart_stop(fc);
    disarm();
    TEST_ASSERT_TRUE(ticks >= 2);
    TEST_ASSERT_EQUAL_HEX32(CTRL_RE | CTRL_TE, seen_ctrl[2]);
    TEST_ASSERT_EQUAL_HEX32(0u, fc[CTRL]);
    TEST_ASSERT_EQUAL_HEX32(GLOBAL_RST, fc[GLOBAL]);
    TEST_ASSERT_EQUAL_HEX32(0u, fc[PSELID]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_sets_the_unit_up_as_a_polled_lpuart);
    RUN_TEST(test_a_rate_the_clock_cannot_divide_writes_nothing);
    RUN_TEST(test_getc_reads_only_an_arrived_byte);
    RUN_TEST(test_getc_clears_an_error_flag);
    RUN_TEST(test_putc_writes_data);
    RUN_TEST(test_stop_leaves_the_unit_in_reset);
    RUN_TEST(test_putc_waits_for_room_before_it_writes);
    RUN_TEST(test_stop_waits_for_the_shifter_not_the_buffer);
    return UNITY_END();
}
