/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's soc/reset_reasons.h. The C3's names and the
 * P4's in one enum, each a distinct value, so either build of
 * boot_hal_esp32.c compiles against it; ESP-IDF's values differ per target
 * and the unit names them only by name.
 */
#ifndef TFW_STANDIN_SOC_RESET_REASONS_H
#define TFW_STANDIN_SOC_RESET_REASONS_H

typedef enum {
    RESET_REASON_CHIP_POWER_ON = 0x01,
    RESET_REASON_CORE_SW       = 0x03,
    RESET_REASON_CORE_DEEP_SLEEP = 0x05,
    RESET_REASON_CORE_MWDT0    = 0x07,
    RESET_REASON_CORE_RTC_WDT  = 0x09,
    RESET_REASON_CPU0_MWDT0    = 0x0B,
    RESET_REASON_CPU0_SW       = 0x0C,
    RESET_REASON_CPU0_RTC_WDT  = 0x0D,
    RESET_REASON_SYS_BROWN_OUT = 0x0F,
    RESET_REASON_SYS_RTC_WDT   = 0x10,
    RESET_REASON_CPU_SW        = 0x20,
    RESET_REASON_CORE_MWDT     = 0x21,
    RESET_REASON_CORE_RWDT     = 0x22,
    RESET_REASON_CPU_MWDT      = 0x23,
    RESET_REASON_CPU_RWDT      = 0x24,
    RESET_REASON_SYS_RWDT      = 0x25
} soc_reset_reason_t;

#endif
