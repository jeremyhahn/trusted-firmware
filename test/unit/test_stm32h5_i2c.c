/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 I2C master (src/hal/cortex-m/stm32h5/hal_bus.c) against a
 * register model of I2C4 and one device on its bus, since QEMU has no STM32H5.
 *
 * The driver reaches a register only through TFW_CM_REG. The model header
 * (test/model/cortex-m/h5_i2c_model.h) defines TFW_CM_REG, and cm_regs.h's
 * guard, before hal_bus.c is included, so every access goes to
 * tfw_h5_i2c_model_reg() and the driver compiles unchanged. That function
 * hands out one cell per access; the next access commits it, so a store is
 * seen as the cell's value having changed (TXDR's cell is reset to a value no
 * byte store can write, so every byte is seen).
 *
 * Every address and bit here is a literal, written from the I2C_TypeDef
 * layout and the I2C_CR2_*, I2C_ISR_*, I2C_ICR_* bits of ST's stm32h573xx.h
 * (docs/reference/README.md), not taken from soc_regs.h: a register or bit the
 * driver moves becomes an unmodelled access, and the test that drove it fails.
 *
 * The bus log is one token per event: S<addr byte> a START or Sr a repeated
 * START, W<byte> a byte sent, R<byte> a byte received, P a STOP, X the block
 * reset (PE cleared).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../model/cortex-m/h5_i2c_model.h"

#include "hal_bus.c"

#define I2C4     0x44002C00u
#define CR1      (I2C4 + 0x00u)
#define CR2      (I2C4 + 0x04u)
#define TIMINGR  (I2C4 + 0x10u)
#define ISR      (I2C4 + 0x18u)
#define ICR      (I2C4 + 0x1Cu)
#define RXDR     (I2C4 + 0x24u)
#define TXDR     (I2C4 + 0x28u)
#define APB3ENR  0x44020CA8u
#define AHB2ENR  0x44020C8Cu
#define GPIOB    0x42020400u

#define CR1_PE    0x00000001u
#define CR2_RDWRN 0x00000400u
#define CR2_START 0x00002000u
#define CR2_STOP  0x00004000u
#define F_TXE     0x00000001u
#define F_TXIS    0x00000002u
#define F_RXNE    0x00000004u
#define F_NACKF   0x00000010u
#define F_STOPF   0x00000020u
#define F_TC      0x00000040u
#define F_ARLO    0x00000200u
#define F_BUSY    0x00008000u
#define TXDR_IDLE 0xFFFFFFFFu

#define DEV       0x60u            /* the device's 7-bit address */
#define NONE      0xFFu

static struct {
    uint32_t  cell;
    uintptr_t pending;
    uint32_t  given;
    uint32_t  cr1, cr2, timingr, apb3enr, ahb2enr;
    uint32_t  gpio[10];
    uint32_t  flags;
    uint8_t   on_bus, reading, left, sent;
    uint8_t   rx[8];
    uint8_t   rx_at;
    /* the scenario */
    uint8_t   nack_byte;           /* the write byte the device NACKs, or NONE */
    uint8_t   held_busy;           /* BUSY never clears: another master or a stuck line */
    uint8_t   stall;               /* START taken, then no flag ever comes */
    uint8_t   stop_hangs;          /* STOP never completes */
    uint8_t   arlo;                /* arbitration lost at the first byte */
    char      log[256];
    unsigned  stray;
    unsigned  delays;
} m;

/* Appends one token to the bus log: the tag, and the byte when there is one. */
static void say(const char *tag, int with_byte, unsigned v)
{
    size_t n = strlen(m.log);

    snprintf(m.log + n, sizeof m.log - n, "%s%s", n ? " " : "", tag);
    if (with_byte) {
        n = strlen(m.log);
        snprintf(m.log + n, sizeof m.log - n, "%02x", v & 0xFFu);
    }
}

void tfw_hal_delay_us(uint16_t us)
{
    m.delays += us;
}

static void on_cr2(uint32_t v)
{
    m.cr2 = v;
    if ((v & CR2_START) != 0u) {
        uint8_t addr = (uint8_t)(v & 0xFEu);
        uint8_t rd   = (uint8_t)((v & CR2_RDWRN) != 0u);

        if (m.on_bus) {
            say("Sr", 1, (unsigned)(addr | rd));
        } else {
            say("S", 1, (unsigned)(addr | rd));
        }
        m.on_bus = 1u;
        m.cr2 &= ~CR2_START;
        m.flags &= ~(F_TC | F_TXIS | F_RXNE);
        m.left    = (uint8_t)(v >> 16);
        m.reading = rd;
        if (m.stall) {
            return;
        }
        if (m.arlo) {
            m.flags |= F_ARLO;
            return;
        }
        if ((addr >> 1) != DEV) {
            m.flags |= F_NACKF;
        } else if (rd) {
            m.flags |= F_RXNE;
        } else {
            m.flags |= F_TXIS;
        }
    }
    if ((v & CR2_STOP) != 0u) {
        m.cr2 &= ~CR2_STOP;
        say("P", 0, 0u);
        if (!m.stop_hangs) {
            m.on_bus = 0u;
            m.flags  = (m.flags & ~(F_TC | F_TXIS | F_RXNE)) | F_STOPF;
        }
    }
}

