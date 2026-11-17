/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 HASH, PKA and RCC facts the host models decode with (#544),
 * restated as literals from ST's sources and NOT derived from
 * src/crypto/stm32h5/h5_crypto_regs.h, so a wrong driver definition fails
 * the host suites instead of being read back by the model:
 *
 *   CMSIS  STMicroelectronics/cmsis-device-h5
 *          @ 884b8dc78e41cbfca008363342b17f4a9e8641f7, Include/stm32h573xx.h
 *   HAL    STMicroelectronics/stm32h5xx-hal-driver
 *          @ 16fea2d290119d6349b8f9a3b8538781ba1e1bdc,
 *          Inc/stm32h5xx_hal_hash.h, Inc/stm32h5xx_hal_pka.h
 *
 * Each line names the ST definition it copies. Offsets are bytes from the
 * peripheral base; the PKA RAM ones are the CMSIS byte offsets before its
 * ((x - PKA_RAM_OFFSET) >> 2) word conversion.
 */
#ifndef TFW_TEST_H5_VENDOR_FACTS_H
#define TFW_TEST_H5_VENDOR_FACTS_H

/* HASH_TypeDef: CR 0x00, DIN 0x04, STR 0x08, SR 0x24; HASH_DIGEST_TypeDef HR
 * at HASH_DIGEST_BASE_NS - HASH_BASE_NS = 0xA0710 - 0xA0400. */
#define ST_HASH_CR          0x000u
#define ST_HASH_DIN         0x004u
#define ST_HASH_STR         0x008u
#define ST_HASH_SR          0x024u
#define ST_HASH_HR          0x310u

#define ST_HASH_CR_INIT         0x00000004u /* HASH_CR_INIT */
#define ST_HASH_CR_DATATYPE     0x00000030u /* HASH_CR_DATATYPE_Msk */
#define ST_HASH_DATATYPE_BYTE   0x00000020u /* HASH_BYTE_SWAP = HASH_CR_DATATYPE_1 */
#define ST_HASH_CR_MODE         0x00000040u /* HASH_CR_MODE (set = HMAC) */
#define ST_HASH_CR_ALGO         0x001E0000u /* HASH_CR_ALGO_0..3 */
#define ST_HASH_ALGO_SHA256     0x00060000u /* HASH_ALGOSELECTION_SHA256 = ALGO_0 | ALGO_1 */
#define ST_HASH_STR_NBLW        0x0000001Fu /* HASH_STR_NBLW_Msk */
#define ST_HASH_STR_DCAL        0x00000100u /* HASH_STR_DCAL */
#define ST_HASH_SR_DINIS        0x00000001u /* HASH_SR_DINIS */
#define ST_HASH_SR_DCIS         0x00000002u /* HASH_SR_DCIS */
#define ST_HASH_SR_BUSY         0x00000008u /* HASH_SR_BUSY */

/* PKA_TypeDef: CR 0x00, SR 0x04, CLRFR 0x08, RAM[1334] at 0x400..0x18D4. */
#define ST_PKA_CR           0x000u
#define ST_PKA_SR           0x004u
#define ST_PKA_CLRFR        0x008u
#define ST_PKA_RAM          0x400u
#define ST_PKA_RAM_LAST     0x18D4u     /* RAM[1333], the last word */

#define ST_PKA_CR_EN            0x00000001u /* PKA_CR_EN */
#define ST_PKA_CR_START         0x00000002u /* PKA_CR_START */
#define ST_PKA_CR_MODE_POS      8u          /* PKA_CR_MODE_Pos */
#define ST_PKA_CR_MODE          0x00003F00u /* PKA_CR_MODE_Msk */
#define ST_PKA_SR_INITOK        0x00000001u /* PKA_SR_INITOK */
#define ST_PKA_SR_BUSY          0x00010000u /* PKA_SR_BUSY */
#define ST_PKA_SR_PROCENDF      0x00020000u /* PKA_SR_PROCENDF */
#define ST_PKA_SR_RAMERRF       0x00080000u /* PKA_SR_RAMERRF */
#define ST_PKA_SR_ADDRERRF      0x00100000u /* PKA_SR_ADDRERRF */
#define ST_PKA_SR_OPERRF        0x00200000u /* PKA_SR_OPERRF */
#define ST_PKA_CLRFR_PROCENDFC  0x00020000u /* PKA_CLRFR_PROCENDFC */
#define ST_PKA_CLRFR_RAMERRFC   0x00080000u /* PKA_CLRFR_RAMERRFC */
#define ST_PKA_CLRFR_ADDRERRFC  0x00100000u /* PKA_CLRFR_ADDRERRFC */
#define ST_PKA_CLRFR_OPERRFC    0x00200000u /* PKA_CLRFR_OPERRFC */
#define ST_PKA_SR_ERRORS        (ST_PKA_SR_RAMERRF | ST_PKA_SR_ADDRERRF | ST_PKA_SR_OPERRF)
#define ST_PKA_CLRFR_ALL        (ST_PKA_CLRFR_PROCENDFC | ST_PKA_CLRFR_RAMERRFC | \
                                 ST_PKA_CLRFR_ADDRERRFC | ST_PKA_CLRFR_OPERRFC)

