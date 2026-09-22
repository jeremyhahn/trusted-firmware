/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_ANALOG_H
#define AG_CAPS_ANALOG_H
#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
ag_status_t ag_analog_cmd_read(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_analog_cmd_set_ref(ag_reader_t *r, ag_writer_t *w);
#endif
