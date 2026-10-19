/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-application runner, linked instead of app_runner.c by every build
 * without APP_DIR (mk/app.mk). No application means no descriptor to check, no
 * configuration to reach and no hook to call, so none of that is linked: not
 * the runner, not the descriptor, not the config span.
 *
 * main.c, tfwsim and tfw_sys_poll call the runner on EVERY build, so its entry
 * points stay real symbols here -- just empty ones. Under -flto each call
 * inlines to nothing, and the image is the one it was before applications
 * existed.
 */
#include "app_runner.h"

void tfw_app_runner_start(void) {}

void tfw_app_runner_poll(void) {}

void tfw_app_runner_stop(uint8_t for_update)
{
    (void)for_update;
}

tfw_app_state_t tfw_app_runner_state(void)
{
    return TFW_APP_STATE_IDLE;
}

uint16_t tfw_app_runner_overruns(void)
{
    return 0u;
}

tfw_status_t tfw_app_runner_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_app_runner_after_update(void)
{
    return TFW_OK;
}
