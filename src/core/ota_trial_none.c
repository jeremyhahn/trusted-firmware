/* SPDX-License-Identifier: commercial */
/*
 * A board with no boot section has no trial (mk/firmware.mk links this in
 * ota_trial.c's place): nothing to read, nothing to arm.
 */

#include "ota_trial.h"

void tfw_ota_trial_start(void) { }
void tfw_ota_trial_poll(void)  { }
