/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_TYPES_H
#define AG_TYPES_H

#include <stdint.h>

/* Milliseconds since boot. Wraps every 49.7 days; everything that compares
 * two of these uses signed-difference arithmetic so the wrap is a non-event. */
typedef uint32_t ag_ms_t;

/* A logical pin, as printed on the board silkscreen. The wire speaks these;
 * the board descriptor translates them to a port and bit. */
typedef uint8_t ag_pin_t;

/* A pin resolved to hardware: the address of its PINx register and the bit
 * within it. On every classic AVR, DDRx is PINx+1 and PORTx is PINx+2, so this
 * pair locates all three.
 *
 * The HAL takes THIS rather than an ag_pin_t. That is the whole reason the HAL
 * is identical code on every part in the family: it does no lookup, consults no
 * table and knows nothing about which board it is on. Translation happens one
 * layer up, in boards/<board>/board.c, which is plain C with no register access
 * and therefore compiles and is unit-tested on the host. */
typedef struct {
    uint16_t port;  /* address of PINx, as a memory-mapped SRAM address */
    uint8_t  bit;
} ag_pinref_t;

typedef enum {
    AG_PIN_INPUT        = 0,
    AG_PIN_OUTPUT       = 1,
    AG_PIN_INPUT_PULLUP = 2
} ag_pin_mode_t;

/* Why the part last reset. Latched from MCUSR in the first instructions after
 * boot and reported by SYS_INFO, so a host can tell a commanded reboot from a
 * brownout or a watchdog bite -- which is the difference between "expected"
 * and "this device is failing". */
typedef enum {
    AG_RESET_UNKNOWN  = 0,
    AG_RESET_POWER_ON = 1,
    AG_RESET_EXTERNAL = 2,
    AG_RESET_BROWNOUT = 3,
    AG_RESET_WATCHDOG = 4
} ag_reset_cause_t;

/* How strong the device's root of trust is. Reported in CAPS so the platform
 * knows what it is talking to BEFORE it trusts anything it says. Mirrors the
 * vocabulary go-xkms uses for the same question. */
typedef enum {
    AG_CRYPTO_NONE           = 0, /* no provider: unattested */
    AG_CRYPTO_SOFTWARE       = 1, /* keys in flash behind lock bits; notSecure */
    AG_CRYPTO_SECURE_ELEMENT = 2, /* ATECC608A: non-exportable key, real TRNG */
    AG_CRYPTO_TPM2           = 3  /* discrete TPM: manufacturer EK chain */
} ag_crypto_tier_t;

#endif /* AG_TYPES_H */
