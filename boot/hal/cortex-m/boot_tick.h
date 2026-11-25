/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's tick on Cortex-M: one pass of boot/recovery/recovery_ota.c's
 * receive loop with nothing received, as on AVR (boot/hal/avr/boot_tick.h)
 * and for its reason -- tfw-boot starts no timer, so SysTick is in its reset
 * state when the application's tfw_hal_tick_init takes it.
 *
 * NOT MEASURED. Of the supported Cortex-M rows (mps2-an505, mps2-an500,
 * stm32h573i-dk, rp2350-pico2w, nrf54l15-dk and frdm-mcxn947, mk/boot.mk),
 * mps2-an505:soft:soft:p256-soft has BOOT_OTA on, yet no test pins the
 * figure here as the simavr suite does on AVR. A row with BOOT_OTA on
 * measures the pass (under QEMU, or on the board) and replaces the figure.
 */
#ifndef TFW_BOOT_TICK_H
#define TFW_BOOT_TICK_H

#include <stdint.h>

#include "tfw/config.h"

#define TFW_BOOT_POLL_CYCLES 20uL
#define TFW_BOOT_TICK_HZ     ((uint32_t)(TFW_CM_CPU_HZ / TFW_BOOT_POLL_CYCLES))

/* serve's pass counter: plain, the loop as main compiles it. Nothing has been
 * measured against it yet; the row that measures the pass settles its type
 * with its figure (#640). */
#define TFW_BOOT_POLL_COUNTER_T uint32_t

#endif /* TFW_BOOT_TICK_H */
