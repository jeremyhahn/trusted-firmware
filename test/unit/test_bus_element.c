/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * I2C_CONFIG on an ELEMENT build -- a separate binary from test_bus.c, for the
 * same reason test_atecc.c and test_alias.c are: the crypto tier is a
 * link-time choice, exactly like the HAL, and this is the one behaviour in
 * src/caps/bus.c that depends on which tier is linked. test_bus.c links the
 * software tier, which has no shared bus to protect and can never observe
 * this refusal; this binary links the real ATECC608A driver and provider
 * against the software element (test/model/atecc608a.c) as the I2C HAL, so
 * ag_crypto_tier() genuinely reports AG_CRYPTO_SECURE_ELEMENT rather than a
 * mock pretending to.
 *
 * bus.c is one translation unit for all three buses (see its own header
 * comment), so linking it here for I2C_CONFIG also requires SPI's and
 * 1-Wire's HAL functions to resolve. This suite never issues an SPI or
 * 1-Wire command, so those five are guard stubs in the same spirit as
 * test/integration/simavr/boot_harness.c's: reaching one is a wiring
 * mistake, not a case to handle.
 */
#include <stdio.h>
#include <stdlib.h>

#include "unity.h"
#include "bus.h"
#include "atecc608a_model.h"
#include "ag/crypto.h"
#include "ag/hal.h"
#include "ag/protocol.h"
#include "ag/status.h"

/* ag_crypto_tier() reports what ag_crypto_init() found, not a link-time
 * constant -- so the element must actually wake, through the same path a real
 * boot takes, before this build's tier is AG_CRYPTO_SECURE_ELEMENT. */
void setUp(void)
{
    ag_model_reset();
    ag_crypto_init();
}

void tearDown(void) {}

static ag_status_t trap(const char *what)
{
    fprintf(stderr, "test_bus_element called %s; this suite is I2C_CONFIG only\n", what);
    abort();
}

ag_status_t ag_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    (void)mode; (void)clkdiv; (void)lsb_first;
    return trap("the SPI init HAL");
}

ag_status_t ag_hal_spi_xfer(const ag_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    (void)cs; (void)wr; (void)rd; (void)len;
    return trap("the SPI transfer HAL");
}

ag_status_t ag_hal_ow_reset(const ag_pinref_t *p, uint8_t *out_presence)
{
    (void)p; (void)out_presence;
    return trap("the 1-Wire reset HAL");
}

ag_status_t ag_hal_ow_write(const ag_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    (void)p; (void)buf; (void)len;
    return trap("the 1-Wire write HAL");
}

ag_status_t ag_hal_ow_read(const ag_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p; (void)buf; (void)len;
    return trap("the 1-Wire read HAL");
}

static uint8_t     inbuf[4];
static uint8_t     outbuf[8];
static ag_reader_t r;
static ag_writer_t w;

static ag_status_t run_config(uint32_t hz)
{
    inbuf[0] = (uint8_t)(hz & 0xFFu);
    inbuf[1] = (uint8_t)((hz >> 8) & 0xFFu);
    inbuf[2] = (uint8_t)((hz >> 16) & 0xFFu);
    inbuf[3] = (uint8_t)((hz >> 24) & 0xFFu);
    ag_reader_init(&r, inbuf, (uint8_t)sizeof inbuf);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_i2c_cmd_config(&r, &w);
}

/*
 * THE ACTUAL REGRESSION. ag_atecc_wake() needs this exact TWI peripheral at
 * or below 100 kHz for its wake pulse (src/crypto/atecc608a/atecc.c), and
 * reasserts that rate before every wake regardless of what I2C_CONFIG last
 * set. Without this refusal, a host retuning the bus for an unrelated sensor
 * would have every later element operation silently fail until reboot --
 * which is exactly the bug a review of this issue's first pass found.
 */
static void test_i2c_config_is_refused_on_a_secure_element_build(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, run_config(100000uL));
}

/* Refused before the range check, not instead of it: a value that is ALSO out
 * of range must not be seen to fail for the wrong reason on this build. */
static void test_the_refusal_precedes_the_range_check(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, run_config(0uL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_i2c_config_is_refused_on_a_secure_element_build);
    RUN_TEST(test_the_refusal_precedes_the_range_check);
    return UNITY_END();
}
