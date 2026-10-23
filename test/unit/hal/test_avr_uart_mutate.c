/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_uart.c alone (#163), register-level over test/mock/avr_regs's
 * <avr/io.h> and <avr/interrupt.h>. The two interrupts run by calling their
 * bodies by the board profile's vector names, so the suite plays the USART:
 * it puts a received byte in UDR before the receive interrupt and reads what
 * the data-register-empty interrupt left there. Built for a USART0 board and
 * for the 32U4's USART1; the USART the board does not use must never move.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "avr/io.h"

#define RING 64u     /* the unit's default, which no board here overrides */

void TFW_UART_RX_VECT(void);
void TFW_UART_UDRE_VECT(void);

/* The board's USART block, in <avr/io.h>'s order, and the other one. */
enum { R_UDR, R_UCSRA, R_UCSRB, R_UCSRC, R_UBRRH, R_UBRRL, R_COUNT };

static const unsigned mine  = TFW_UART_N ? TFW_AVR_UDR1 : TFW_AVR_UDR0;
static const unsigned other = TFW_UART_N ? TFW_AVR_UDR0 : TFW_AVR_UDR1;

static uint8_t regs[TFW_AVR_REGS];

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE(id < TFW_AVR_REGS);
    TEST_ASSERT_TRUE(id < other || id >= other + R_COUNT);
    return &regs[id];
}

static uint8_t *reg(unsigned r)
{
    return &regs[mine + r];
}

#define UDRIE 5u
#define TXC   6u
#define U2X   1u

static void rx_isr(uint8_t b)
{
    *reg(R_UDR) = b;
    TFW_UART_RX_VECT();
}

/* One data-register-empty interrupt: what it wrote to UDR, or -1 for none. */
static int udre_isr(void)
{
    *reg(R_UDR) = 0xC9u;    /* sent by no test */
    TFW_UART_UDRE_VECT();
    return *reg(R_UDR) == 0xC9u ? -1 : *reg(R_UDR);
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200uL));
}

void tearDown(void)
{
    unsigned i;

    for (i = 0u; i < R_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, regs[other + i]);
    }
}

static void check_init(uint32_t baud, uint16_t ubrr)
{
    memset(regs, 0xFF, sizeof regs);
    memset(&regs[other], 0, R_COUNT);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(baud));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(ubrr >> 8), *reg(R_UBRRH));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)ubrr, *reg(R_UBRRL));
    TEST_ASSERT_EQUAL_HEX8(0x02u, *reg(R_UCSRA));   /* U2X alone: clears TXC and the error flags' writes */
    TEST_ASSERT_EQUAL_HEX8(0x06u, *reg(R_UCSRC));   /* UCSZ1:0, 8N1 */
    TEST_ASSERT_EQUAL_HEX8(0x98u, *reg(R_UCSRB));   /* RXCIE, RXEN, TXEN; UDRIE off */
}

/* Double speed at 16 MHz: UBRR = F_CPU / (8 * baud) - 1, truncated. */
static void test_init_programs_divisor_and_frame(void)
{
    check_init(115200uL, 16u);
    check_init(57600uL, 33u);
    check_init(9600uL, 207u);
    check_init(2400uL, 832u);    /* high byte 3 */
    check_init(300uL, 6665u);    /* 0x1A09 */
    check_init(1000000uL, 1u);
    check_init(2000000uL, 0u);
}

static void test_receive_in_order_and_empty_leaves_out_alone(void)
{
    uint8_t  b = 0xEEu;
    unsigned i;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, b);
    rx_isr(0x00u);
    rx_isr(0x7Fu);
    rx_isr(0xFFu);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x00u, b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, b);
    b = 0xEEu;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, b);
    /* Many laps of the ring, a few bytes in flight at a time. */
    for (i = 0u; i < 5u * RING; i++) {
        uint8_t got;

        rx_isr((uint8_t)(i * 7u));
        if (i % 3u == 2u) {
            unsigned k;

            for (k = 3u; k > 0u; k--) {
                TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&got));
                TEST_ASSERT_EQUAL_HEX8((uint8_t)((i + 1u - k) * 7u), got);
            }
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&got));
        }
    }
}

