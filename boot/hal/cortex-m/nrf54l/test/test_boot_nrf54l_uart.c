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
#include <stdio.h>

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

/* A rate the vendor's table refuses: the UARTE the application left on is
 * switched off, no receive is armed, and receive and send do nothing, so the
 * session is silent rather than on the wire at the application's rate. */
static void test_an_unlisted_baud_leaves_the_uarte_off(void)
{
    static const uint32_t rates[] = { 0u, 1u, 115201u };
    char msg[32];
    size_t i;
    uint8_t b;

    for (i = 0u; i < sizeof rates / sizeof rates[0]; i++) {
        (void)snprintf(msg, sizeof msg, "baud %lu", (unsigned long)rates[i]);
        tfw_nrf54l_model_reset();
        TFW_NRF_UARTE_ENABLE = TFW_NRF_UARTE_ENABLE_ON;
        TFW_NRF_UARTE_INTEN  = 0xFFFFFFFFu;
        tfw_boot_uart_init(rates[i]);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, TFW_NRF_UARTE_ENABLE, msg);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, TFW_NRF_UARTE_INTEN, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, TFW_NRF_UARTE_RX_MAXCNT, msg);

        TFW_NRF_UARTE_RX_END = 1u;
        b = 0xA5u;
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, tfw_boot_uart_rx(&b), msg);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA5u, b, msg);

        tfw_boot_uart_tx(0x5Au);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, TFW_NRF_UARTE_TX_MAXCNT, msg);
    }
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
    RUN_TEST(test_an_unlisted_baud_leaves_the_uarte_off);
    return UNITY_END();
}
