/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A RESET restarts the firmware in place (tools/tfwsim/main.c), keeping the
 * mock EEPROM and the element model as a device keeps its EEPROM and its
 * element across a reset.
 *
 * In tfwsim-prov's object list only (mk/host.mk), selected by the list and not
 * by a define, like the feature it serves. The element provider decides at
 * startup whether the part holds a UDS and refuses to sign until it does, so
 * the jig image's evidence names the key `tfwprov --commit` generated only
 * after the boot that follows the commit. Every other tfwsim binary keeps
 * main.c's weak default, a RESET that is counted and nothing more.
 */

#include "tier.h"

int tfwsim_reboot_on_reset(void) { return 1; }
