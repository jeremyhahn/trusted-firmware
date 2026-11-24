/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's nRF54L15 UART (boot/hal/cortex-m/nrf54l/boot_uart.c) over the
 * nRF54L register model (test/model/nrf54l_model.h): mk/boot.mk puts
 * test/model/cortex-m/nrf54l/ ahead of src/hal/cortex-m/, so its cm_regs.h
 * sends every TFW_CM_REG to the model. A task the UARTE answers (TX.START with
 * TXDRDY, RX.STOP with RXTO) is the model's late task. The test includes
 * boot_uart.c itself, so it can see the EasyDMA bytes rx_dma and tx_dma.
 */
#include <stddef.h>
#include <stdint.h>

#include "unity.h"
#include "boot_hal.h"
#include "../../boot_soc.h"
#include "nrf54l/soc_regs.h"
#include "nrf54l/soc_tables.h"
#include "nrf54l_model.h"

#include "../boot_uart.c"

#define BAUD 115200u

void setUp(void)
{
    tfw_nrf54l_model_reset();
}

void tearDown(void) {}

/* tfw-boot polls: an interrupt the application left enabled is off. */
static void test_init_leaves_inten_zero(void)
{
    TFW_NRF_UARTE_INTEN = 0xFFFFFFFFu;
    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_UARTE_INTEN);
}

/* The receive armed at init is one byte into the one-byte buffer. */
static void test_init_arms_a_one_byte_receive(void)
{
    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_MAXCNT);
}

/* A received byte re-arms the next receive, one byte again. */
static void test_rx_rearms_one_byte(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    TFW_NRF_UARTE_RX_MAXCNT = 0u;
    TFW_NRF_UARTE_RX_END    = 1u;
    (void)tfw_boot_uart_rx(&b);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_MAXCNT);
}

/* A send is one byte per transfer. */
static void test_tx_sends_one_byte(void)
{
    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_TX_START, &TFW_NRF_UARTE_TXDRDY, 0u, NULL);
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_TX_MAXCNT);
}

/* Before the jump the armed receive is stopped. No late task here: the model
 * leaves RX.STOP as written, and tfw-boot's wait for RXTO gives up. */
static void test_off_stops_the_armed_receive(void)
{
    tfw_boot_uart_init(BAUD);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_STOP);
}

/* RX.STOP completes at the next RXTO lookup (event_wait looks the event up
 * once, then spins on it); until it has, the UARTE still owns the receive into
 * RAM the application reuses, so ENABLE must not be touched, and the stop must
 * have been seen through to RXTO (the task consumed). */
static void test_off_disables_only_after_rxto(void)
{
    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_RX_STOP, &TFW_NRF_UARTE_RXTO, 0u,
                               &TFW_NRF_UARTE_ENABLE);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_nrf54l_model_blocked());
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_STOP);
}

/* ...and the UARTE and the console pins are back at their reset values. */
static void test_off_restores_reset_values(void)
{
    const uint32_t want[] = {
        0u, 0u,
        TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF54L_PSEL_DISCONNECTED,
        TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF54L_PSEL_DISCONNECTED,
        TFW_NRF_UARTE_BAUDRATE_RESET, TFW_NRF_UARTE_CONFIG_RESET,
        TFW_NRF_PIN_CNF_RESET, TFW_NRF_PIN_CNF_RESET,
    };
    uint32_t got[sizeof want / sizeof want[0]];

    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_RX_STOP, &TFW_NRF_UARTE_RXTO, 0u, NULL);
    TFW_NRF_UARTE_RX_END = 1u;
    TFW_NRF_UARTE_TX_END = 1u;
    TFW_NRF_UARTE_TXDRDY = 1u;
    TFW_NRF_UARTE_ERROR  = 1u;
    TFW_NRF_UARTE_RXTO   = 1u;
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_END);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_END);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TXDRDY);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_ERROR);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RXTO);
    /* P1.4, the console TX pin, driven low. */
    TEST_ASSERT_BITS_HIGH(1uL << 4, TFW_NRF_GPIO_OUTCLR(TFW_NRF_P1_BASE));
    got[0] = TFW_NRF_UARTE_ENABLE;
    got[1] = TFW_NRF_UARTE_INTEN;
    got[2] = TFW_NRF_UARTE_PSEL_TXD;
    got[3] = TFW_NRF_UARTE_PSEL_RXD;
    got[4] = TFW_NRF_UARTE_PSEL_CTS;
    got[5] = TFW_NRF_UARTE_PSEL_RTS;
    got[6] = TFW_NRF_UARTE_BAUDRATE;
    got[7] = TFW_NRF_UARTE_CONFIG;
    got[8] = TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_TX_BIT);
    got[9] = TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_RX_BIT);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, got, sizeof want / sizeof want[0]);
}

/* A received byte is handed out once: RX.END is cleared as the byte is taken
 * (boot_uart.c lines 104-110), so the next poll with nothing new is 0. */
