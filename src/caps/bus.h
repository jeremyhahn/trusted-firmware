/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_BUS_H
#define TFW_CAPS_BUS_H
#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

/* Applies the boot defaults to whichever of I2C/SPI this build has -- called
 * once from main.c. Exactly one of this and bus_none.c's stub is linked, so
 * main.c can call it unconditionally regardless of TFW_FEATURE_I2C/SPI/ONEWIRE:
 * see the BUS_SRCS selection in mk/avr.mk, the same shape CRYPTO_SRCS uses for
 * the crypto tier. */
void        tfw_bus_init(void);

tfw_status_t tfw_i2c_cmd_xfer(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_i2c_cmd_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_spi_cmd_xfer(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_spi_cmd_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_ow_cmd_reset(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_ow_cmd_write(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_ow_cmd_read(tfw_reader_t *r, tfw_writer_t *w);
#endif
