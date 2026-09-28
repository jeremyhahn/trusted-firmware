/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_BUS_H
#define AG_CAPS_BUS_H
#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

/* Applies the boot defaults to whichever of I2C/SPI this build has -- called
 * once from main.c. Exactly one of this and bus_none.c's stub is linked, so
 * main.c can call it unconditionally regardless of AG_FEATURE_I2C/SPI/ONEWIRE:
 * see the BUS_SRCS selection in mk/avr.mk, the same shape CRYPTO_SRCS uses for
 * the crypto tier. */
void        ag_bus_init(void);

ag_status_t ag_i2c_cmd_xfer(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_i2c_cmd_config(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_spi_cmd_xfer(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_spi_cmd_config(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_ow_cmd_reset(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_ow_cmd_write(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_ow_cmd_read(ag_reader_t *r, ag_writer_t *w);
#endif