static void on_txdr(uint8_t b)
{
    say("W", 1, b);
    m.flags &= ~F_TXIS;
    if (m.sent++ == m.nack_byte) {
        m.flags |= F_NACKF;
        return;
    }
    if (--m.left == 0u) {
        m.flags |= F_TC;
    } else {
        m.flags |= F_TXIS;
    }
}

static void on_cr1(uint32_t v)
{
    if ((m.cr1 & CR1_PE) != 0u && (v & CR1_PE) == 0u) {
        say("X", 0, 0u);
        m.flags  = 0u;
        m.on_bus = 0u;
    }
    m.cr1 = v;
}

/* Commits the access tfw_h5_i2c_model_reg last handed out: a changed cell is a store. */
static void commit(void)
{
    uintptr_t a = m.pending;
    uint32_t  v = m.cell;

    m.pending = 0u;
    if (a == 0u || v == m.given) {
        return;
    }
    switch (a) {
    case CR1:     on_cr1(v); break;
    case CR2:     on_cr2(v); break;
    case TIMINGR: m.timingr = v; break;
    case ICR:     m.flags &= ~(v & 0x3F38u); break;
    case TXDR:    on_txdr((uint8_t)v); break;
    case APB3ENR: m.apb3enr = v; break;
    case AHB2ENR: m.ahb2enr = v; break;
    default:
        if (a >= GPIOB && a < GPIOB + 0x28u) {
            m.gpio[(a - GPIOB) / 4u] = v;
        } else {
            m.stray++;
        }
        break;
    }
}

volatile uint32_t *tfw_h5_i2c_model_reg(uintptr_t a)
{
    commit();
    m.pending = a;
    switch (a) {
    case CR1:     m.cell = m.cr1; break;
    case CR2:     m.cell = m.cr2; break;
    case TIMINGR: m.cell = m.timingr; break;
    case ICR:     m.cell = 0u; break;
    case TXDR:    m.cell = TXDR_IDLE; break;
    case APB3ENR: m.cell = m.apb3enr; break;
    case AHB2ENR: m.cell = m.ahb2enr; break;
    case ISR:
        m.cell = m.flags | F_TXE | ((m.on_bus || m.held_busy) ? F_BUSY : 0u);
        break;
    case RXDR:
        /* A read: the byte, and the device sends the next one. */
        m.cell = m.rx[m.rx_at];
        say("R", 1, m.rx[m.rx_at]);
        m.rx_at++;
        m.flags &= ~F_RXNE;
        if (--m.left == 0u) {
            m.flags |= F_TC;
        } else {
            m.flags |= F_RXNE;
        }
        break;
    default:
        if (a >= GPIOB && a < GPIOB + 0x28u) {
            m.cell = m.gpio[(a - GPIOB) / 4u];
        } else {
            m.cell = 0u;
            m.stray++;
        }
        break;
    }
    m.given = m.cell;
    return &m.cell;
}

void setUp(void)
{
    memset(&m, 0, sizeof m);
    m.nack_byte = NONE;
    m.rx[0] = 0x11u;
    m.rx[1] = 0x22u;
    m.rx[2] = 0x33u;
}

void tearDown(void) {}

static void done(void)
{
    commit();
    TEST_ASSERT_EQUAL_UINT(0u, m.stray);
}

static void test_init_routes_i2c4_and_loads_the_timing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    done();
    TEST_ASSERT_EQUAL_HEX32(0x60702729u, m.timingr);
    TEST_ASSERT_EQUAL_HEX32(CR1_PE, m.cr1);
    TEST_ASSERT_EQUAL_HEX32(1u << 8, m.apb3enr & (1u << 8));         /* I2C4EN */
    TEST_ASSERT_EQUAL_HEX32(1u << 1, m.ahb2enr & (1u << 1));         /* GPIOBEN */
    TEST_ASSERT_EQUAL_HEX32(0xAu << 16, m.gpio[0] & (0xFu << 16));   /* MODER 8, 9 AF */
    TEST_ASSERT_EQUAL_HEX32(0x3u << 8, m.gpio[1] & (0x3u << 8));     /* OTYPER open drain */
    TEST_ASSERT_EQUAL_HEX32(0x5u << 16, m.gpio[3] & (0xFu << 16));   /* PUPDR pull-up */
    TEST_ASSERT_EQUAL_HEX32(0x66u, m.gpio[9] & 0xFFu);               /* AFRH AF6 */
}

