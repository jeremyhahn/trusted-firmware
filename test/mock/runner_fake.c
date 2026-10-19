/* SPDX-License-Identifier: commercial */
#include "app_runner.h"
#include "runner_fake.h"

static tfw_status_t answer;
static unsigned     calls;

void tfw_fake_runner_reset(void)
{
    answer = TFW_OK;
    calls  = 0u;
}

void     tfw_fake_runner_set_after_update(tfw_status_t st) { answer = st; }
unsigned tfw_fake_runner_after_update_calls(void)          { return calls; }

tfw_status_t tfw_app_runner_after_update(void)
{
    calls++;
    return answer;
}
