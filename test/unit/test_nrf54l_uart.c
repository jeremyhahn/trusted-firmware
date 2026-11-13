/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15 uart HAL's register values (src/hal/cortex-m/nrf54l/
 * hal_uart.c) through tfw_hal_uart_init, tfw_hal_uart_tx_try,
 * tfw_hal_uart_tx_idle, tfw_hal_uart_rx and the UARTE20 interrupt on the
 * register file (test/model/cortex-m/regfile_model.c): UARTE20's BAUDRATE
 * word for 115200, CONFIG 8N1, PSEL.TXD/RXD for P1.4/P1.5, ENABLE and
 * INTEN, the EasyDMA pointer and MAXCNT of each transfer, TXDRDY ending a
 * transmission, and a byte received only on DMA.RX.END. The values are the
 * nrfx MDK's (the source soc_regs.h names). A board has not run them.
 *
 * The suite includes hal_uart.c, as test_nrf54l_saadc.c includes hal_adc.c:
 * the register file moves no data, so the test reads the EasyDMA buffers
 * and plays the UARTE's part by writing them and raising its events.
 */
#include <stdint.h>

#include "unity.h"
#include "regfile_model.h"

#include "hal_uart.c"

/* UARTE20 and P1 register addresses (nrfx MDK, nrf54l15_types.h). */
#define UARTE_ADDR(off)  ((uintptr_t)0x500C6000u + (off))
#define P1_ADDR(off)     ((uintptr_t)0x500D8200u + (off))
/* UARTE20's interrupt, 198: NVIC word 6, bit 6. */
#define NVIC_ISER6_ADDR  ((uintptr_t)0xE000E100u + 4u * 6u)

static uint32_t peek(uintptr_t addr)
{
    return regfile_model_peek(addr);
}

/* The address EasyDMA is handed, as the HAL writes it into a 32-bit word. */
static uint32_t dma_addr(volatile uint8_t *buf)
{
    return (uint32_t)(uintptr_t)buf;
}

/* The UARTE's end of a byte on the TXD line: TXDRDY, then its interrupt. */
static void byte_left_txd(void)
{
    TFW_NRF_UARTE_TXDRDY = 1u;
    tfw_cm_irq198();
}

/* The UARTE's receive: EasyDMA writes the byte, DMA.RX.END, the interrupt. */
static void byte_arrives(uint8_t b)
{
    rx_dma               = b;
    TFW_NRF_UARTE_RX_END = 1u;
    tfw_cm_irq198();
}

void setUp(void)
{
    regfile_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
}

void tearDown(void)
{
}

/* BAUDRATE Baud115200 (UARTE_BAUDRATE_BAUDRATE_Baud115200, 16 MHz clock). */
void test_init_writes_the_115200_baudrate_word(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x01D60000u, peek(UARTE_ADDR(0x524u)));
}

/* CONFIG: FRAMESIZE 8 (bits 9-12), no parity, one stop, no flow control. */
void test_init_writes_config_8n1(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00001000u, peek(UARTE_ADDR(0x56Cu)));
}

/* A rate not in the table: TFW_ERR_RANGE, and no register touched. */
void test_init_refuses_an_unknown_rate_before_any_write(void)
{
    regfile_model_reset();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(12345u));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(UARTE_ADDR(0x524u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(UARTE_ADDR(0x500u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(UARTE_ADDR(0x604u)));
}

/* PSEL: PORT in bits 5-7, PIN in bits 0-4, CONNECT (bit 31) 0. TXD P1.4,
 * RXD P1.5, CTS and RTS disconnected. */
void test_init_selects_p1_4_and_p1_5(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000024u, peek(UARTE_ADDR(0x604u)));
    TEST_ASSERT_EQUAL_HEX32(0x00000025u, peek(UARTE_ADDR(0x60Cu)));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, peek(UARTE_ADDR(0x608u)));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, peek(UARTE_ADDR(0x610u)));
}

/* TXD driven high as an output, RXD an input with its pull-up. */
void test_init_configures_the_console_pins(void)
{
    TEST_ASSERT_EQUAL_HEX32(1u << 4, peek(P1_ADDR(0x004u)));
    TEST_ASSERT_EQUAL_HEX32(0x1u, peek(P1_ADDR(0x080u + 4u * 4u)));
    TEST_ASSERT_EQUAL_HEX32(0xCu, peek(P1_ADDR(0x080u + 4u * 5u)));
}

/* ENABLE Enabled (8), INTEN TXDRDY (bit 3) and DMARXEND (bit 19), and the
 * interrupt on in the NVIC. */
void test_init_enables_the_uarte_and_its_interrupts(void)
{
    TEST_ASSERT_EQUAL_HEX32(8u, peek(UARTE_ADDR(0x500u)));
    TEST_ASSERT_EQUAL_HEX32(0x00080008u, peek(UARTE_ADDR(0x300u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 6, peek(NVIC_ISER6_ADDR));
}

/* Init starts the first one-byte receive into rx_dma. */
void test_init_starts_a_one_byte_receive(void)
{
    TEST_ASSERT_EQUAL_HEX32(dma_addr(&rx_dma), peek(UARTE_ADDR(0x704u)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x708u)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x028u)));
}

