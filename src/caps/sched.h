/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_SCHED_H
#define AG_CAPS_SCHED_H

#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

ag_status_t ag_sched_cmd_set(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_sched_cmd_cancel(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_sched_cmd_query(ag_reader_t *r, ag_writer_t *w);

void ag_sched_init(void);

/* Called every main-loop pass. Drives any pin whose deadline has arrived to its
 * final level. */
void ag_sched_poll(void);

/* How many slots are currently armed. For CAPS and the tests. */
uint8_t ag_sched_active(void);

#endif /* AG_CAPS_SCHED_H */
