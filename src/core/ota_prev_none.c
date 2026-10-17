/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stub linked in place of ota_prev.c on a board whose fallback is a second
 * internal slot (the 2560's swap-move): there is no PREV to back up to.
 */
#include "ota_stage.h"

tfw_status_t tfw_ota_prev_backup(void)
{
    return TFW_OK;
}
