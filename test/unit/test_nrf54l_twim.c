/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L I2C master (src/hal/cortex-m/nrf54l/hal_bus.c) against a model of
 * one TWIM: the driver is the image's, unchanged; its register accesses reach
 * the model through test/model/cortex-m/nrf54l/cm_regs.h.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"

/* The TWIM registers the driver touches, as the model holds them. */
typedef struct {
    uint32_t tasks_starttx;
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
} twim_model_t;

static twim_model_t s_twim;
static uint32_t     s_cell;

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr)
{
    (void)addr;
    s_cell = 0u;
    return &s_cell;
}

void setUp(void)
{
    memset(&s_twim, 0, sizeof s_twim);
}

void tearDown(void) { }

static void test_twim_write_acked(void)
{
    static const uint8_t wr[2] = { 0x01u, 0x02u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100000uL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x60u, wr, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x60u, s_twim.address);
    TEST_ASSERT_EQUAL_UINT32(2u, s_twim.txd_maxcnt);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_twim_write_acked);
    return UNITY_END();
}
