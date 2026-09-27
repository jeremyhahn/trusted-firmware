/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_SYS_H
#define AG_CAPS_SYS_H

#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

ag_status_t ag_sys_cmd_caps(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_sys_cmd_ping(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_sys_cmd_reset(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_sys_cmd_sys_info(ag_reader_t *r, ag_writer_t *w);

/* True once RESET has been accepted. The main loop polls this and performs the
 * reset only after the UART has drained, so the host actually receives the
 * acknowledgement it is waiting for. */
uint8_t     ag_sys_reset_pending(void);

/* Called from the main loop. Performs a deferred reset once the link is idle. */
void        ag_sys_poll(void);

/* Test seam: clears the pending-reset latch between cases. */
void        ag_sys_reset_state(void);

/* Mirrors the transport's framer counters into SYS_INFO's view of them.
 *
 * dispatch.h forbids a command handler from reaching the framer or the
 * transport directly -- ag_sys_cmd_sys_info runs on the host against a mock
 * HAL and nothing else, and must stay able to. The one place that owns both
 * the transport and every capability is the composition root (main.c,
 * tools/agsim/main.c), so it is the one place that bridges the two: called
 * once per superloop pass, after ag_transport_poll(), with the live counters
 * from t->framer. */
void        ag_sys_set_framer_errors(uint16_t err_crc, uint16_t err_format);

#endif /* AG_CAPS_SYS_H */
