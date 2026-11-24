/*
 * boot/hal/host/boot_tick.h -- the host boot HAL's tick rate (boot/boot_hal.h).
 *
 * tfwsim runs the boot pass to completion in one call: the UART never has a
 * byte and no session is opened (boot/hal/host/boot_hal_host.c), so nothing
 * counts these ticks. The rate is the 328P's at 16 MHz so the session limits
 * boot_main.c computes from it are the ones an image on that part would get.
 */
#ifndef TFW_BOOT_TICK_H
#define TFW_BOOT_TICK_H

#include <stdint.h>

#define TFW_BOOT_POLL_CYCLES 77uL
#define TFW_BOOT_TICK_HZ     ((uint32_t)16000000uL / TFW_BOOT_POLL_CYCLES)

/* serve's pass counter, as on the 328P whose figures this mirrors
 * (boot/hal/avr/boot_tick.h, #640). */
#define TFW_BOOT_POLL_COUNTER_T volatile uint32_t

#endif /* TFW_BOOT_TICK_H */
