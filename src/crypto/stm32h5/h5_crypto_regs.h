/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's HASH and PKA registers, as offsets from each peripheral's
 * base, and the field values the drivers write (#544). Every value is from
 * ST's sources at the commits docs/plans/544.md pins:
 *
 *   CMSIS  STMicroelectronics/cmsis-device-h5 @ 884b8dc7, Include/stm32h573xx.h
 *   HAL    STMicroelectronics/stm32h5xx-hal-driver @ 16fea2d2,
 *          Inc/stm32h5xx_hal_hash.h, Inc/stm32h5xx_hal_pka.h
 *
 * The bases are the board's, not this file's: a driver takes its base as an
 * argument, so the host tests drive the same code against a model.
 *
 * Register access goes through tfw_h5_rd and tfw_h5_wr. A host test defines
 * TFW_H5_REG_MODEL and supplies both, so its model sees every access in
 * order, as the peripheral would.
 */
#ifndef TFW_CRYPTO_H5_CRYPTO_REGS_H
#define TFW_CRYPTO_H5_CRYPTO_REGS_H

#include <stdint.h>

#ifdef TFW_H5_REG_MODEL
uint32_t tfw_h5_rd(uintptr_t addr);
void     tfw_h5_wr(uintptr_t addr, uint32_t value);
#else
static inline uint32_t tfw_h5_rd(uintptr_t addr)
{
    return *(volatile const uint32_t *)addr;
}

static inline void tfw_h5_wr(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}
#endif

/* HASH: CMSIS HASH_TypeDef; the digest registers are HASH_DIGEST_TypeDef's
 * HR[], at HASH_DIGEST_BASE_NS, 0x310 past HASH_BASE_NS. */
#define TFW_H5_HASH_CR      0x000u
#define TFW_H5_HASH_DIN     0x004u
#define TFW_H5_HASH_STR     0x008u
#define TFW_H5_HASH_SR      0x024u
#define TFW_H5_HASH_HR      0x310u

/* CMSIS HASH_CR_INIT; HAL HASH_BYTE_SWAP (HASH_CR_DATATYPE_1: 8-bit data,
 * so a little-endian word load of four message bytes is fed in order);
 * HAL HASH_ALGOSELECTION_SHA256 (HASH_CR_ALGO_0 | HASH_CR_ALGO_1). MODE,
 * bit 6, stays 0: a hash, not an HMAC. */
#define TFW_H5_HASH_CR_INIT         (1u << 2)
#define TFW_H5_HASH_CR_BYTE_SWAP    (2u << 4)
#define TFW_H5_HASH_CR_ALGO_SHA256  (3u << 17)
#define TFW_H5_HASH_CR_MODE         (1u << 6)

/* CMSIS HASH_STR_NBLW (bits 4:0, the valid bits of the last word written)
 * and HASH_STR_DCAL; HASH_SR_DINIS, HASH_SR_DCIS and HASH_SR_BUSY. */
#define TFW_H5_HASH_STR_NBLW_MASK   0x1Fu
#define TFW_H5_HASH_STR_DCAL        (1u << 8)
#define TFW_H5_HASH_SR_DINIS        (1u << 0)
#define TFW_H5_HASH_SR_DCIS         (1u << 1)
#define TFW_H5_HASH_SR_BUSY         (1u << 3)

/* The input FIFO's depth in words, a block of SHA-256: the driver waits for
 * BUSY to clear after each one rather than lean on the bus stalling a write
 * to a full FIFO, which RM0481 would have to confirm. */
#define TFW_H5_HASH_BLOCK_WORDS     16u

/* How many status reads a driver makes before it gives up. At the board's
 * 64 MHz this is well past a block's digest; the figure is a bound, not a
 * timing, and is not confirmed against RM0481. */
#define TFW_H5_POLL_LIMIT           200000u

/* PKA: CMSIS PKA_TypeDef, CR, SR and CLRFR, then the RAM at 0x400. */
#define TFW_H5_PKA_CR       0x000u
#define TFW_H5_PKA_SR       0x004u
#define TFW_H5_PKA_CLRFR    0x008u
#define TFW_H5_PKA_RAM      0x400u
#define TFW_H5_PKA_RAM_END  0x18D8u   /* RAM[1334] */

/* CMSIS PKA_CR_EN, PKA_CR_START, PKA_CR_MODE_Pos; PKA_SR_INITOK, PKA_SR_BUSY,
 * PKA_SR_PROCENDF; PKA_CLRFR_PROCENDFC, _RAMERRFC, _ADDRERRFC, _OPERRFC.
 * The SR error flags are taken at their CLRFR bits, 19 to 21, which the
 * CMSIS names pair but RM0481 would have to confirm. */
#define TFW_H5_PKA_CR_EN            (1u << 0)
#define TFW_H5_PKA_CR_START         (1u << 1)
#define TFW_H5_PKA_CR_MODE_POS      8u
#define TFW_H5_PKA_SR_INITOK        (1u << 0)
#define TFW_H5_PKA_SR_BUSY          (1u << 16)
#define TFW_H5_PKA_SR_PROCENDF      (1u << 17)
#define TFW_H5_PKA_SR_ERRORS        ((1u << 19) | (1u << 20) | (1u << 21))
#define TFW_H5_PKA_CLRFR_ALL        ((1u << 17) | (1u << 19) | (1u << 20) | (1u << 21))

/* HAL PKA_MODE_ECDSA_SIGNATURE, PKA_MODE_ECDSA_VERIFICATION, PKA_NO_ERROR
 * (the sign's error word and the verify's result when the signature holds). */
#define TFW_H5_PKA_MODE_ECDSA_SIGN    0x24u
#define TFW_H5_PKA_MODE_ECDSA_VERIFY  0x26u
#define TFW_H5_PKA_NO_ERROR           0xD60Du

/* The operands' byte offsets from the PKA's base: CMSIS PKA_ECDSA_SIGN_* and
 * PKA_ECDSA_VERIF_*, each defined as ((offset - PKA_RAM_OFFSET) >> 2), an
 * index into RAM[], which starts at PKA_RAM_OFFSET; so the offset is the
 * address's distance from the base. */
#define TFW_H5_PKA_SIGN_IN_ORDER_NB_BITS    0x0400u
#define TFW_H5_PKA_SIGN_IN_MOD_NB_BITS      0x0408u
#define TFW_H5_PKA_SIGN_IN_A_COEFF_SIGN     0x0410u
#define TFW_H5_PKA_SIGN_IN_A_COEFF          0x0418u
#define TFW_H5_PKA_SIGN_IN_B_COEFF          0x0520u
#define TFW_H5_PKA_SIGN_IN_MOD_GF           0x1088u
#define TFW_H5_PKA_SIGN_IN_K                0x12A0u
#define TFW_H5_PKA_SIGN_IN_INITIAL_POINT_X  0x0578u
#define TFW_H5_PKA_SIGN_IN_INITIAL_POINT_Y  0x0470u
#define TFW_H5_PKA_SIGN_IN_HASH_E           0x0FE8u
#define TFW_H5_PKA_SIGN_IN_PRIVATE_KEY_D    0x0F28u
#define TFW_H5_PKA_SIGN_IN_ORDER_N          0x0F88u
#define TFW_H5_PKA_SIGN_OUT_ERROR           0x0FE0u
#define TFW_H5_PKA_SIGN_OUT_SIGNATURE_R     0x0730u
#define TFW_H5_PKA_SIGN_OUT_SIGNATURE_S     0x0788u

#define TFW_H5_PKA_VERIF_IN_ORDER_NB_BITS   0x0408u
#define TFW_H5_PKA_VERIF_IN_MOD_NB_BITS     0x04C8u
#define TFW_H5_PKA_VERIF_IN_A_COEFF_SIGN    0x0468u
#define TFW_H5_PKA_VERIF_IN_A_COEFF         0x0470u
#define TFW_H5_PKA_VERIF_IN_MOD_GF          0x04D0u
#define TFW_H5_PKA_VERIF_IN_INITIAL_POINT_X 0x0678u
#define TFW_H5_PKA_VERIF_IN_INITIAL_POINT_Y 0x06D0u
#define TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_X    0x12F8u
#define TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_Y    0x1350u
#define TFW_H5_PKA_VERIF_IN_SIGNATURE_R     0x10E0u
#define TFW_H5_PKA_VERIF_IN_SIGNATURE_S     0x0C68u
#define TFW_H5_PKA_VERIF_IN_HASH_E          0x13A8u
#define TFW_H5_PKA_VERIF_IN_ORDER_N         0x1088u
#define TFW_H5_PKA_VERIF_OUT_RESULT         0x05D0u

#endif /* TFW_CRYPTO_H5_CRYPTO_REGS_H */