static void test_rx_takes_a_byte_once(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_END);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
}

/* A rate the vendor's table does not list leaves the UARTE off and no receive
 * armed (lines 58-66), and a stale RX.END is not read as a byte while it is
 * off (lines 95-97). */
static void test_unlisted_baud_leaves_the_uarte_off(void)
{
    uint8_t b;

    TFW_NRF_UARTE_RX_END = 1u;
    tfw_boot_uart_init(1234u);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_UARTE_ENABLE);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_START);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
}

/* An overrun, framing error or break is cleared at the next poll: ERROR back
 * to 0 and every ERRORSRC bit written to clear (lines 98-103). */
static void test_rx_clears_an_error(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    TFW_NRF_UARTE_ERROR    = 1u;
    TFW_NRF_UARTE_ERRORSRC = 0u;
    (void)tfw_boot_uart_rx(&b);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_ERROR);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_ERRORSRC_ALL, TFW_NRF_UARTE_ERRORSRC);
}

/* The values are written out, not derived through tfw_nrf54l_psel() or the
 * vendor's rate table, so a wrong pin, rate or frame in init fails here. */
static void test_init_programs_the_console_pins_rate_and_frame(void)
{
    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_BITS_HIGH(1uL << 4, TFW_NRF_GPIO_OUTSET(TFW_NRF_P1_BASE));
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_OUTPUT, TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, 4));
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_INPUT | TFW_NRF_PIN_CNF_PULLUP,
                            TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, 5));
    TEST_ASSERT_EQUAL_HEX32(0x24u, TFW_NRF_UARTE_PSEL_TXD);   /* P1.4, CONNECT */
    TEST_ASSERT_EQUAL_HEX32(0x25u, TFW_NRF_UARTE_PSEL_RXD);   /* P1.5, CONNECT */
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF_UARTE_PSEL_CTS);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF_UARTE_PSEL_RTS);
    TEST_ASSERT_EQUAL_HEX32(0x01D60000u, TFW_NRF_UARTE_BAUDRATE);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_CONFIG_8N1, TFW_NRF_UARTE_CONFIG);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_ENABLE_ON, TFW_NRF_UARTE_ENABLE);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_START);
}

/* No late task is armed: a send that waited for TXDRDY would hang here. */
static void test_tx_on_a_uarte_left_off_starts_nothing(void)
{
    tfw_boot_uart_init(1234u);
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_START);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_MAXCNT);
}

/* No late task is armed: an off that waited for RXTO would hang here. */
static void test_off_on_a_uarte_never_enabled_stops_nothing(void)
{
    tfw_boot_uart_init(1234u);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_STOP);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_UARTE_ENABLE);
}

/* A stale RX.END from before init is not handed out as a byte. */
static void test_init_drops_a_stale_rx_end(void)
{
    uint8_t b;

    TFW_NRF_UARTE_RX_END = 1u;
    TFW_NRF_UARTE_ERROR  = 1u;
    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_END);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_END);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TXDRDY);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_ERROR);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RXTO);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
}

static void test_tx_waits_for_txdrdy_and_clears_its_events(void)
{
    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_TX_START, &TFW_NRF_UARTE_TXDRDY, 0u, NULL);
    TFW_NRF_UARTE_TX_END = 1u;
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_START);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TXDRDY);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_END);
}

static void test_tx_whose_txdrdy_never_comes_returns(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_TX_START);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TXDRDY);
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
}

static void test_after_off_rx_tx_and_a_second_off_do_nothing(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_RX_STOP, &TFW_NRF_UARTE_RXTO, 0u, NULL);
    tfw_cm_boot_uart_off();
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_START);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_MAXCNT);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_STOP);
}

/* The byte rx hands out is the one EasyDMA wrote into the armed buffer, and
 * each byte received is the new one. */
static void test_rx_hands_out_the_received_byte(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(uintptr_t)&rx_dma, TFW_NRF_UARTE_RX_PTR);
    rx_dma = 0xA7u;
    TFW_NRF_UARTE_RX_END = 1u;
    b = 0x00u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xA7u, b);
    rx_dma = 0x3Cu;
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);
}

/* With no byte received, or after off, rx leaves the caller's byte alone. */
static void test_rx_with_nothing_received_leaves_out_alone(void)
{
    uint8_t b;

    tfw_boot_uart_init(BAUD);
    b = 0x5Eu;
    rx_dma = 0xA7u;
    TFW_NRF_UARTE_RX_END = 0u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5Eu, b);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_RX_STOP, &TFW_NRF_UARTE_RXTO, 0u, NULL);
    tfw_cm_boot_uart_off();
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5Eu, b);
}

