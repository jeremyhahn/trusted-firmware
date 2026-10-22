/* SPDX-License-Identifier: commercial */
#include "app_runner.h"
#include "runner_fake.h"

static tfw_status_t    answer;
static unsigned        calls;
static tfw_app_state_t app;

void tfw_fake_runner_reset(void)
{
    answer = TFW_OK;
    calls  = 0u;
    app    = TFW_APP_STATE_RUNNING;
}

void     tfw_fake_runner_set_state(tfw_app_state_t st)     { app = st; }
void     tfw_fake_runner_set_after_update(tfw_status_t st) { answer = st; }
unsigned tfw_fake_runner_after_update_calls(void)          { return calls; }

tfw_app_state_t tfw_app_runner_state(void)
{
    return app;
}

tfw_status_t tfw_app_runner_after_update(void)
{
    calls++;
    return answer;
}
