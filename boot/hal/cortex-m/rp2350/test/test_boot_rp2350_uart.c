/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled UART on the RP2350 (boot/hal/cortex-m/rp2350/boot_uart.c)
 * run over a register model, every address and value a literal from [RP-DS]
 * release 8: UART0 at 0x40070000 and its registers §12.1.8 tables 1081-1089,
 * RESETS at 0x40020000 with UART0 bit 26 §7.5.3, the atomic SET/CLR aliases
 * at +0x2000/+0x3000 §2.1.3, IO_BANK0 GPIOn_CTRL at 0x40028004 + 8n §9.11.1,
 * PADS_BANK0 GPIOn at 0x40038004 + 4n §9.11.3, function 2 = UART §9.4.
 * The divisors are the 12 MHz crystal's at 115200: 4 * 12e6 / 115200 = 416.7,
 * rounded to 417 = 6 * 64 + 33.
 */
#include <stdint.h>
#include "unity.h"
#include "boot_hal.h"
#include "cm_regs.h"

/* Not in boot_hal.h: the Cortex-M boot HAL's hand-over hook. */
void tfw_cm_boot_uart_off(void);

#define MODEL_REGS 32u

static uint32_t model_addr[MODEL_REGS];
static volatile uint32_t model_val[MODEL_REGS];
static uint32_t model_used;

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_REGS) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used] = 0u;
    return &model_val[model_used++];
}

/* The register's value, or 0 when nothing touched it; never adds one. */
static uint32_t reg(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return model_val[i];
        }
    }
    return 0u;
}

static uint8_t touched(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return 1u;
        }
    }
    return 0u;
}

void setUp(void)
{
    model_used = 0u;
    /* RESETS.RESET_DONE reports UART0 out of reset; UART0.FR an empty,
     * idle transmitter. */
    *tfw_cm_model_reg(0x40020008u) = 1u << 26;
    *tfw_cm_model_reg(0x40070018u) = 1u << 7;
}

void tearDown(void) {}

static void test_init_releases_uart0_and_sets_115200_8n1(void)
{
    tfw_boot_uart_init(115200u);

    TEST_ASSERT_EQUAL_HEX32(1u << 26, reg(0x40022000u)); /* RESET SET */
    TEST_ASSERT_EQUAL_HEX32(1u << 26, reg(0x40023000u)); /* RESET CLR */
    TEST_ASSERT_EQUAL_UINT32(6u, reg(0x40070024u));      /* UARTIBRD */
    TEST_ASSERT_EQUAL_UINT32(33u, reg(0x40070028u));     /* UARTFBRD */
    TEST_ASSERT_EQUAL_HEX32(0x70u, reg(0x4007002Cu));    /* LCR_H 8 bits, FIFO */
    TEST_ASSERT_EQUAL_HEX32(0x0u, reg(0x40070038u));     /* UARTIMSC */
    TEST_ASSERT_EQUAL_HEX32(0x7FFu, reg(0x40070044u));   /* UARTICR */
    TEST_ASSERT_EQUAL_HEX32(0x301u, reg(0x40070030u));   /* CR RXE TXE EN */
}

static void test_init_routes_gpio0_and_gpio1_to_uart0(void)
{
    tfw_boot_uart_init(115200u);

    TEST_ASSERT_EQUAL_HEX32(2u, reg(0x40028004u)); /* GPIO0_CTRL FUNCSEL */
    TEST_ASSERT_EQUAL_HEX32(2u, reg(0x4002800Cu)); /* GPIO1_CTRL FUNCSEL */
    /* IE | DRIVE 4 mA | SCHMITT; the RX pad adds its pull-up. */
    TEST_ASSERT_EQUAL_HEX32(0x52u, reg(0x40038004u));
    TEST_ASSERT_EQUAL_HEX32(0x5Au, reg(0x40038008u));
}

static void test_tx_writes_the_byte_to_uartdr(void)
{
    tfw_boot_uart_init(115200u);
    tfw_boot_uart_tx(0xA5u);

    TEST_ASSERT_EQUAL_HEX32(0xA5u, reg(0x40070000u));
}

static void test_rx_reads_uartdr_unless_the_fifo_is_empty(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(115200u);
    *tfw_cm_model_reg(0x40070000u) = 0x3Cu;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);

    *tfw_cm_model_reg(0x40070018u) = (1u << 7) | (1u << 4); /* RXFE */
    b = 0u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0u, b);
}

/* 1 Mbaud from 12 MHz is a divisor under 1: the UART stays in reset, and the
 * session is silent rather than garbled. */
static void test_a_baud_out_of_reach_leaves_the_uart_in_reset(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(1000000u);
    tfw_boot_uart_tx(0xA5u);

    TEST_ASSERT_EQUAL_HEX32(1u << 26, reg(0x40022000u));
    TEST_ASSERT_FALSE(touched(0x40023000u));
    TEST_ASSERT_FALSE(touched(0x40070030u));
    TEST_ASSERT_FALSE(touched(0x40070000u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
}

static void test_off_returns_the_pins_and_resets_uart0(void)
{
    tfw_boot_uart_init(115200u);
    *tfw_cm_model_reg(0x40022000u) = 0u;
    tfw_cm_boot_uart_off();

    TEST_ASSERT_EQUAL_HEX32(0x1Fu, reg(0x40028004u)); /* FUNCSEL NULL */
    TEST_ASSERT_EQUAL_HEX32(0x1Fu, reg(0x4002800Cu));
    TEST_ASSERT_EQUAL_HEX32(1u << 26, reg(0x40022000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_releases_uart0_and_sets_115200_8n1);
    RUN_TEST(test_init_routes_gpio0_and_gpio1_to_uart0);
    RUN_TEST(test_tx_writes_the_byte_to_uartdr);
    RUN_TEST(test_rx_reads_uartdr_unless_the_fifo_is_empty);
    RUN_TEST(test_a_baud_out_of_reach_leaves_the_uart_in_reset);
    RUN_TEST(test_off_returns_the_pins_and_resets_uart0);
    return UNITY_END();
}
