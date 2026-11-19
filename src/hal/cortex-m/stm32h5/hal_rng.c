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
#define RNG_SR_LATCHED (TFW_H5_RNG_SR_CEIS | TFW_H5_RNG_SR_SEIS)

/*
 * RM0481's RNG error recovery. CEIS and SEIS are rc_w0 and stay set after
 * the error has passed, so a driver that only tests them fails forever.
 * A seed error (SECS or SEIS) resets the conditioning logic with a CONDRST
 * pulse, RNGEN left set, which discards the pipeline; both latched flags are
 * then cleared by writing 0 to them (writing back the status as read keeps
 * every other bit). The RNG is healthy again once CONDRST reads 0 and neither
 * SECS nor CECS is set, within a bounded spin. 0, or -1 if the error stays.
 */
static int rng_recover(const tfw_h5_rng_t *r, uint32_t sr)
{
    if ((sr & (TFW_H5_RNG_SR_SECS | TFW_H5_RNG_SR_SEIS)) != 0u) {
        r->regs[TFW_H5_RNG_CR_IX] |= TFW_H5_RNG_CR_CONDRST;
        r->regs[TFW_H5_RNG_CR_IX] &= ~TFW_H5_RNG_CR_CONDRST;
    }
    r->regs[TFW_H5_RNG_SR_IX] = sr & ~RNG_SR_LATCHED;
    for (uint32_t spin = 0u; spin < TFW_H5_RNG_SPIN; spin++) {
        uint32_t s = r->regs[TFW_H5_RNG_SR_IX];

        if ((r->regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST) == 0u &&
            (s & (TFW_H5_RNG_SR_SECS | TFW_H5_RNG_SR_CECS)) == 0u) {
            if ((s & RNG_SR_LATCHED) != 0u) {
                r->regs[TFW_H5_RNG_SR_IX] = s & ~RNG_SR_LATCHED;
            }
            return 0;
        }
    }
    return -1;
}

int tfw_h5_rng_init(const tfw_h5_rng_t *r)
{
    uint32_t spin;
    uint32_t sr;

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
    sr = r->regs[TFW_H5_RNG_SR_IX];
    if ((sr & RNG_SR_ERRORS) != 0u) {
        return rng_recover(r, sr);          /* a flag left from an earlier try */
    }
    return 0;
}

int tfw_h5_rng_read(const tfw_h5_rng_t *r, uint32_t *out)
{
    int recovered = 0;

    for (uint32_t spin = 0u; spin < TFW_H5_RNG_SPIN; spin++) {
        uint32_t sr = r->regs[TFW_H5_RNG_SR_IX];

        if ((sr & RNG_SR_ERRORS) != 0u) {
            /* One recovery per word: an error that comes straight back is
             * reported, and the caller retries later. */
            if (recovered || rng_recover(r, sr) != 0) {
                return -1;
            }
            recovered = 1;
            continue;
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
