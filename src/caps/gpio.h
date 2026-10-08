/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_GPIO_H
#define TFW_CAPS_GPIO_H

#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

tfw_status_t tfw_gpio_cmd_mode(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_gpio_cmd_read(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_gpio_cmd_write(tfw_reader_t *r, tfw_writer_t *w);

#endif /* TFW_CAPS_GPIO_H */
