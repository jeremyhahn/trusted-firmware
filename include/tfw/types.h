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

/* A pin resolved to hardware: the address of its PINx register and the bit
 * within it. On every classic AVR, DDRx is PINx+1 and PORTx is PINx+2, so this
 * pair locates all three.
 *
 * The HAL takes THIS rather than an tfw_pin_t. That is the whole reason the HAL
 * is identical code on every part in the family: it does no lookup, consults no
 * table and knows nothing about which board it is on. Translation happens one
 * layer up, in boards/<board>/board.c, which is plain C with no register access
 * and therefore compiles and is unit-tested on the host. */
typedef struct {
    uint16_t port;  /* address of PINx, as a memory-mapped SRAM address */
    uint8_t  bit;
} tfw_pinref_t;

typedef enum {
    TFW_PIN_INPUT        = 0,
    TFW_PIN_OUTPUT       = 1,
    TFW_PIN_INPUT_PULLUP = 2
} tfw_pin_mode_t;

/* Why the part last reset. Latched from MCUSR in the first instructions after
 * boot and reported by SYS_INFO, so a host can tell a commanded reboot from a
 * brownout or a watchdog bite -- which is the difference between "expected"
 * and "this device is failing". */
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
