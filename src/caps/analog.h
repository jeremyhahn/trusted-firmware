/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_ANALOG_H
#define TFW_CAPS_ANALOG_H
#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
tfw_status_t tfw_analog_cmd_read(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_analog_cmd_set_ref(tfw_reader_t *r, tfw_writer_t *w);
#endif
