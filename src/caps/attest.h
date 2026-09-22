/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_ATTEST_H
#define AG_CAPS_ATTEST_H
#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
ag_status_t ag_attest_cmd(ag_reader_t *r, ag_writer_t *w);
void        ag_attest_reset_state(void);   /* test seam */
#endif
