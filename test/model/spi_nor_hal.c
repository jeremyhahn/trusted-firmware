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

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                              uint8_t *rd, uint8_t len)
{
    tfw_pinref_t me;

    if (tfw_board_pin(TFW_OTA_STORE_CS_PIN, &me) != TFW_OK || cs == NULL ||
        cs->port != me.port || cs->bit != me.bit) {
        return tfw_mock_spi_xfer(cs, wr, rd, len);
    }
    memmove(rd, wr, len);
    tfw_nor_model_xfer(rd, len);
    return TFW_OK;
}
