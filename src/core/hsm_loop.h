/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response loop (docs/hsm-self-defence/m7-design.md, slice 3): the
 * superloop's one call into the engine. Each pass ticks the engine, so R5's
 * heartbeat deadline is kept without a message, and raises every signal the
 * port senses (hsm_port.h, tfw_hsm_port_sense) on its rising edge.
 *
 * Linked by file: hsm_loop.c where the engine is (mk/firmware.mk HSM_SRCS),
 * hsm_loop_none.c's no-op everywhere else, so main.c calls it on every image.
 */
#ifndef TFW_CORE_HSM_LOOP_H
#define TFW_CORE_HSM_LOOP_H

/* One pass. Never blocks. */
void tfw_hsm_loop_poll(void);

/* What a reset does to the loop: no line seen asserted. A test calls it
 * between cases, beside tfw_hsm_cmd_reset. */
void tfw_hsm_loop_reset(void);

#endif
