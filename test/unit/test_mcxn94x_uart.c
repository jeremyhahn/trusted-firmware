/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The FRDM-MCXN947's link UART, src/hal/cortex-m/mcxn94x/hal_uart.c, on the
 * register file model (-DTFW_CM_MODEL_REGFILE, test/model/cortex-m): which
 * registers tfw_hal_uart_init, tfw_hal_uart_tx_try, tfw_hal_uart_tx_idle,
 * tfw_hal_uart_rx and the LP_FLEXCOMM4 interrupt (tfw_cm_irq39) write, and
 * what they return. Each value is the MCXNx4x Reference Manual's, through the
 * names soc_regs.h gives it: LP_FLEXCOMM PSELID = 1 (UART), LPUART BAUD with
 * OSR - 1 at bits 28:24 and SBR at 12:0, CTRL TE/RE/RIE/TIE, STAT TDRE, TC,
 * RDRF, and DATA. A board has not run them.
 *
 * The model holds what is written and nothing more, so a status flag the
 * LPUART would raise is preset by the test. The wrap below (linked with
 * -Wl,--wrap=tfw_cm_model_reg) adds the two behaviours of STAT the handler
 * depends on: its TDRE, TC and RDRF bits are read-only, and a read of DATA
 * clears RDRF. Without the second the receive loop would never see the FIFO
 * empty.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "lpuart_baud.h"
#include "regfile_model.h"

void tfw_cm_irq39(void);

#define FC4(off)        ((uintptr_t)TFW_MCXN_FC4_BASE + (off))
#define BAUD_ADDR       FC4(TFW_MCXN_LPUART_BAUD_OFF)
#define STAT_ADDR       FC4(TFW_MCXN_LPUART_STAT_OFF)
#define CTRL_ADDR       FC4(TFW_MCXN_LPUART_CTRL_OFF)
#define DATA_ADDR       FC4(TFW_MCXN_LPUART_DATA_OFF)
#define PSELID_ADDR     FC4(TFW_MCXN_FC_PSELID_OFF)
#define AHBCLKSET1_ADDR ((uintptr_t)TFW_MCXN_SYSCON_BASE + 0x224u)
#define FCCLKSEL4_ADDR  ((uintptr_t)TFW_MCXN_SYSCON_BASE + 0x2B0u + 4u * 4u)
#define PCR_ADDR(bit)   ((uintptr_t)0x50117000u + 0x80u + 4u * (bit))   /* PORT1 */

/* 115200 on FRO12M: OSR 26, SBR 4 gives 12e6 / 104 = 115384, 0.16 % fast,
 * the closest of OSR 4..32; OSR >= 8 samples on one edge, so BOTHEDGE stays
 * clear. (26 - 1) << 24 | 4. */
#define BAUD_115200     0x19000004u

volatile uint32_t *__real_tfw_cm_model_reg(uintptr_t addr);
volatile uint32_t *__wrap_tfw_cm_model_reg(uintptr_t addr);

/* STAT's TDRE, TC and RDRF are read-only on the LPUART: a write of STAT (the
 * handler's write-1-to-clear of OR, NF and FE) leaves them as they were. The
 * model keeps what is written, so the wrap holds them here and sets them
 * again on each access of STAT; a read of DATA takes the one queued byte and
 * clears RDRF. */
static uint32_t stat_ro;

volatile uint32_t *__wrap_tfw_cm_model_reg(uintptr_t addr)
{
    if (addr == DATA_ADDR) {
        stat_ro &= ~TFW_MCXN_LPUART_STAT_RDRF;
        *__real_tfw_cm_model_reg(STAT_ADDR) &= ~TFW_MCXN_LPUART_STAT_RDRF;
    } else if (addr == STAT_ADDR) {
        *__real_tfw_cm_model_reg(STAT_ADDR) |= stat_ro;
    }
    return __real_tfw_cm_model_reg(addr);
}

static void preset(uintptr_t addr, uint32_t v)
{
    if (addr == STAT_ADDR) {
        stat_ro = v & (TFW_MCXN_LPUART_STAT_TDRE | TFW_MCXN_LPUART_STAT_TC |
                       TFW_MCXN_LPUART_STAT_RDRF);
    }
    *__real_tfw_cm_model_reg(addr) = v;
}

void setUp(void)
{
    regfile_model_reset();
    stat_ro = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
}

void tearDown(void)
{
}

/* ---- init */

static void test_init_selects_uart_on_lp_flexcomm4(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_FC_PSELID_UART, regfile_model_peek(PSELID_ADDR));
    TEST_ASSERT_EQUAL_HEX32(1u, regfile_model_peek(PSELID_ADDR));
}

static void test_init_gates_and_clocks_flexcomm4(void)
{
    TEST_ASSERT_EQUAL_HEX32(1u << 15, regfile_model_peek(AHBCLKSET1_ADDR));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_FCCLKSEL_FRO12M, regfile_model_peek(FCCLKSEL4_ADDR));
}

