/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software NOR part on the application HAL's SPI bus -- see
 * spi_nor_model.h. Linked beside spi_nor_model.c by the suites and the tfwsim
 * build that run src/store/spi_nor.c; the boot suites link the part without it.
 */
#include <string.h>
#include "tfw/board.h"
#include "tfw/hal.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "spi_nor_hal.h"

static uint32_t     xfers, fail_at;
static tfw_status_t fail_st;

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                              uint8_t *rd, uint8_t len)
{
    tfw_pinref_t me;

    if (tfw_board_pin(TFW_OTA_STORE_CS_PIN, &me) != TFW_OK || cs == NULL ||
        cs->port != me.port || cs->bit != me.bit) {
        return tfw_mock_spi_xfer(cs, wr, rd, len);
    }
    xfers++;
    if (fail_at != 0u && xfers == fail_at) {
        return fail_st;         /* the part never sees it */
    }
    memmove(rd, wr, len);
    tfw_nor_model_xfer(rd, len);
    if (tfw_nor_model_bus() == TFW_NOR_MODEL_ABSENT_HIGH) {
        tfw_mock_advance(1u);   /* WIP never clears: the mock clock runs out the wait */
    }
    return TFW_OK;
}

void tfw_nor_shim_reset(void)
{
    xfers = fail_at = 0u;
    fail_st = TFW_OK;
}

void tfw_nor_shim_fail(uint32_t nth, tfw_status_t st)
{
    xfers   = 0u;
    fail_at = nth;
    fail_st = st;
}

uint32_t tfw_nor_shim_xfers(void) { return xfers; }
