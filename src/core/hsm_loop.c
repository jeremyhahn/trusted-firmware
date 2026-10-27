/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response loop: see hsm_loop.h. The engine is the one the
 * channel's commands drive (hsm_cmd.c), so a signal sensed here and a report
 * off the channel decide against the same P, the same verdict and the same
 * latch.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "hsm.h"
#include "hsm_cmd.h"
#include "hsm_loop.h"
#include "hsm_port.h"

/* The lines asserted on the last pass: a signal is raised when its line goes
 * from clear to asserted, so a switch held down is one event, not one per
 * pass. */
static uint8_t g_level;

void tfw_hsm_loop_reset(void)
{
    g_level = 0u;
}

void tfw_hsm_loop_poll(void)
{
    uint32_t   now   = tfw_hal_millis();
    tfw_hsm_t *h     = tfw_hsm_cmd_engine(now);
    uint8_t    level = tfw_hsm_port_sense();
    uint8_t    rose  = (uint8_t)(level & (uint8_t)~g_level);
    uint8_t    sig;

    g_level = level;
    for (sig = TFW_HSM_SIG_TAMPER_PIN; sig <= TFW_HSM_SIG_HEARTBEAT_LOSS; sig++) {
        if ((rose & (uint8_t)(1u << (sig - 1u))) != 0u) {
            tfw_hsm_signal(h, sig, now);
        }
    }
}
