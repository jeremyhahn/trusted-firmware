/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's tick on AVR: one pass of boot/recovery/recovery_ota.c's
 * receive loop with nothing received.
 *
 * tfw-boot has no timer -- it leaves every peripheral in its reset state for
 * the application, and a timer would be one more thing to put back -- so the
 * loop itself is the clock. TFW_BOOT_POLL_CYCLES is what one empty pass costs
 * in CPU cycles as avr-gcc compiles it, and the simavr suite
 * (test/integration/simavr/boot_harness.c) measures the session windows this
 * produces against TFW_BOOT_SESSION_IDLE_SECONDS, so a change to the loop that
 * makes the number wrong fails CI rather than silently stretching or shrinking
 * the window.
 *
 * A compile-time constant rather than a HAL function, because the session
 * limits are computed from it once, in boot/boot_main.c, and a call would buy
 * nothing but the call. Found on the include path mk/boot.mk gives
 * $(BOOT_HAL_DIR), as the rest of this directory is selected at link time.
 */
#ifndef TFW_BOOT_TICK_H
#define TFW_BOOT_TICK_H

#include <stdint.h>

#define TFW_BOOT_POLL_CYCLES 77uL
#define TFW_BOOT_TICK_HZ     ((uint32_t)F_CPU / TFW_BOOT_POLL_CYCLES)

#endif /* TFW_BOOT_TICK_H */
