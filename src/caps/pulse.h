/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_PULSE_H
#define AG_CAPS_PULSE_H
#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
ag_status_t ag_pulse_cmd_in(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_pulse_cmd_capture(ag_reader_t *r, ag_writer_t *w);
#endif
