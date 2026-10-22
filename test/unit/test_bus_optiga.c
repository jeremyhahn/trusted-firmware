/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * I2C_XFER on a build that links the OPTIGA Trust M transport -- a separate
 * binary from test_bus.c, for the reason test_bus_element.c is one: which
 * addresses bus.c refuses depends on the object list. test_bus.c links
 * bus_held_none.c and sees 0x30 reach the bus; this binary links
 * src/crypto/optiga/optiga_i2c.c in its place, and 0x30 is the element's.
 *
 * The I2C HAL here records the address it was asked for. SPI's and 1-Wire's
 * HAL functions resolve bus.c's other handlers, which this suite never
 * issues: reaching one is a wiring mistake, as in test_bus_element.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "bus.h"
#include "optiga_i2c.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/status.h"

static unsigned xfers;
static uint8_t  last_addr;

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    (void)hz;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                              uint8_t *rd, uint8_t rlen)
{
    (void)wr; (void)wlen;
    xfers++;
    last_addr = addr7;
    if (rlen) {
        memset(rd, 0xA5, rlen);
    }
    return TFW_OK;
}

void tfw_hal_delay_us(uint16_t us)
{
    (void)us;
}

/* bus.c's I2C_CONFIG asks the tier; no provider is linked, and this suite
 * never sends I2C_CONFIG. */
tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return TFW_CRYPTO_SOFTWARE;
}

static tfw_status_t trap(const char *what)
{
    fprintf(stderr, "test_bus_optiga called %s; this suite is I2C_XFER only\n", what);
    abort();
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    (void)mode; (void)clkdiv; (void)lsb_first;
    return trap("the SPI init HAL");
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                              uint8_t *rd, uint8_t len)
{
    (void)cs; (void)wr; (void)rd; (void)len;
    return trap("the SPI transfer HAL");
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    (void)p; (void)out_presence;
    return trap("the 1-Wire reset HAL");
}

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    (void)p; (void)buf; (void)len;
    return trap("the 1-Wire write HAL");
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p; (void)buf; (void)len;
    return trap("the 1-Wire read HAL");
}

static uint8_t      inbuf[4];
static uint8_t      outbuf[8];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    xfers     = 0u;
    last_addr = 0xFFu;
}

void tearDown(void) {}

/* [addr][read_len][one write byte] */
static tfw_status_t xfer(uint8_t addr, uint8_t rlen)
{
    inbuf[0] = addr;
    inbuf[1] = rlen;
    inbuf[2] = 0x00u;
    tfw_reader_init(&r, inbuf, 3u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_i2c_cmd_xfer(&r, &w);
}

static void test_the_optiga_address_is_refused_and_never_reaches_the_bus(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, xfer(TFW_OPTIGA_ADDR, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, xfer(TFW_OPTIGA_ADDR, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, xfers);
}

static void test_the_optiga_address_is_0x30(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x30u, TFW_OPTIGA_ADDR);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, xfer(0x30u, 1u));
}

static void test_its_neighbours_reach_the_bus(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x2Fu, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x2Fu, last_addr);
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x31u, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x31u, last_addr);
    TEST_ASSERT_EQUAL_UINT(2u, xfers);
}

/* The transport adds an address; the element's and the reserved one stay
 * refused beside it. */
static void test_the_element_and_reserved_addresses_stay_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, xfer(TFW_I2C_RESERVED_ADDR, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, xfer(TFW_ELEMENT_I2C_ADDR, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, xfers);
}

static void test_the_transport_holds_its_address_and_no_other(void)
{
    unsigned a;

    for (a = 0u; a <= 0x7Fu; a++) {
        TEST_ASSERT_EQUAL_UINT8(a == TFW_OPTIGA_ADDR ? 1u : 0u, tfw_bus_i2c_held((uint8_t)a));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_optiga_address_is_refused_and_never_reaches_the_bus);
    RUN_TEST(test_the_optiga_address_is_0x30);
    RUN_TEST(test_its_neighbours_reach_the_bus);
    RUN_TEST(test_the_element_and_reserved_addresses_stay_refused);
    RUN_TEST(test_the_transport_holds_its_address_and_no_other);
    return UNITY_END();
}