/* Events left over from before init are cleared, ERRORSRC written 1s. */
void test_init_clears_stale_events(void)
{
    TFW_NRF_UARTE_RX_END = 1u;
    TFW_NRF_UARTE_TX_END = 1u;
    TFW_NRF_UARTE_TXDRDY = 1u;
    TFW_NRF_UARTE_ERROR  = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x14Cu)));
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x168u)));
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x10Cu)));
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x114u)));
    TEST_ASSERT_EQUAL_HEX32(0xFu, peek(UARTE_ADDR(0x480u)));
}

/* tx_try hands EasyDMA the byte: DMA.TX.PTR at tx_dma, MAXCNT 1, START. */
void test_tx_try_starts_a_one_byte_transfer(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, tx_dma);
    TEST_ASSERT_EQUAL_HEX32(dma_addr(&tx_dma), peek(UARTE_ADDR(0x73Cu)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x740u)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x050u)));
}

/* A byte on the wire holds the next in the ring: no second START until
 * TXDRDY, which then sends it. */
void test_tx_try_queues_behind_a_byte_on_the_wire(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x11u));
    TFW_NRF_UARTE_TX_START = 0u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x22u));
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x050u)));
    TEST_ASSERT_EQUAL_HEX8(0x11u, tx_dma);

    byte_left_txd();
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x10Cu)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x050u)));
    TEST_ASSERT_EQUAL_HEX8(0x22u, tx_dma);
}

/* One byte on the wire and 63 in the ring fill it: the next is refused. */
void test_tx_try_refuses_a_full_ring(void)
{
    uint32_t n;

    for (n = 0u; n < 64u; n++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)n));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xFFu));
}

/* Idle after init; busy from tx_try until the last byte's TXDRDY. The
 * transfer's end (DMA.TX.END) alone is not idle: the byte may still be on
 * the line. */
void test_tx_idle_follows_txdrdy(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x11u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x22u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());

    TFW_NRF_UARTE_TX_END = 1u;
    tfw_cm_irq198();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());

    byte_left_txd();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    byte_left_txd();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x168u)));
}

/* DMA.RX.END: the byte reaches the ring, the event is cleared and the next
 * one-byte receive started. */
void test_rx_returns_the_byte_on_rx_end(void)
{
    uint8_t b = 0u;

    TFW_NRF_UARTE_RX_START = 0u;
    byte_arrives(0x5Au);
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x14Cu)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x028u)));
    TEST_ASSERT_EQUAL_HEX32(dma_addr(&rx_dma), peek(UARTE_ADDR(0x704u)));
    TEST_ASSERT_EQUAL_UINT32(1u, peek(UARTE_ADDR(0x708u)));

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

/* Bytes come out in the order they arrived. */
void test_rx_keeps_the_order(void)
{
    uint8_t b = 0u;

    byte_arrives(0x01u);
    byte_arrives(0x02u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x01u, b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x02u, b);
}

/* No DMA.RX.END: whatever rx_dma holds is not a byte, and no receive is
 * restarted. */
void test_rx_returns_nothing_without_rx_end(void)
{
    uint8_t b = 0x77u;

    TFW_NRF_UARTE_RX_START = 0u;
    rx_dma = 0x5Au;
    tfw_cm_irq198();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x77u, b);
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x028u)));
}

/* An ERROR event is cleared with its ERRORSRC bits. */
void test_irq_clears_a_receive_error(void)
{
    TFW_NRF_UARTE_ERRORSRC = 0u;
    TFW_NRF_UARTE_ERROR    = 1u;
    tfw_cm_irq198();
    TEST_ASSERT_EQUAL_UINT32(0u, peek(UARTE_ADDR(0x114u)));
    TEST_ASSERT_EQUAL_HEX32(0xFu, peek(UARTE_ADDR(0x480u)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_writes_the_115200_baudrate_word);
    RUN_TEST(test_init_writes_config_8n1);
    RUN_TEST(test_init_refuses_an_unknown_rate_before_any_write);
    RUN_TEST(test_init_selects_p1_4_and_p1_5);
    RUN_TEST(test_init_configures_the_console_pins);
    RUN_TEST(test_init_enables_the_uarte_and_its_interrupts);
    RUN_TEST(test_init_starts_a_one_byte_receive);
    RUN_TEST(test_init_clears_stale_events);
    RUN_TEST(test_tx_try_starts_a_one_byte_transfer);
    RUN_TEST(test_tx_try_queues_behind_a_byte_on_the_wire);
    RUN_TEST(test_tx_try_refuses_a_full_ring);
    RUN_TEST(test_tx_idle_follows_txdrdy);
    RUN_TEST(test_rx_returns_the_byte_on_rx_end);
    RUN_TEST(test_rx_keeps_the_order);
    RUN_TEST(test_rx_returns_nothing_without_rx_end);
    RUN_TEST(test_irq_clears_a_receive_error);
    return UNITY_END();
}
