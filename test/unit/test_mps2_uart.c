/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 byte link, src/hal/cortex-m/mps2/hal_uart.c, on a host register
 * file: the values tfw_hal_uart_init, tfw_hal_uart_tx_try,
 * tfw_hal_uart_tx_idle, tfw_hal_uart_rx and the UART0 interrupt service
 * write to the CMSDK APB UART and the NVIC, checked against the Cortex-M
 * System Design Kit TRM (Arm DDI 0479C, 4.3, the APB UART: DATA 0x00,
 * STATE 0x04, CTRL 0x08, INTSTATUS/INTCLEAR 0x0C, BAUDDIV 0x10, BAUDDIV at
 * least 16) on mps2-an505's profile (a 20 MHz APB clock, UART0 at
 * 0x40200000 on external interrupts 32 and 33).
 *
 * test/model/cortex-m/cm_regs.h sends every TFW_CM_REG access to
 * tfw_imx95_model_reg(); this suite defines that function itself as a plain
 * register file, so the SoC file compiles unchanged and every store it makes
 * lands in a write log. A status bit the hardware raises (STATE's RX-full and
 * TX-full, INTSTATUS) is preset before the call. Reading DATA takes the next
 * byte off the suite's receive queue and drops RX-full when it empties, as
 * the UART's one-byte receive buffer does.
 *
 * These are host tests of the register values only. A board has not run them.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_A)(void);
void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_B)(void);

#define UART(off) ((uintptr_t)TFW_MPS2_UART0_BASE + (off))
#define A_DATA    UART(0x00u)
#define A_STATE   UART(0x04u)
#define A_CTRL    UART(0x08u)
#define A_INT     UART(0x0Cu)
#define A_BAUDDIV UART(0x10u)
#define A_ICPR1   ((uintptr_t)0xE000E284u)
#define A_ISER1   ((uintptr_t)0xE000E104u)

/* A value no store of the driver's equals, so every store is seen. */
#define UNWRITTEN 0xA5A50000u

/* ---- the register file and its write log ---- */

#define CELLS 32
#define LOG   256

static struct { uintptr_t addr; volatile uint32_t val; } cell[CELLS];
static unsigned  ncell;
static struct { uintptr_t addr; uint32_t val; } wlog[LOG];
static unsigned  nlog;
static int       last = -1;          /* the cell handed out last ... */
static uint32_t  presented;          /* ... and what it held then */
static uint8_t   rxq[16];
static unsigned  rxq_head, rxq_len;
static uint32_t  primask;

static int find(uintptr_t addr)
{
    unsigned i;

    for (i = 0; i < ncell; i++) {
        if (cell[i].addr == addr) {
            return (int)i;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(ncell < CELLS, "register file full");
    cell[ncell].addr = addr;
    cell[ncell].val  = UNWRITTEN;
    return (int)ncell++;
}

/* The access before this one was a store if its cell changed. */
static void settle(void)
{
    if (last >= 0 && cell[last].val != presented) {
        TEST_ASSERT_TRUE_MESSAGE(nlog < LOG, "write log full");
        wlog[nlog].addr = cell[last].addr;
        wlog[nlog].val  = cell[last].val;
        nlog++;
    }
    last = -1;
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    int i;

    settle();
    i = find(addr);
    if (addr == A_DATA && rxq_len > 0u) {
        cell[i].val = rxq[rxq_head];
        rxq_head++;
        rxq_len--;
        if (rxq_len == 0u) {
            cell[find(A_STATE)].val &= ~(uint32_t)TFW_MPS2_UART_STATE_RXFULL;
        }
    }
    last      = i;
    presented = cell[i].val;
    return &cell[i].val;
}

uint32_t tfw_imx95_model_irq_save(void)
{
    uint32_t was = primask;

    primask = 1u;
    return was;
}

void tfw_imx95_model_irq_restore(uint32_t was) { primask = was; }
void tfw_imx95_model_irq_mask(uint32_t masked)  { primask = masked; }

static uint32_t reg(uintptr_t addr)
{
    settle();
    return cell[find(addr)].val;
}

static void preset(uintptr_t addr, uint32_t val)
{
    settle();
    cell[find(addr)].val = val;
}

static void rx_queue(const uint8_t *b, unsigned n)
{
    memcpy(rxq, b, n);
    rxq_head = 0u;
    rxq_len  = n;
}

/* The stores to addr since the log was last cleared, oldest first. */
static unsigned writes_to(uintptr_t addr, uint32_t *out, unsigned max)
{
    unsigned i, n = 0;

    settle();
    for (i = 0; i < nlog; i++) {
        if (wlog[i].addr == addr && n < max) {
            out[n++] = wlog[i].val;
        }
    }
    return n;
}

static void log_clear(void)
{
    settle();
    nlog = 0;
}

void setUp(void)
{
    ncell = 0;
    nlog  = 0;
    last  = -1;
    rxq_len = rxq_head = 0u;
    primask = 0u;
}

void tearDown(void) {}

static void init_ok(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    preset(A_STATE, 0u);
    log_clear();
}

static void irq_a(uint32_t intstatus)
{
    preset(A_INT, intstatus);
    TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_A)();
    settle();
}

/* ---- init ---- */

/* DDI 0479C 4.3: baud = PCLK / BAUDDIV; 20 MHz / 115200 = 173.6, so 173. */
static void test_init_sets_bauddiv_for_115200_at_20mhz(void)
{
    uint32_t w[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_BAUDDIV, w, 4));
    TEST_ASSERT_EQUAL_HEX32(173u, w[0]);
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_CPU_HZ / 115200u, w[0]);
}

