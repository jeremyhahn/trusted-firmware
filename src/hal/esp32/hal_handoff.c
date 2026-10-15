/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the CDI handoff block, on ESP-IDF.
 *
 * The AVR pins a fixed SRAM address in both images by linker arithmetic. ESP-IDF
 * already has the shared fixed address this needs: the custom area of the
 * bootloader's RTC-retained memory (CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC),
 * which the bootloader and the application both locate at the same spot and
 * which survives every reset but a power loss. The ESP-IDF build sets its size
 * to TFW_BOOT_HANDOFF_LEN and keeps it out of the retained block's CRC, so
 * writing it never makes ESP-IDF's own record look corrupt.
 *
 * boot/hal/esp32's layer 0 writes it from the bootloader after secure boot has
 * verified the application; src/core/handoff.c reads it, checks magic and CRC
 * and clears it, exactly as on the AVR.
 */

#include "bootloader_common.h"
#include "sdkconfig.h"

#include "tfw/hal.h"
#include "tfw/boot.h"

_Static_assert(CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE == TFW_BOOT_HANDOFF_LEN,
               "the RTC custom reserve must be exactly the handoff block");

uint8_t *tfw_hal_handoff_block(void)
{
    return bootloader_common_get_rtc_retain_mem()->custom;
}
