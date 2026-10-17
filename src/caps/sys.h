/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_SYS_H
#define TFW_CAPS_SYS_H

#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

tfw_status_t tfw_sys_cmd_caps(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_sys_cmd_ping(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_sys_cmd_reset(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_sys_cmd_sys_info(tfw_reader_t *r, tfw_writer_t *w);

/* True once RESET has been accepted. The main loop polls this and performs the
 * reset only after the UART has drained, so the host actually receives the
 * acknowledgement it is waiting for. */
uint8_t     tfw_sys_reset_pending(void);
/* RESET mode 0 after the reply drains: OTA_ACTIVATE mode 1 (issue #77). */
void        tfw_sys_reset_soon(void);

/* Called from the main loop. Performs a deferred reset once the link is idle. */
void        tfw_sys_poll(void);

/* Test seam: clears the pending-reset latch between cases. */
void        tfw_sys_reset_state(void);

/* Mirrors the transport's framer counters into SYS_INFO's view of them.
 *
 * dispatch.h forbids a command handler from reaching the framer or the
 * transport directly -- tfw_sys_cmd_sys_info runs on the host against a mock
 * HAL and nothing else, and must stay able to. The one place that owns both
 * the transport and every capability is the composition root (main.c,
 * tools/tfwsim/main.c), so it is the one place that bridges the two: called
 * once per superloop pass, after tfw_transport_poll(), with the live counters
 * from t->framer. */
void        tfw_sys_set_framer_errors(uint16_t err_crc, uint16_t err_format);

#endif /* TFW_CAPS_SYS_H */