/* A full ring holds RING-1 bytes and drops what arrives after, keeping the oldest. */
static void test_full_receive_ring_drops_the_newest(void)
{
    uint8_t  b;
    unsigned i;

    for (i = 0u; i < RING + 5u; i++) {
        rx_isr((uint8_t)(0x80u + i));
    }
    for (i = 0u; i < RING - 1u; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x80u + i), b);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    /* And it works again once drained. */
    rx_isr(0x42u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x42u, b);
}

static void test_transmit_arms_udrie_sends_in_order_then_disarms(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x98u, *reg(R_UCSRB));
    TEST_ASSERT_EQUAL_INT(-1, udre_isr());                     /* nothing queued */
    TEST_ASSERT_EQUAL_HEX8(0x98u, *reg(R_UCSRB));

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x01u));
    TEST_ASSERT_EQUAL_HEX8(0x98u | (1u << UDRIE), *reg(R_UCSRB));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xFEu));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x00u));
    TEST_ASSERT_EQUAL_INT(0x01, udre_isr());
    TEST_ASSERT_EQUAL_INT(0xFE, udre_isr());
    TEST_ASSERT_EQUAL_INT(0x00, udre_isr());
    TEST_ASSERT_EQUAL_HEX8(0x98u | (1u << UDRIE), *reg(R_UCSRB));
    TEST_ASSERT_EQUAL_INT(-1, udre_isr());                     /* drained: disarm only UDRIE */
    TEST_ASSERT_EQUAL_HEX8(0x98u, *reg(R_UCSRB));
    TEST_ASSERT_EQUAL_INT(-1, udre_isr());
}

/* The interrupt enable is a read-modify-write: other UCSRB bits survive both ways. */
static void test_udrie_edits_keep_the_other_bits(void)
{
    *reg(R_UCSRB) = 0x00u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x33u));
    TEST_ASSERT_EQUAL_HEX8(1u << UDRIE, *reg(R_UCSRB));
    TEST_ASSERT_EQUAL_INT(0x33, udre_isr());
    *reg(R_UCSRB) = 0xFFu;
    TEST_ASSERT_EQUAL_INT(-1, udre_isr());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)~(1u << UDRIE), *reg(R_UCSRB));
}

static void test_transmit_ring_holds_ring_minus_one(void)
{
    unsigned i, lap;

    for (lap = 0u; lap < 3u; lap++) {
        for (i = 0u; i < RING - 1u; i++) {
            TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)(lap * 50u + i)));
        }
        *reg(R_UCSRB) = 0x98u;
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xAAu));    /* full */
        TEST_ASSERT_EQUAL_HEX8(0x98u, *reg(R_UCSRB));              /* refused: not armed */
        for (i = 0u; i < RING - 1u; i++) {
            TEST_ASSERT_EQUAL_INT((int)(uint8_t)(lap * 50u + i), udre_isr());
        }
        TEST_ASSERT_EQUAL_INT(-1, udre_isr());
        /* One out frees exactly one. */
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x10u));
        TEST_ASSERT_EQUAL_INT(0x10, udre_isr());
    }
}

static void test_idle_needs_both_an_empty_ring_and_txc(void)
{
    *reg(R_UCSRA) = 0x00u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    *reg(R_UCSRA) = (uint8_t)(1u << TXC);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    *reg(R_UCSRA) = (uint8_t)~(1u << TXC);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    *reg(R_UCSRA) = 0xFFu;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x01u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_INT(0x01, udre_isr());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

/* init empties both rings, whatever was in flight. */
static void test_init_empties_both_rings(void)
{
    uint8_t b;

    rx_isr(0x11u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x22u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200uL));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_INT(-1, udre_isr());
    *reg(R_UCSRA) = (uint8_t)(1u << TXC);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_programs_divisor_and_frame);
    RUN_TEST(test_receive_in_order_and_empty_leaves_out_alone);
    RUN_TEST(test_full_receive_ring_drops_the_newest);
    RUN_TEST(test_transmit_arms_udrie_sends_in_order_then_disarms);
    RUN_TEST(test_udrie_edits_keep_the_other_bits);
    RUN_TEST(test_transmit_ring_holds_ring_minus_one);
    RUN_TEST(test_idle_needs_both_an_empty_ring_and_txc);
    RUN_TEST(test_init_empties_both_rings);
    return UNITY_END();
}
