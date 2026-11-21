/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * STM32H5 true random number generator (RM0481 RNG), for the netif's TCP ISN
 * key and its DHCP xid (#584). Reads are bounded spins; a clock or seed error,
 * or no data within the bound, is an error and never a fallback value. Only
 * the RNG and the RCC enable bits (HSI48ON, RNGEN) are written.
 */
#ifndef TFW_H5_HAL_RNG_H
#define TFW_H5_HAL_RNG_H

#include <stdint.h>

#define TFW_H5_RNG_SPIN 100000u  /* polls of a status bit before giving up */

typedef struct {
    volatile uint32_t *regs;         /* RNG_CR, RNG_SR, RNG_DR */
    volatile uint32_t *rcc_cr;       /* RCC_CR: HSI48ON, HSI48RDY */
    volatile uint32_t *rcc_ahb2enr;  /* RCC_AHB2ENR: RNGEN */
} tfw_h5_rng_t;

/* The SoC's RNG. A host test points it at fake registers. */
extern tfw_h5_rng_t tfw_h5_rng;

/* Start HSI48 and the RNG, recovering from an error flag latched by an
 * earlier try. 0, or -1 if HSI48 does not become ready or the RNG does not
 * recover. */
int tfw_h5_rng_init(const tfw_h5_rng_t *r);

/* One 32-bit word. A clock or seed error (now or latched) runs RM0481's
 * recovery (latched flags cleared, a CONDRST pulse for a seed error) and the
 * word comes from the RNG's fresh output after it. 0, or -1 if the error does
 * not clear, comes back, the word is zero (the seed-error output), or no data
 * arrives in time. */
int tfw_h5_rng_read(const tfw_h5_rng_t *r, uint32_t *out);

#endif /* TFW_H5_HAL_RNG_H */
