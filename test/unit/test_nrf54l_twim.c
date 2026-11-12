/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L I2C master (src/hal/cortex-m/nrf54l/hal_bus.c) against a model of
 * TWIM21: the driver is the image's, unchanged; its register accesses reach
 * the model through test/model/cortex-m/nrf54l/cm_regs.h, which hands back a
 * cell per access. A write shows as the cell changed when the next access (or
 * model_settle) comes, so the model acts on it then, as the part would have.
 *
 * The device on the bus is scripted: present or not (ANACK), the write byte
 * it refuses (DNACK), the bytes it answers, or a bus that never stops.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"

/* TWIM21's base and the offsets of the registers the model acts on
 * (nrf54l15_global.h, nrf54l15_types.h). */
#define TWIM_BASE      0x500C7000u
#define TWIM_SPAN      0x800u
#define O_TASKS_STOP   0x004u
#define O_TASKS_RESUME 0x010u
#define O_TASKS_RX     0x028u
#define O_TASKS_TX     0x050u
#define O_EV_STOPPED   0x104u
#define O_EV_ERROR     0x114u
#define O_EV_LASTRX    0x134u
#define O_EV_LASTTX    0x138u
#define O_SHORTS       0x200u
#define O_INTEN        0x300u
#define O_ERRORSRC     0x4C4u
#define O_ENABLE       0x500u
#define O_FREQUENCY    0x524u
#define O_ADDRESS      0x588u
#define O_PSEL_SCL     0x600u
#define O_PSEL_SDA     0x604u
#define O_RX_PTR       0x704u
#define O_RX_MAXCNT    0x708u
#define O_TX_PTR       0x73Cu
#define O_TX_MAXCNT    0x740u

#define SH_LASTTX_RXSTART (1u << 7)
#define SH_LASTTX_STOP    (1u << 9)
#define SH_LASTRX_STOP    (1u << 12)
#define ES_ANACK          (1u << 1)
#define ES_DNACK          (1u << 2)

/* The TWIM as the model holds it, and the bus it drives. */
typedef struct {
    uint32_t tasks_starttx;      /* counts of each task triggered */
    uint32_t tasks_startrx;
    uint32_t tasks_stop;
    uint32_t events_stopped;
    uint32_t events_error;
    uint32_t errorsrc;
    uint32_t shorts;
    uint32_t address;
    uint32_t txd_ptr;
    uint32_t txd_maxcnt;
    uint32_t rxd_ptr;
    uint32_t rxd_maxcnt;
    uint32_t regs[TWIM_SPAN / 4u]; /* every other register, by offset */

    /* The device. */
    int      present;
    int      dnack_at;           /* write byte refused, -1 none */
    int      hang;               /* STOPPED never raised */
    uint8_t  answer[16];

    /* What crossed the bus. */
    uint8_t  sent[16];
    uint32_t nsent;
    uint32_t nread;
    uint32_t restarts;           /* repeated STARTs */
    uint32_t stops;              /* STOP conditions sent */
} twim_model_t;

static twim_model_t s_twim;

static uintptr_t s_last_addr;
static int       s_have_last;
static uint32_t  s_presented;
static uint32_t  s_cell;
static uint32_t  s_delays;

/* The host's addresses are 64-bit and the TWIM's pointers 32: the buffers are
 * statics of this executable, so their high half is the model's own. */
static uint8_t *dma_ptr(uint32_t lo)
{
    uintptr_t hi = (uintptr_t)&s_twim & ~(uintptr_t)0xFFFFFFFFu;
    return (uint8_t *)(hi | (uintptr_t)lo);
}

static uint32_t *field(uint32_t off)
{
    switch (off) {
    case O_EV_STOPPED: return &s_twim.events_stopped;
    case O_EV_ERROR:   return &s_twim.events_error;
    case O_ERRORSRC:   return &s_twim.errorsrc;
    case O_SHORTS:     return &s_twim.shorts;
    case O_ADDRESS:    return &s_twim.address;
    case O_TX_PTR:     return &s_twim.txd_ptr;
    case O_TX_MAXCNT:  return &s_twim.txd_maxcnt;
    case O_RX_PTR:     return &s_twim.rxd_ptr;
    case O_RX_MAXCNT:  return &s_twim.rxd_maxcnt;
    default:           return &s_twim.regs[off / 4u];
    }
}

