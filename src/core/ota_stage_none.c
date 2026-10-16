/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stub linked in place of ota_stage.c on an image without the OTA stager.
 *
 * main.c calls tfw_stage_init() unconditionally, with no #if: the gate is which
 * of this file and ota_stage.c mk/firmware.mk's STAGE_SRCS selects, as with
 * bus_none.c. An image with no store has no select to park.
 */

#include "ota_stage.h"

void tfw_stage_init(void)
{
}
