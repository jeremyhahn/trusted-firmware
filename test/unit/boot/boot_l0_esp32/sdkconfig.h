/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for the generated sdkconfig.h, for test_boot_l0_esp32_mutate.c
 * alone: the P4 build's switches that boot/hal/esp32/boot_l0.c reads. Rollback
 * and anti-rollback on, as mk/arch/esp32/sdkconfig.defaults has them; newlib
 * off, as the host has no _GLOBAL_REENT.
 */
#ifndef TFW_TEST_L0_SDKCONFIG_H
#define TFW_TEST_L0_SDKCONFIG_H

#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
#define CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK   1
#define CONFIG_LIBC_NEWLIB                    0

#endif