/* A send puts its byte in the transmit buffer and points TX.PTR at it. */
static void test_tx_sends_the_byte_from_its_buffer(void)
{
    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_TX_START, &TFW_NRF_UARTE_TXDRDY, 0u, NULL);
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tx_dma);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(uintptr_t)&tx_dma, TFW_NRF_UARTE_TX_PTR);
    tfw_boot_uart_tx(0xC3u);
    TEST_ASSERT_EQUAL_HEX8(0xC3u, tx_dma);
}

/* A byte that comes with a line error is still handed out (the framer's
 * check drops it) and the one-byte receive is armed again. */
static void test_rx_hands_out_a_byte_that_came_with_an_error(void)
{
    uint8_t b = 0x00u;

    tfw_boot_uart_init(BAUD);
    TFW_NRF_UARTE_RX_MAXCNT = 0u;
    TFW_NRF_UARTE_RX_START  = 0u;
    rx_dma                  = 0x6Bu;
    TFW_NRF_UARTE_ERROR     = 1u;
    TFW_NRF_UARTE_ERRORSRC  = 0u;
    TFW_NRF_UARTE_RX_END    = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x6Bu, b);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_ERROR);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_ERRORSRC_ALL, TFW_NRF_UARTE_ERRORSRC);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_RX_END);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_MAXCNT);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_START);
}

/* An off whose RXTO never comes still turns the UARTE off and leaves
 * rx and tx doing nothing. */
static void test_off_whose_rxto_never_comes_still_turns_the_uarte_off(void)
{
    uint8_t b = 0x00u;

    tfw_boot_uart_init(BAUD);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_STOP);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_ENABLE);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_INTEN);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF_UARTE_PSEL_TXD);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF54L_PSEL_DISCONNECTED, TFW_NRF_UARTE_PSEL_RXD);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_BAUDRATE_RESET, TFW_NRF_UARTE_BAUDRATE);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_CONFIG_RESET, TFW_NRF_UARTE_CONFIG);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_RESET,
                            TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_TX_BIT));
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_RESET,
                            TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_RX_BIT));
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_UARTE_TX_START);
}

/* A second init after off enables the UARTE, arms the receive and lets
 * rx and tx run again. */
static void test_a_second_init_after_off_runs_again(void)
{
    uint8_t b = 0x00u;

    tfw_boot_uart_init(BAUD);
    tfw_nrf54l_model_late_task(&TFW_NRF_UARTE_RX_STOP, &TFW_NRF_UARTE_RXTO, 0u, NULL);
    tfw_cm_boot_uart_off();
    TFW_NRF_UARTE_RX_START  = 0u;
    TFW_NRF_UARTE_RX_MAXCNT = 0u;
    tfw_boot_uart_init(BAUD);
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_UARTE_ENABLE_ON, TFW_NRF_UARTE_ENABLE);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_MAXCNT);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_RX_START);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(uintptr_t)&rx_dma, TFW_NRF_UARTE_RX_PTR);
    rx_dma = 0x42u;
    TFW_NRF_UARTE_RX_END = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x42u, b);
    /* No late task: TX.START stays readable, and tx's bounded wait for
     * TXDRDY returns. */
    tfw_boot_uart_tx(0x24u);
    TEST_ASSERT_EQUAL_HEX8(0x24u, tx_dma);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_UARTE_TX_START);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_leaves_inten_zero);
    RUN_TEST(test_init_arms_a_one_byte_receive);
    RUN_TEST(test_rx_rearms_one_byte);
    RUN_TEST(test_tx_sends_one_byte);
    RUN_TEST(test_off_stops_the_armed_receive);
    RUN_TEST(test_off_disables_only_after_rxto);
    RUN_TEST(test_off_restores_reset_values);
    RUN_TEST(test_rx_takes_a_byte_once);
    RUN_TEST(test_unlisted_baud_leaves_the_uarte_off);
    RUN_TEST(test_rx_clears_an_error);
    RUN_TEST(test_init_programs_the_console_pins_rate_and_frame);
    RUN_TEST(test_tx_on_a_uarte_left_off_starts_nothing);
    RUN_TEST(test_off_on_a_uarte_never_enabled_stops_nothing);
    RUN_TEST(test_init_drops_a_stale_rx_end);
    RUN_TEST(test_tx_waits_for_txdrdy_and_clears_its_events);
    RUN_TEST(test_tx_whose_txdrdy_never_comes_returns);
    RUN_TEST(test_after_off_rx_tx_and_a_second_off_do_nothing);
    RUN_TEST(test_rx_hands_out_the_received_byte);
    RUN_TEST(test_rx_with_nothing_received_leaves_out_alone);
    RUN_TEST(test_tx_sends_the_byte_from_its_buffer);
    RUN_TEST(test_rx_hands_out_a_byte_that_came_with_an_error);
    RUN_TEST(test_off_whose_rxto_never_comes_still_turns_the_uarte_off);
    RUN_TEST(test_a_second_init_after_off_runs_again);
    return UNITY_END();
}
