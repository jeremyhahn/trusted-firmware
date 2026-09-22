/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_GPIO_H
#define AG_CAPS_GPIO_H

#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

ag_status_t ag_gpio_cmd_mode(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_gpio_cmd_read(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_gpio_cmd_write(ag_reader_t *r, ag_writer_t *w);

#endif /* AG_CAPS_GPIO_H */
