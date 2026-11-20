/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The update request where the architecture's own OTA file is the update path
 * (ARCH_UPDATE_REQUEST := arch, ESP-IDF's src/hal/esp32/hal_ota.c), linked
 * instead of update_request.c. Its bootloader opens no tfw-boot session, so
 * RESET(1) is answered TFW_ERR_UNSUPPORTED as in update_request_none.c; but no
 * session is not no update path, so tfw_update_slots() is left to that OTA
 * file, which answers the slots it serves. Chosen by file (mk/firmware.mk),
 * not by a conditional in a core file.
 */
#include "update_request.h"

uint8_t tfw_update_request_supported(void)
{
    return 0u;
}

uint8_t tfw_update_request_session(void)
{
    return 0u;
}

void tfw_update_request_arm(void) {}
