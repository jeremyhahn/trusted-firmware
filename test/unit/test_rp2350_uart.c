/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's application UART, src/hal/cortex-m/rp2350/hal_uart.c, on the
 * Pico 2 W's profile against the suite's own register file (#621).
 *
 * This file stands in for cm_regs.h: TFW_CM_REG goes to reg(), each address
 * its own cell, with RESETS_DONE preset to UART0's bit so init's wait ends,
 * and the PL011's receive FIFO modelled on FR and DR: FR reads RXFE while the
 * FIFO is empty, and each DR read takes the FIFO's next byte. The NVIC is two
 * stubs that count. hal_uart.c is included unchanged.
 */
#define TFW_CM_REGS_H

#include <stdint.h>

static volatile uint32_t *reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*reg((uintptr_t)(addr)))

static uint32_t nvic_enabled;
static uint32_t nvic_disabled;
static uint32_t nvic_last_irq;

static inline void tfw_cm_nvic_enable(uint32_t irq)
{
    nvic_enabled++;
    nvic_last_irq = irq;
}

static inline void tfw_cm_nvic_disable(uint32_t irq)
{
    nvic_disabled++;
    nvic_last_irq = irq;
}

#include "unity.h"
#include "hal_uart.c"

#define CELLS 64u

static struct {
    uintptr_t         addr;
    volatile uint32_t value;
} cells[CELLS];
static unsigned n_cells;

static uint8_t  fifo[128];
static unsigned fifo_len;
static unsigned fifo_pos;

static volatile uint32_t *cell(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < n_cells; i++) {
        if (cells[i].addr == addr) {
            return &cells[i].value;
        }
    }
    TEST_ASSERT_LESS_THAN_UINT(CELLS, n_cells);
    cells[n_cells].addr  = addr;
    cells[n_cells].value = 0u;
    return &cells[n_cells++].value;
}

static volatile uint32_t *reg(uintptr_t addr)
{
    volatile uint32_t *c = cell(addr);

    if (addr == TFW_RP_UART0_BASE + 0x018u) {
        *c = fifo_pos < fifo_len ? 0u : TFW_RP_UART_FR_RXFE;
    } else if (addr == TFW_RP_UART0_BASE + 0x000u && fifo_pos < fifo_len) {
        *c = fifo[fifo_pos++];
    }
    return c;
}

void setUp(void)
{
    n_cells       = 0u;
    fifo_len      = 0u;
    fifo_pos      = 0u;
    nvic_enabled  = 0u;
    nvic_disabled = 0u;
    nvic_last_irq = 0u;
    *cell(TFW_RP_RESETS_BASE + 0x8u) = TFW_RP_RESET_UART0;
}

void tearDown(void)
{
}

/* From clk_peri at 12 MHz the integer divisor is 4 * 12 MHz / baud / 64:
 * 0 above 750000 baud, past 0xFFFF below 12 baud, and baud 0 divides
 * nothing. Each is refused before a register or the NVIC is touched. */
static void test_a_baud_it_cannot_divide_is_refused_and_writes_nothing(void)
{
    static const uint32_t bauds[] = { 0u, 1000000u, 11u };
    unsigned i;

    for (i = 0u; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_uart_init(bauds[i]));
    }
    TEST_ASSERT_EQUAL_UINT(1u, n_cells);        /* RESETS_DONE alone */
    TEST_ASSERT_EQUAL_UINT(0u, nvic_disabled);
    TEST_ASSERT_EQUAL_UINT(0u, nvic_enabled);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_baud_it_cannot_divide_is_refused_and_writes_nothing);
    return UNITY_END();
}
