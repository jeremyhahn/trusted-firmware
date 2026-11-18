/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * STM32H5 RNG (RM0481): HSI48 on, RNGEN in RCC_AHB2ENR, RNGEN in RNG_CR, then
 * each word is read once RNG_SR DRDY is set and no error bit is. Nothing here
 * touches option bytes, OTP, RDP or flash.
 */
#include <stddef.h>
#include <stdint.h>

#include "hal_rng.h"
#include "soc_regs.h"

tfw_h5_rng_t tfw_h5_rng = {
    (volatile uint32_t *)(uintptr_t)TFW_H5_RNG_BASE,
    &TFW_H5_RCC_CR,
    &TFW_H5_RCC_AHB2ENR,
};

#define RNG_SR_ERRORS (TFW_H5_RNG_SR_CECS | TFW_H5_RNG_SR_SECS | \
                       TFW_H5_RNG_SR_CEIS | TFW_H5_RNG_SR_SEIS)

int tfw_h5_rng_init(const tfw_h5_rng_t *r)
{
    uint32_t spin;

    *r->rcc_cr |= TFW_H5_RCC_CR_HSI48ON;
    for (spin = 0u; spin < TFW_H5_RNG_SPIN; spin++) {
        if ((*r->rcc_cr & TFW_H5_RCC_CR_HSI48RDY) != 0u) {
            break;
        }
    }
    if (spin == TFW_H5_RNG_SPIN) {
        return -1;
    }
    *r->rcc_ahb2enr |= TFW_H5_RCC_AHB2ENR_RNGEN;
    (void)*r->rcc_ahb2enr;                  /* the enable lands first */
    r->regs[TFW_H5_RNG_CR_IX] |= TFW_H5_RNG_CR_RNGEN;
    return 0;
}

int tfw_h5_rng_read(const tfw_h5_rng_t *r, uint32_t *out)
{
    for (uint32_t spin = 0u; spin < TFW_H5_RNG_SPIN; spin++) {
        uint32_t sr = r->regs[TFW_H5_RNG_SR_IX];

        if ((sr & RNG_SR_ERRORS) != 0u) {
            return -1;
        }
        if ((sr & TFW_H5_RNG_SR_DRDY) != 0u) {
            uint32_t v = r->regs[TFW_H5_RNG_DR_IX];

            if (v == 0u) {
                return -1;
            }
            *out = v;
            return 0;
        }
    }
    return -1;
}
