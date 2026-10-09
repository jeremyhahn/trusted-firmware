/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_TYPES_H
#define TFW_TYPES_H

#include <stdint.h>

/* Milliseconds since boot. Wraps every 49.7 days; everything that compares
 * two of these uses signed-difference arithmetic so the wrap is a non-event. */
typedef uint32_t tfw_ms_t;

/* A logical pin, as printed on the board silkscreen. The wire speaks these;
 * the board descriptor translates them to a port and bit. */
typedef uint8_t tfw_pin_t;

/* tfw_pinref_t -- a pin resolved to hardware -- is defined by the architecture,
 * in src/hal/<arch>/hal_types.h, and reaches code through include/tfw/hal.h.
 * Core sees it as opaque: it declares one, has tfw_board_pin() fill it and
 * hands it to the HAL, and never names a field. */

typedef enum {
    TFW_PIN_INPUT        = 0,
    TFW_PIN_OUTPUT       = 1,
    TFW_PIN_INPUT_PULLUP = 2
} tfw_pin_mode_t;

/* Why the part last reset. Latched from the part's reset-status register in the
 * first instructions after boot, decoded by the HAL (the only code that knows
 * the register), and reported by SYS_INFO and carried in tfw-boot's handoff, so
 * a host can tell a commanded reboot from a brownout or a watchdog bite -- which
 * is the difference between "expected" and "this device is failing".
 *
 * TFW_RESET_WATCHDOG means a watchdog reset and nothing else: it is what the
 * update-request gate honours, so a HAL must not report it for a reset that
 * something other than the watchdog also caused. These values are the wire;
 * they extend, they never renumber. */
typedef enum {
    TFW_RESET_UNKNOWN  = 0,
    TFW_RESET_POWER_ON = 1,
    TFW_RESET_EXTERNAL = 2,
    TFW_RESET_BROWNOUT = 3,
    TFW_RESET_WATCHDOG = 4
} tfw_reset_cause_t;

/* How strong the device's root of trust is. Reported in CAPS so the platform
 * knows what it is talking to BEFORE it trusts anything it says. Mirrors the
 * vocabulary go-xkms uses for the same question. */
typedef enum {
    TFW_CRYPTO_NONE           = 0, /* no provider: unattested */
    TFW_CRYPTO_SOFTWARE       = 1, /* keys in flash behind lock bits; notSecure */
    TFW_CRYPTO_SECURE_ELEMENT = 2, /* ATECC608A: non-exportable key, real TRNG */
    TFW_CRYPTO_TPM2           = 3  /* discrete TPM: manufacturer EK chain */
} tfw_crypto_tier_t;

#endif /* TFW_TYPES_H */
