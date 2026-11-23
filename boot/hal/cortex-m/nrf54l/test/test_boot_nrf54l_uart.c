/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's nRF54L15 UART (boot/hal/cortex-m/nrf54l/boot_uart.c) over the
 * nRF54L register model (test/model/nrf54l_model.h): mk/boot.mk puts
 * test/model/cortex-m/nrf54l/ ahead of src/hal/cortex-m/, so its cm_regs.h
 * sends every TFW_CM_REG to the model. A task the UARTE answers (TX.START with
 * TXDRDY, RX.STOP with RXTO) is the model's late task.
 */
#include <stddef.h>
#include <stdint.h>

#include "unity.h"
#include "boot_hal.h"
#include "../../boot_soc.h"
#include "nrf54l/soc_regs.h"
#include "nrf54l/soc_tables.h"
#include "nrf54l_model.h"

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
    tfw_cm_boot_uart_off();
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
    return UNITY_END();
}
