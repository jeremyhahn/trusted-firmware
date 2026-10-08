/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_SCHED_H
#define TFW_CAPS_SCHED_H

#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

tfw_status_t tfw_sched_cmd_set(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_sched_cmd_cancel(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_sched_cmd_query(tfw_reader_t *r, tfw_writer_t *w);

void tfw_sched_init(void);

/* Called every main-loop pass. Drives any pin whose deadline has arrived to its
 * final level. */
void tfw_sched_poll(void);

/* How many slots are currently armed. For CAPS and the tests. */
uint8_t tfw_sched_active(void);

#endif /* TFW_CAPS_SCHED_H */
