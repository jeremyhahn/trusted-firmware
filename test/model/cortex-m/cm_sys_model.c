/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* See cm_sys_model.h. */
#include "cm_sys_model.h"
#include "hal_early.h"

jmp_buf           cm_sys_model_reset;
tfw_reset_cause_t cm_sys_model_latched;

_Noreturn void tfw_cm_system_reset(void)
{
    cm_sys_model_call('R');
    longjmp(cm_sys_model_reset, 1);
}

void tfw_cm_soc_watchdog_arm(void) { cm_sys_model_call('A'); }
void tfw_cm_soc_watchdog_pet(void) { cm_sys_model_call('p'); }

tfw_reset_cause_t tfw_hal_early_reset_cause(void)
{
    cm_sys_model_call('l');
    return cm_sys_model_latched;
}