/* CTRL is cleared before the divisor is set, then gets TX enable (bit 0),
 * RX enable (bit 1), TX interrupt enable (bit 2) and RX interrupt enable
 * (bit 3), and nothing else: no overrun interrupts, no high-speed test mode. */
static void test_init_disables_then_enables_tx_rx_and_their_interrupts(void)
{
    uint32_t w[4];
    unsigned i, ctrl_at[2], n = 0;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    TEST_ASSERT_EQUAL_UINT(2u, writes_to(A_CTRL, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x00u, w[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0Fu, w[1]);
    for (i = 0; i < nlog; i++) {
        if (wlog[i].addr == A_CTRL) {
            ctrl_at[n++] = i;
        } else if (wlog[i].addr == A_BAUDDIV) {
            TEST_ASSERT_EQUAL_UINT(1u, n);   /* between the two CTRL stores */
        }
    }
    TEST_ASSERT_EQUAL_UINT(nlog - 5u, ctrl_at[1]);  /* the 4 NVIC stores follow */
}

/* STATE's overrun flags (bits 2, 3) and INTCLEAR's TX and RX bits (0, 1) are
 * write-one-to-clear: init clears the stale ones. */
static void test_init_clears_overruns_and_pending_interrupts(void)
{
    uint32_t w[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_STATE, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x0Cu, w[0]);
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_INT, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x03u, w[0]);
}

/* External interrupts 32 and 33 are bits 0 and 1 of the second NVIC word:
 * each is cleared pending, then enabled. */
static void test_init_enables_both_uart0_lines_in_the_nvic(void)
{
    uint32_t w[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    TEST_ASSERT_EQUAL_UINT(2u, writes_to(A_ICPR1, w, 4));
    TEST_ASSERT_EQUAL_HEX32(1u << (TFW_MPS2_UART0_IRQ_A % 32), w[0]);
    TEST_ASSERT_EQUAL_HEX32(1u << (TFW_MPS2_UART0_IRQ_B % 32), w[1]);
    TEST_ASSERT_EQUAL_UINT(2u, writes_to(A_ISER1, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x1u, w[0]);
    TEST_ASSERT_EQUAL_HEX32(0x2u, w[1]);
}

/* BAUDDIV below 16 is invalid (DDI 0479C 4.3): a rate that needs one, and a
 * rate of 0, are refused before any register is touched. */
static void test_init_refuses_zero_and_a_divisor_below_16(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(TFW_CM_CPU_HZ / 15u));
    settle();
    TEST_ASSERT_EQUAL_UINT(0u, nlog);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(TFW_CM_CPU_HZ / 16u));
    TEST_ASSERT_EQUAL_HEX32(16u, reg(A_BAUDDIV));
}

/* ---- transmit ---- */

static void test_tx_try_writes_the_first_byte_to_data(void)
{
    uint32_t w[4];

    init_ok();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x5Au));
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_DATA, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x5Au, w[0]);
    TEST_ASSERT_EQUAL_HEX32(0u, primask);       /* the mask is put back */
}

/* While a byte is in flight the next waits for the TX interrupt, which
 * writes it to DATA and clears the TX interrupt in INTCLEAR. */