static void bus_stop(void)
{
    s_twim.stops++;
    if (!s_twim.hang) {
        s_twim.events_stopped = 1u;
    }
}

static void bus_error(uint32_t src)
{
    s_twim.errorsrc    |= src;
    s_twim.events_error = 1u;
}

static void bus_read(void)
{
    uint8_t *p = dma_ptr(s_twim.rxd_ptr);
    uint32_t i;

    if (!s_twim.present) {
        bus_error(ES_ANACK);
        return;
    }
    for (i = 0u; i < s_twim.rxd_maxcnt; i++) {
        p[i] = s_twim.answer[i];
        s_twim.nread++;
    }
    s_twim.regs[O_EV_LASTRX / 4u] = 1u;
    if (s_twim.shorts & SH_LASTRX_STOP) {
        bus_stop();
    }
}

static void bus_write(void)
{
    const uint8_t *p = dma_ptr(s_twim.txd_ptr);
    uint32_t       i;

    if (!s_twim.present) {
        bus_error(ES_ANACK);
        return;
    }
    for (i = 0u; i < s_twim.txd_maxcnt; i++) {
        s_twim.sent[s_twim.nsent++] = p[i];
        if ((int)i == s_twim.dnack_at) {
            bus_error(ES_DNACK);
            return;
        }
    }
    s_twim.regs[O_EV_LASTTX / 4u] = 1u;
    if (s_twim.shorts & SH_LASTTX_RXSTART) {
        s_twim.restarts++;
        s_twim.tasks_startrx++;
        bus_read();
    } else if (s_twim.shorts & SH_LASTTX_STOP) {
        bus_stop();
    }
}

static void model_write(uintptr_t addr, uint32_t v)
{
    uint32_t off;

    if (addr < TWIM_BASE || addr >= TWIM_BASE + TWIM_SPAN) {
        return; /* the pins' PIN_CNF: not modelled */
    }
    off = (uint32_t)(addr - TWIM_BASE);
    switch (off) {
    case O_TASKS_STOP:   s_twim.tasks_stop++; bus_stop(); break;
    case O_TASKS_RESUME: break;
    case O_TASKS_TX:     s_twim.tasks_starttx++; bus_write(); break;
    case O_TASKS_RX:     s_twim.tasks_startrx++; bus_read(); break;
    case O_ERRORSRC:     s_twim.errorsrc &= ~v; break;
    default:             *field(off) = v; break;
    }
}

static uint32_t model_view(uintptr_t addr)
{
    if (addr < TWIM_BASE || addr >= TWIM_BASE + TWIM_SPAN) {
        return 0u;
    }
    return *field((uint32_t)(addr - TWIM_BASE));
}

static void model_settle(void)
{
    if (s_have_last && s_cell != s_presented) {
        model_write(s_last_addr, s_cell);
    }
    s_have_last = 0;
}

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr)
{
    model_settle();
    s_last_addr = addr;
    s_have_last = 1;
    s_presented = model_view(addr);
    s_cell      = s_presented;
    return &s_cell;
}

void tfw_hal_delay_us(uint16_t us)
{
    s_delays += us;
}

static uint8_t s_rd[16];

static tfw_status_t xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen, uint8_t rlen)
{
    tfw_status_t st = tfw_hal_i2c_xfer(addr7, wr, wlen, rlen ? s_rd : NULL, rlen);

    model_settle();
    return st;
}

void setUp(void)
{
    memset(&s_twim, 0, sizeof s_twim);
    memset(s_rd, 0, sizeof s_rd);
    s_twim.present  = 1;
    s_twim.dnack_at = -1;
    s_have_last     = 0;
    s_delays        = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    model_settle();
}

void tearDown(void) { }

