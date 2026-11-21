/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled UART on mps2 (boot/hal/cortex-m/mps2/boot_uart.c) under
 * mps2-an505's profile, over a model of UART0, the CMSDK APB UART: DATA,
 * STATE, CTRL, INT and BAUDDIV at +0x00..+0x10 from 0x40200000, the layout
 * src/hal/cortex-m/mps2/soc_regs.h names. docs/cortex-m/engineering.md R21:
 * tfw-boot enables no interrupt, its UART polls; here no store ever sets CTRL's
 * TXINT (0x04) or RXINT (0x08). The model notes a store when a register's
 * value differs from the one it last saw, at the driver's next access or the
 * test's next look, so each test starts its registers at a value the driver's
 * store must change. The transmitter drains on the driver's STATE polls, never
 * on a clock.
 */
#include <stdint.h>
#include "unity.h"
#include "boot_hal.h"
#include "cm_regs.h"
#include "tfw/config.h"

/* Not in boot_hal.h: the Cortex-M boot HAL's hand-over hook. */
void tfw_cm_boot_uart_off(void);

#define UART0     0x40200000u
#define R_DATA    0u
#define R_STATE   1u
#define R_CTRL    2u
#define R_INT     3u
#define R_BAUDDIV 4u
#define MODEL_REGS 5u

#define STATE_TXFULL 0x01u
#define STATE_TXOVR  0x04u
#define STATE_RXOVR  0x08u
#define CTRL_TXEN    0x01u
#define CTRL_RXEN    0x02u
#define CTRL_TXINT   0x04u
#define CTRL_RXINT   0x08u
#define INT_TX       0x01u
#define INT_RX       0x02u

#define MODEL_LOG 16u

static volatile uint32_t model_val[MODEL_REGS];
static uint32_t model_seen[MODEL_REGS];
static uint32_t model_stores[MODEL_REGS];
static uint32_t model_ever[MODEL_REGS];   /* every bit any store set */

/* The stores in order, and whether the transmitter had drained by then. */
static uint32_t log_reg[MODEL_LOG];
static uint32_t log_val[MODEL_LOG];
static uint8_t  log_drained[MODEL_LOG];
static uint32_t log_used;

/* STATE polls left before the model's transmitter empties, clearing TXFULL. */
static uint32_t tx_polls;
static uint8_t  tx_drained;

/* Note every register the driver changed since the model last looked. */
static void model_sync(void)
{
    uint32_t i;

    for (i = 0u; i < MODEL_REGS; i++) {
        uint32_t v = model_val[i];

        if (v == model_seen[i]) {
            continue;
        }
        if (log_used == MODEL_LOG) {
            TEST_FAIL_MESSAGE("store log full");
        }
        log_reg[log_used] = i;
        log_val[log_used] = v;
        log_drained[log_used] = tx_drained;
        log_used++;
        model_stores[i]++;
        model_ever[i] |= v;
        model_seen[i] = v;
    }
}

/* A value the model, not the driver, puts in a register. */
static void model_set(uint32_t r, uint32_t v)
{
    model_val[r] = v;
    model_seen[r] = v;
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    uint32_t r = (addr - UART0) / 4u;

    if (addr < UART0 || (addr & 3u) != 0u || r >= MODEL_REGS) {
        TEST_FAIL_MESSAGE("access outside UART0");
    }
    model_sync();
    if (r == R_STATE && tx_polls != 0u && --tx_polls == 0u) {
        model_set(R_STATE, model_val[R_STATE] & ~STATE_TXFULL);
        tx_drained = 1u;
    }
    return &model_val[r];
}

/* The register as the test reads it, after noting the driver's last store. */
static uint32_t reg(uint32_t r)
{
    model_sync();
    return model_val[r];
}

static uint32_t stores(uint32_t r)
{
    model_sync();
    return model_stores[r];
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < MODEL_REGS; i++) {
        model_set(i, 0u);
        model_stores[i] = 0u;
        model_ever[i] = 0u;
    }
    log_used = 0u;
    tx_polls = 0u;
    tx_drained = 0u;
}

void tearDown(void) {}

/* 20 MHz / 115200 = 173. CTRL starts with both interrupt enables set, so the
 * driver's clearing store is seen too. */
static void test_init_enables_tx_and_rx_and_no_interrupt(void)
{
    model_set(R_CTRL, CTRL_TXINT | CTRL_RXINT);
    model_set(R_BAUDDIV, 0xDEAD0000u);

    tfw_boot_uart_init(115200u);

    TEST_ASSERT_EQUAL_HEX32(CTRL_TXEN | CTRL_RXEN, reg(R_CTRL));
    TEST_ASSERT_EQUAL_UINT32(2u, stores(R_CTRL));
    TEST_ASSERT_EQUAL_HEX32(0u, model_ever[R_CTRL] & (CTRL_TXINT | CTRL_RXINT));
    TEST_ASSERT_EQUAL_UINT32(173u, (uint32_t)(TFW_CM_CPU_HZ / 115200u));
    TEST_ASSERT_EQUAL_UINT32(TFW_CM_CPU_HZ / 115200u, reg(R_BAUDDIV));
    TEST_ASSERT_EQUAL_HEX32(STATE_TXOVR | STATE_RXOVR, reg(R_STATE));
    TEST_ASSERT_EQUAL_UINT32(1u, stores(R_STATE));
    TEST_ASSERT_EQUAL_UINT32(0u, stores(R_DATA));
    TEST_ASSERT_EQUAL_UINT32(0u, stores(R_INT));
    /* Enabled last, after the divisor and the cleared overruns. */
    TEST_ASSERT_EQUAL_UINT32(R_CTRL, log_reg[log_used - 1u]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_enables_tx_and_rx_and_no_interrupt);
    return UNITY_END();
}