static void test_tx_irq_sends_the_queued_byte_and_clears_tx(void)
{
    uint32_t w[4];

    init_ok();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x11u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x22u));
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_DATA, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x11u, w[0]);

    log_clear();
    irq_a(TFW_MPS2_UART_INT_TX | 0x04u);     /* a TX-overrun bit rides along */
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_DATA, w, 4));
    TEST_ASSERT_EQUAL_HEX32(0x22u, w[0]);
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_INT, w, 4));
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_UART_INT_TX, w[0]);   /* TX only cleared */
}

/* The ring holds TX_RING - 1 bytes behind the one in DATA; the next is refused
 * and touches no register. */
static void test_tx_try_refuses_when_the_ring_is_full(void)
{
    unsigned i;
    uint32_t w[4];

    init_ok();
    for (i = 0; i < TFW_UART_TX_RING; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)i));
    }
    log_clear();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xEEu));
    TEST_ASSERT_EQUAL_UINT(0u, writes_to(A_DATA, w, 4));
}

/* Idle needs an empty ring, no byte in flight, and STATE's TX-full (bit 0)
 * clear: the last byte still in the UART's buffer is not idle. */
static void test_tx_idle_waits_for_the_ring_the_irq_and_tx_full(void)
{
    init_ok();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x33u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());      /* in flight */

    irq_a(TFW_MPS2_UART_INT_TX);                              /* ring empty */
    preset(A_STATE, TFW_MPS2_UART_STATE_TXFULL);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());      /* TX full */
    preset(A_STATE, TFW_MPS2_UART_STATE_RXFULL);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

/* ---- receive ---- */

/* RX-full (STATE bit 1) set: the service reads DATA until it drops, clears
 * the RX interrupt and RX overrun, and tfw_hal_uart_rx hands the bytes on. */
static void test_rx_irq_moves_data_to_the_ring_while_rx_full(void)
{
    static const uint8_t in[] = { 0x7Eu, 0x00u, 0xC3u };
    uint32_t w[4];
    uint8_t  b = 0;

    init_ok();
    rx_queue(in, sizeof in);
    preset(A_STATE, TFW_MPS2_UART_STATE_RXFULL);
    irq_a(TFW_MPS2_UART_INT_RX | 0x08u);       /* an RX-overrun bit rides along */
    TEST_ASSERT_EQUAL_UINT(0u, rxq_len);

    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_INT, w, 4));
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_UART_INT_RX, w[0]);
    TEST_ASSERT_EQUAL_UINT(1u, writes_to(A_STATE, w, 4));
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_UART_STATE_RXOVR, w[0]);
    TEST_ASSERT_EQUAL_UINT(0u, writes_to(A_DATA, w, 4));   /* no TX on RX */

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x7Eu, b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x00u, b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

/* RX-full clear: DATA is not read and nothing reaches the ring. */
static void test_rx_irq_reads_nothing_without_rx_full(void)
{
    static const uint8_t in[] = { 0x55u };
    uint8_t b = 0;

    init_ok();
    rx_queue(in, sizeof in);
    preset(A_STATE, 0u);
    irq_a(TFW_MPS2_UART_INT_RX);
    TEST_ASSERT_EQUAL_UINT(1u, rxq_len);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

/* The second line, interrupt 33, runs the same service. */
static void test_the_second_line_runs_the_same_service(void)
{
    static const uint8_t in[] = { 0x9Au };
    uint8_t b = 0;

    init_ok();
    rx_queue(in, sizeof in);
    preset(A_STATE, TFW_MPS2_UART_STATE_RXFULL);
    preset(A_INT, TFW_MPS2_UART_INT_RX);
    TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_B)();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x9Au, b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_bauddiv_for_115200_at_20mhz);
    RUN_TEST(test_init_disables_then_enables_tx_rx_and_their_interrupts);
    RUN_TEST(test_init_clears_overruns_and_pending_interrupts);
    RUN_TEST(test_init_enables_both_uart0_lines_in_the_nvic);
    RUN_TEST(test_init_refuses_zero_and_a_divisor_below_16);
    RUN_TEST(test_tx_try_writes_the_first_byte_to_data);
    RUN_TEST(test_tx_irq_sends_the_queued_byte_and_clears_tx);
    RUN_TEST(test_tx_try_refuses_when_the_ring_is_full);
    RUN_TEST(test_tx_idle_waits_for_the_ring_the_irq_and_tx_full);
    RUN_TEST(test_rx_irq_moves_data_to_the_ring_while_rx_full);
    RUN_TEST(test_rx_irq_reads_nothing_without_rx_full);
    RUN_TEST(test_the_second_line_runs_the_same_service);
    return UNITY_END();
}