static void test_init_rates(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000uL));
    done();
    TEST_ASSERT_EQUAL_HEX32(0x10A11626u, m.timingr);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(399999uL));          /* never faster than asked */
    done();
    TEST_ASSERT_EQUAL_HEX32(0x60702729u, m.timingr);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(99999uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(0uL));
}

static void test_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV, wr, 2u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 W01 W02 P", m.log);
    TEST_ASSERT_FALSE(m.on_bus);
}

static void test_write_then_read_is_a_repeated_start(void)
{
    const uint8_t wr[] = { 0x30u };
    uint8_t       rd[3] = { 0 };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV, wr, 1u, rd, 3u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 W30 Src1 R11 R22 R33 P", m.log);
    TEST_ASSERT_EQUAL_HEX8(0x11u, rd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, rd[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33u, rd[2]);
}

static void test_read_only(void)
{
    uint8_t rd[1] = { 0 };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV, NULL, 0u, rd, 1u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc1 R11 P", m.log);
}

static void test_nothing_to_send_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(DEV, NULL, 0u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("", m.log);
}

static void test_address_nack_stops(void)
{
    const uint8_t wr[] = { 0x01u };
    uint8_t       rd[1];

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x21u, wr, 1u, rd, 1u));
    done();
    TEST_ASSERT_EQUAL_STRING("S42 P", m.log);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags & (F_NACKF | F_STOPF));      /* cleared */
}

static void test_read_address_nack_stops(void)
{
    uint8_t rd[1];

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x21u, NULL, 0u, rd, 1u));
    done();
    TEST_ASSERT_EQUAL_STRING("S43 P", m.log);
}

static void test_data_nack_stops_and_skips_the_read(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };
    uint8_t       rd[1];

    m.nack_byte = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(DEV, wr, 3u, rd, 1u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 W01 W02 P", m.log);
}

static void test_busy_bus_times_out_without_a_start(void)
{
    const uint8_t wr[] = { 0x01u };

    m.held_busy = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV, wr, 1u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("", m.log);
    TEST_ASSERT_TRUE(m.delays >= H5_I2C_POLL_US);
}

static void test_stalled_transfer_times_out_and_stops(void)
{
    const uint8_t wr[] = { 0x01u };

    m.stall = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV, wr, 1u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 P", m.log);
    TEST_ASSERT_FALSE(m.on_bus);
}

static void test_stop_that_never_completes_resets_the_block(void)
{
    const uint8_t wr[] = { 0x01u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    memset(m.log, 0, sizeof m.log);
    m.stop_hangs = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV, wr, 1u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 W01 P X", m.log);
    TEST_ASSERT_EQUAL_HEX32(CR1_PE, m.cr1);                         /* enabled again */
}

static void test_lost_arbitration_times_out_and_stops(void)
{
    const uint8_t wr[] = { 0x01u };

    m.arlo = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_i2c_xfer(DEV, wr, 1u, NULL, 0u));
    done();
    TEST_ASSERT_EQUAL_STRING("Sc0 P", m.log);
    TEST_ASSERT_EQUAL_HEX32(0u, m.flags & F_ARLO);                  /* cleared */
}

static void test_spi_and_onewire_refuse(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_init(0u, 0u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(NULL, &b, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(NULL, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(NULL, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(NULL, &b, 1u));
    done();
    TEST_ASSERT_EQUAL_STRING("", m.log);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_routes_i2c4_and_loads_the_timing);
    RUN_TEST(test_init_rates);
    RUN_TEST(test_write);
    RUN_TEST(test_write_then_read_is_a_repeated_start);
    RUN_TEST(test_read_only);
    RUN_TEST(test_nothing_to_send_touches_nothing);
    RUN_TEST(test_address_nack_stops);
    RUN_TEST(test_read_address_nack_stops);
    RUN_TEST(test_data_nack_stops_and_skips_the_read);
    RUN_TEST(test_busy_bus_times_out_without_a_start);
    RUN_TEST(test_stalled_transfer_times_out_and_stops);
    RUN_TEST(test_stop_that_never_completes_resets_the_block);
    RUN_TEST(test_lost_arbitration_times_out_and_stops);
    RUN_TEST(test_spi_and_onewire_refuse);
    return UNITY_END();
}