static void test_init_baud_is_osr_sbr_for_115200(void)
{
    uint32_t want = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(12000000u, 115200u, &want));
    TEST_ASSERT_EQUAL_HEX32(BAUD_115200, want);
    TEST_ASSERT_EQUAL_HEX32(BAUD_115200, regfile_model_peek(BAUD_ADDR));
}

static void test_init_enables_te_re_and_rx_interrupt_only(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_LPUART_CTRL_TE | TFW_MCXN_LPUART_CTRL_RE |
                            TFW_MCXN_LPUART_CTRL_RIE,
                            regfile_model_peek(CTRL_ADDR));
    TEST_ASSERT_EQUAL_HEX32((1u << 19) | (1u << 18) | (1u << 21), regfile_model_peek(CTRL_ADDR));
}

static void test_init_muxes_the_link_pins(void)
{
    TEST_ASSERT_EQUAL_HEX32((2u << 8) | TFW_MCXN_PCR_IBE, regfile_model_peek(PCR_ADDR(8u)));
    TEST_ASSERT_EQUAL_HEX32(2u << 8, regfile_model_peek(PCR_ADDR(9u)));
}

static void test_init_refuses_a_baud_it_cannot_reach(void)
{
    regfile_model_reset();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(BAUD_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(CTRL_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PSELID_ADDR));
}

/* ---- tx */

static void test_tx_try_with_tdre_set_writes_data(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    TEST_ASSERT_BITS_HIGH(TFW_MCXN_LPUART_CTRL_TIE, regfile_model_peek(CTRL_ADDR));

    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TDRE);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_HEX32(0xA5u, regfile_model_peek(DATA_ADDR));
    TEST_ASSERT_BITS_LOW(TFW_MCXN_LPUART_CTRL_TIE, regfile_model_peek(CTRL_ADDR));
    TEST_ASSERT_BITS_HIGH(TFW_MCXN_LPUART_CTRL_TE | TFW_MCXN_LPUART_CTRL_RE,
                          regfile_model_peek(CTRL_ADDR));
}

static void test_tx_with_tdre_clear_holds_the_byte(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x5Au));
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(DATA_ADDR));
    TEST_ASSERT_BITS_HIGH(TFW_MCXN_LPUART_CTRL_TIE, regfile_model_peek(CTRL_ADDR));

    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TDRE);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_HEX32(0x5Au, regfile_model_peek(DATA_ADDR));
}

static void test_tx_try_refuses_when_tdre_stays_clear_and_the_ring_fills(void)
{
    uint32_t n;

    for (n = 0u; n < 63u; n++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)n));
        tfw_cm_irq39();
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xFFu));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(DATA_ADDR));

    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TDRE);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_HEX32(62u, regfile_model_peek(DATA_ADDR));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xFFu));
}

static void test_tx_idle_follows_stat_tc(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TC);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

static void test_tx_idle_is_false_with_a_byte_queued(void)
{
    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TC);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x11u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());

    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_TC | TFW_MCXN_LPUART_STAT_TDRE);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

/* ---- rx */

static void test_rx_reads_data_with_rdrf_set(void)
{
    uint8_t b = 0u;

    preset(DATA_ADDR, 0x3Cu);
    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_RDRF);
    tfw_cm_irq39();
    TEST_ASSERT_BITS_LOW(TFW_MCXN_LPUART_STAT_RDRF, regfile_model_peek(STAT_ADDR));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

static void test_rx_none_with_rdrf_clear(void)
{
    uint8_t b = 0x77u;

    preset(DATA_ADDR, 0x3Cu);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x77u, b);
}

static void test_rx_writes_back_the_error_flags_it_saw(void)
{
    uint8_t b = 0u;

    preset(DATA_ADDR, 0x42u);
    preset(STAT_ADDR, TFW_MCXN_LPUART_STAT_RDRF | TFW_MCXN_LPUART_STAT_OR |
                      TFW_MCXN_LPUART_STAT_FE);
    tfw_cm_irq39();
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_LPUART_STAT_OR | TFW_MCXN_LPUART_STAT_FE,
                            regfile_model_peek(STAT_ADDR));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x42u, b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_selects_uart_on_lp_flexcomm4);
    RUN_TEST(test_init_gates_and_clocks_flexcomm4);
    RUN_TEST(test_init_baud_is_osr_sbr_for_115200);
    RUN_TEST(test_init_enables_te_re_and_rx_interrupt_only);
    RUN_TEST(test_init_muxes_the_link_pins);
    RUN_TEST(test_init_refuses_a_baud_it_cannot_reach);
    RUN_TEST(test_tx_try_with_tdre_set_writes_data);
    RUN_TEST(test_tx_with_tdre_clear_holds_the_byte);
    RUN_TEST(test_tx_try_refuses_when_tdre_stays_clear_and_the_ring_fills);
    RUN_TEST(test_tx_idle_follows_stat_tc);
    RUN_TEST(test_tx_idle_is_false_with_a_byte_queued);
    RUN_TEST(test_rx_reads_data_with_rdrf_set);
    RUN_TEST(test_rx_none_with_rdrf_clear);
    RUN_TEST(test_rx_writes_back_the_error_flags_it_saw);
    return UNITY_END();
}
