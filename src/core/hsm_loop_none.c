/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* An image without the tamper-response engine: no engine to tick, no line to
 * sense. See hsm_loop.h. */
#include "hsm_loop.h"

void tfw_hsm_loop_poll(void)  { }
void tfw_hsm_loop_reset(void) { }