#define ST_PKA_MODE_ECDSA_SIGN    0x24u     /* PKA_MODE_ECDSA_SIGNATURE */
#define ST_PKA_MODE_ECDSA_VERIFY  0x26u     /* PKA_MODE_ECDSA_VERIFICATION */
#define ST_PKA_NO_ERROR           0xD60Du   /* PKA_NO_ERROR */

/* PKA_ECDSA_SIGN_* */
#define ST_PKA_SIGN_IN_ORDER_NB_BITS    0x0400u
#define ST_PKA_SIGN_IN_MOD_NB_BITS      0x0408u
#define ST_PKA_SIGN_IN_A_COEFF_SIGN     0x0410u
#define ST_PKA_SIGN_IN_A_COEFF          0x0418u
#define ST_PKA_SIGN_IN_B_COEFF          0x0520u
#define ST_PKA_SIGN_IN_MOD_GF           0x1088u
#define ST_PKA_SIGN_IN_K                0x12A0u
#define ST_PKA_SIGN_IN_INITIAL_POINT_X  0x0578u
#define ST_PKA_SIGN_IN_INITIAL_POINT_Y  0x0470u
#define ST_PKA_SIGN_IN_HASH_E           0x0FE8u
#define ST_PKA_SIGN_IN_PRIVATE_KEY_D    0x0F28u
#define ST_PKA_SIGN_IN_ORDER_N          0x0F88u
#define ST_PKA_SIGN_OUT_ERROR           0x0FE0u
#define ST_PKA_SIGN_OUT_SIGNATURE_R     0x0730u
#define ST_PKA_SIGN_OUT_SIGNATURE_S     0x0788u

/* PKA_ECDSA_VERIF_* */
#define ST_PKA_VERIF_IN_ORDER_NB_BITS   0x0408u
#define ST_PKA_VERIF_IN_MOD_NB_BITS     0x04C8u
#define ST_PKA_VERIF_IN_A_COEFF_SIGN    0x0468u
#define ST_PKA_VERIF_IN_A_COEFF         0x0470u
#define ST_PKA_VERIF_IN_MOD_GF          0x04D0u
#define ST_PKA_VERIF_IN_INITIAL_POINT_X 0x0678u
#define ST_PKA_VERIF_IN_INITIAL_POINT_Y 0x06D0u
#define ST_PKA_VERIF_IN_PUBLIC_KEY_X    0x12F8u /* ..._PUBLIC_KEY_POINT_X */
#define ST_PKA_VERIF_IN_PUBLIC_KEY_Y    0x1350u /* ..._PUBLIC_KEY_POINT_Y */
#define ST_PKA_VERIF_IN_SIGNATURE_R     0x10E0u
#define ST_PKA_VERIF_IN_SIGNATURE_S     0x0C68u
#define ST_PKA_VERIF_IN_HASH_E          0x13A8u
#define ST_PKA_VERIF_IN_ORDER_N         0x1088u
#define ST_PKA_VERIF_OUT_RESULT         0x05D0u

/* RCC_TypeDef CR at 0x00; RCC_CR_HSI48ON_Pos 12, RCC_CR_HSI48RDY_Pos 13. */
#define ST_RCC_CR               0x00u
#define ST_RCC_CR_HSI48ON       0x00001000u
#define ST_RCC_CR_HSI48RDY      0x00002000u

#endif
