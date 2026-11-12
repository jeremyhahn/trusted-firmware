/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 I2C master (src/hal/cortex-m/rp2350/hal_bus.c, row rp2350) over
 * the register model of I2C0's DW_apb_i2c in test/model/cortex-m/rp2350/
 * (i2c_model.c), as test_imx95_hal.c runs the i.MX 95 files over
 * test/model/imx95_model.c. The driver compiles unchanged. One device sits on
 * the modelled bus; the model logs what reaches the wire (S, Sr, the address
 * byte, data, r for a byte read, P), so each test checks the transaction the
 * part would put out.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "soc_calc.h"
#include "i2c_model.h"

void setUp(void)
{
    rp_i2c_reset();
    rp_i2c.dev_addr = 0x60u;
    rp_i2c.nack_data_at = 0xFFu;
    rp_i2c.dev_tx[0] = 0xA5u;
    rp_i2c.dev_tx[1] = 0x5Au;
    rp_i2c.dev_tx[2] = 0x3Cu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000uL));
    rp_i2c_settle();
}

void tearDown(void) {}

static tfw_status_t xfer(uint8_t a, const uint8_t *wr, uint8_t wl, uint8_t *rd, uint8_t rl)
{
    tfw_status_t st = tfw_hal_i2c_xfer(a, wr, wl, rd, rl);

    rp_i2c_settle();
    return st;
}

/* ---- init */

static void test_init_programs_the_fast_mode_master_and_its_pins(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x65u, rp_i2c.con);
    TEST_ASSERT_EQUAL_UINT32(7u, rp_i2c.hcnt);
    TEST_ASSERT_EQUAL_UINT32(17u, rp_i2c.lcnt);
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.spklen);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.mask);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.enable);
    TEST_ASSERT_EQUAL_UINT32(TFW_RP_FUNC_I2C, *rp_i2c_other(0x40028024u));   /* GPIO4 CTRL */
    TEST_ASSERT_EQUAL_UINT32(TFW_RP_FUNC_I2C, *rp_i2c_other(0x4002802Cu));   /* GPIO5 CTRL */
    TEST_ASSERT_EQUAL_HEX32(0x5Au, *rp_i2c_other(0x40038014u));             /* GPIO4 pad */
    TEST_ASSERT_EQUAL_HEX32(0x5Au, *rp_i2c_other(0x40038018u));             /* GPIO5 pad */
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_RESET_I2C0, *rp_i2c_other(0x40023000u)); /* reset released */
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.cfg_while_enabled);
}

static void test_init_refuses_a_rate_it_cannot_make(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(0uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(1000001uL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_i2c_init(10uL));
}

/* ---- transfers */

static void test_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 3u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 02 03 P", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT8(3u, rp_i2c.dev_rx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, rp_i2c.dev_rx, 3u);
    TEST_ASSERT_EQUAL_UINT32(0x60u, rp_i2c.tar);
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.stops);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.cfg_while_enabled);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.raw & TFW_RP_IC_INTR_STOP_DET);
}

static void test_read(void)
{
    uint8_t rd[3] = { 0 };
    const uint8_t want[] = { 0xA5u, 0x5Au, 0x3Cu };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, NULL, 0u, rd, 3u));
    TEST_ASSERT_EQUAL_STRING(" S C1 rA5 r5A r3C P", rp_i2c_log());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 3u);
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.stops);
}

static void test_write_then_read_with_restart(void)
{
    const uint8_t wr[] = { 0x10u };
    uint8_t rd[2] = { 0 };
    const uint8_t want[] = { 0xA5u, 0x5Au };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, rd, 2u));
    TEST_ASSERT_EQUAL_STRING(" S C0 10 Sr C1 rA5 r5A P", rp_i2c_log());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 2u);
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.stops);
}

static void test_two_transfers_to_two_addresses(void)
{
    const uint8_t wr[] = { 0x55u };

    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x61u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 55 P S C2 P S C0 55 P", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.cfg_while_enabled);
}

static void test_empty_transfer_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", rp_i2c_log());
}

/* ---- aborts */

static void test_address_nack_on_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, wr, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S 44 P", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT8(0u, rp_i2c.dev_rx_n);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.raw);       /* TX_ABRT and STOP_DET cleared */
}

static void test_address_nack_on_read(void)
{
    uint8_t rd[2] = { 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, NULL, 0u, rd, 2u));
    TEST_ASSERT_EQUAL_STRING(" S 45 P", rp_i2c_log());
}

static void test_data_nack_stops_the_write(void)
{
    const uint8_t wr[] = { 0x01u, 0x02u, 0x03u };
    uint8_t rd[1] = { 0 };

    rp_i2c.nack_data_at = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x60u, wr, 3u, rd, 1u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 02 P", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT8(1u, rp_i2c.dev_rx_n);
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.stops);
}

static void test_abort_without_a_stop_is_ended_by_abort(void)
{
    const uint8_t wr[] = { 0x01u };

    rp_i2c.no_stop_on_abrt = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, xfer(0x22u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S 44 P", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.abort_reqs);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.enable);
}

/* ---- a bus that does not come free */

static void test_stuck_bus_times_out_and_aborts(void)
{
    const uint8_t wr[] = { 0x01u };

    rp_i2c.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.abort_reqs);
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.enable & TFW_RP_IC_ENABLE_ENABLE);
    TEST_ASSERT_EQUAL_STRING("", rp_i2c_log());
}

static void test_stuck_bus_on_read_times_out(void)
{
    uint8_t rd[1] = { 0 };

    rp_i2c.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, NULL, 0u, rd, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, rp_i2c.abort_reqs);
}

static void test_controller_that_never_goes_idle_times_out(void)
{
    const uint8_t wr[] = { 0x01u };

    rp_i2c.never_idle = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("", rp_i2c_log());
    TEST_ASSERT_EQUAL_UINT32(0u, rp_i2c.tar);       /* never addressed */
}

static void test_bus_recovers_after_a_timeout(void)
{
    const uint8_t wr[] = { 0x01u };

    rp_i2c.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, xfer(0x60u, wr, 1u, NULL, 0u));
    rp_i2c.stuck = 0u;
    rp_i2c.in_txn = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, xfer(0x60u, wr, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_STRING(" S C0 01 P", rp_i2c_log());
}

/* ---- the buses this SoC does not carry */

static void test_spi_and_onewire_are_unsupported(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_init(0u, 0u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(NULL, &b, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_reset(NULL, &b));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_write(NULL, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_ow_read(NULL, &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_programs_the_fast_mode_master_and_its_pins);
    RUN_TEST(test_init_refuses_a_rate_it_cannot_make);
    RUN_TEST(test_write);
    RUN_TEST(test_read);
    RUN_TEST(test_write_then_read_with_restart);
    RUN_TEST(test_two_transfers_to_two_addresses);
    RUN_TEST(test_empty_transfer_touches_nothing);
    RUN_TEST(test_address_nack_on_write);
    RUN_TEST(test_address_nack_on_read);
    RUN_TEST(test_data_nack_stops_the_write);
    RUN_TEST(test_abort_without_a_stop_is_ended_by_abort);
    RUN_TEST(test_stuck_bus_times_out_and_aborts);
    RUN_TEST(test_stuck_bus_on_read_times_out);
    RUN_TEST(test_controller_that_never_goes_idle_times_out);
    RUN_TEST(test_bus_recovers_after_a_timeout);
    RUN_TEST(test_spi_and_onewire_are_unsupported);
    return UNITY_END();
}
