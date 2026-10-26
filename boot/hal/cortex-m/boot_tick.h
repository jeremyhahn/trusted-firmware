/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's tick on Cortex-M: one pass of boot/recovery/recovery_ota.c's
 * receive loop with nothing received, as on AVR (boot/hal/avr/boot_tick.h)
 * and for its reason -- tfw-boot starts no timer, so SysTick is in its reset
 * state when the application's tfw_hal_tick_init takes it.
 *
 * NOT MEASURED. The supported rows (mps2-an505 and rp2350-pico2w, mk/boot.mk)
 * shed OTA, so no session is ever opened and nothing counts these ticks yet.
 * A row that turns BOOT_OTA on measures the pass first (under QEMU, or on the
 * board), as the simavr suite does on AVR, and replaces the figure here.
 */
#ifndef TFW_BOOT_TICK_H
#define TFW_BOOT_TICK_H

#include <stdint.h>

#include "tfw/config.h"

#define TFW_BOOT_POLL_CYCLES 20uL
#define TFW_BOOT_TICK_HZ     ((uint32_t)(TFW_CM_CPU_HZ / TFW_BOOT_POLL_CYCLES))

#endif /* TFW_BOOT_TICK_H */