static void test_twim_init_sets_up_the_bus(void)
{
    TEST_ASSERT_EQUAL_HEX32(6u, s_twim.regs[O_ENABLE / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0x01980000u, s_twim.regs[O_FREQUENCY / 4u]);
    TEST_ASSERT_EQUAL_HEX32((1u << 5) | 10u, s_twim.regs[O_PSEL_SCL / 4u]);
    TEST_ASSERT_EQUAL_HEX32((1u << 5) | 8u, s_twim.regs[O_PSEL_SDA / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0u, s_twim.regs[O_INTEN / 4u]);
}

static void test_twim_init_rates(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000uL));
    model_settle();
    TEST_ASSERT_EQUAL_HEX32(0x06400000u, s_twim.regs[O_FREQUENCY / 4u]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(250000uL));
    model_settle();
    TEST_ASSERT_EQUAL_HEX32(0x04000000u, s_twim.regs[O_FREQUENCY / 4u]);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(50000uL));
}

static void test_twim_write_acked(void)
{
    static const uint8_t wr[2] = { 0x01u, 0x02u };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 2u, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x60u, s_twim.address);
    TEST_ASSERT_EQUAL_UINT32(2u, s_twim.txd_maxcnt);
    TEST_ASSERT_EQUAL_UINT32(2u, s_twim.nsent);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, s_twim.sent, 2);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_starttx);
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.tasks_startrx);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.stops);
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.tasks_stop);
}

static void test_twim_read(void)
{
    static const uint8_t want[3] = { 0xAAu, 0x55u, 0x0Fu };

    memcpy(s_twim.answer, want, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x35u, NULL, 0u, 3u));
    TEST_ASSERT_EQUAL_HEX32(0x35u, s_twim.address);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, s_rd, 3);
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.tasks_starttx);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_startrx);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.stops);
}

static void test_twim_write_then_read_repeated_start(void)
{
    static const uint8_t wr[1]   = { 0x07u };
    static const uint8_t want[2] = { 0x12u, 0x34u };

    memcpy(s_twim.answer, want, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, 2u));
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.nsent);
    TEST_ASSERT_EQUAL_HEX8(0x07u, s_twim.sent[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, s_rd, 2);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.restarts);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_starttx);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.stops);
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.shorts);
}

static void test_twim_address_nack(void)
{
    static const uint8_t wr[1] = { 0x00u };

    s_twim.present = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, wr, 1u, 2u));
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_stop);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.stops);
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.restarts);
    TEST_ASSERT_EQUAL_HEX32(0u, s_twim.errorsrc);
}

static void test_twim_read_address_nack(void)
{
    s_twim.present = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, NULL, 0u, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_stop);
}

static void test_twim_data_nack(void)
{
    static const uint8_t wr[3] = { 0x01u, 0x02u, 0x03u };

    s_twim.dnack_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x60u, wr, 3u, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, s_twim.nsent);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_stop);
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.stops);
}

static void test_twim_never_stopped_times_out(void)
{
    static const uint8_t wr[1] = { 0x01u };

    s_twim.hang = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, s_twim.tasks_stop);
    TEST_ASSERT_TRUE(s_delays > 0u);
}

static void test_twim_empty_transaction_does_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, NULL, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, s_twim.tasks_starttx + s_twim.tasks_startrx + s_twim.stops);
}

static void test_spi_and_onewire_refuse(void)
{
    tfw_pinref_t p;
    uint8_t      b = 0u;

    memset(&p, 0, sizeof p);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_init(0u, 0u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(&p, &b, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(&p, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(&p, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(&p, &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_twim_init_sets_up_the_bus);
    RUN_TEST(test_twim_init_rates);
    RUN_TEST(test_twim_write_acked);
    RUN_TEST(test_twim_read);
    RUN_TEST(test_twim_write_then_read_repeated_start);
    RUN_TEST(test_twim_address_nack);
    RUN_TEST(test_twim_read_address_nack);
    RUN_TEST(test_twim_data_nack);
    RUN_TEST(test_twim_never_stopped_times_out);
    RUN_TEST(test_twim_empty_transaction_does_nothing);
    RUN_TEST(test_spi_and_onewire_refuse);
    return UNITY_END();
}
