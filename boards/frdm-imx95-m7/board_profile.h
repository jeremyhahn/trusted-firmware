/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * NXP FRDM-IMX95-PRO, the i.MX 95's Cortex-M7 (SPEC row `imx95`): a board of
 * the generic Cortex-M port. The board is NXP UM12527 ("FRDM-IMX95-PRO Board
 * User Manual", Rev. 1.0), the SoC NXP IMX95RM ("i.MX 95 Applications
 * Processor Reference Manual"), which this project does not hold: every SoC
 * fact here and in src/hal/cortex-m/imx95/ is TO CONFIRM against it.
 *
 * The byte link is LPUART3, the M7's debug UART on NXP's i.MX 95 boards (TO
 * CONFIRM against UM12527, the debug UART section). The pins are two RGPIO
 * lines and every other capability is off by file selection. The System
 * Manager owns the pin mux (IOMUXC) and assigns pins and peripherals to the
 * M7, so the HAL muxes nothing: a pin the configuration did not give the M7
 * does not move.
 *
 * The board id is PROVISIONAL (boards/frdm-imx95-m7/profile.mk).
 */
#ifndef TFW_BOARD_FRDM_IMX95_M7_H
#define TFW_BOARD_FRDM_IMX95_M7_H

#define TFW_BOARD_NAME "frdm-imx95-m7"
#define TFW_BOARD_ID   0x41u   /* provisional */

/* The SAR ADC is built only in the soft:ADC=1 row (profile.mk): -DTFW_CM_ADC=1
 * appends the analog pins after the two GPIO pins and names the channels. */
#ifndef TFW_CM_ADC
#define TFW_CM_ADC 0
#endif

/* The two GPIO pins that carry the TPM's PWM channels (board.c), after
 * every pin above, so they add to TFW_PIN_COUNT by their own term. */
#define TFW_GPIO_PWM_PINS 2u

#if TFW_CM_ADC
#define TFW_PIN_COUNT  (4u + TFW_GPIO_PWM_PINS)
#else
#define TFW_PIN_COUNT  (2u + TFW_GPIO_PWM_PINS)
#endif
#define TFW_PORT_COUNT 5u      /* GPIO1 to GPIO5 */
#ifdef TFW_CM_PWM
#define TFW_PWM_COUNT  2u
#else
#define TFW_PWM_COUNT  0u
#endif
#if TFW_CM_ADC
#define TFW_ADC_COUNT  2u      /* channels 0 and 1, the analog pins' */
#else
#define TFW_ADC_COUNT  0u
#endif
#define TFW_EEPROM_SIZE 0u

#if TFW_CM_ADC
#define TFW_FEATURE_ANALOG  1
#else
#define TFW_FEATURE_ANALOG  0
#endif
#ifdef TFW_CM_PWM
#define TFW_FEATURE_PWM     1
#else
#define TFW_FEATURE_PWM     0
#endif
#define TFW_FEATURE_TONE    0
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
#define TFW_FEATURE_I2C     0
#define TFW_FEATURE_SPI     0
#define TFW_FEATURE_ONEWIRE 0

/* The soft root, as on mps2-an505, until the EdgeLock enclave tier: the UDS
 * in tfw-boot's region, the CDI handed over in the DTCM. */
#define TFW_FEATURE_ATTEST 1

/* The M7's core clock, which SysTick counts. The System Manager configuration
 * sets it; 800 MHz is the part's M7 maximum (NXP IMX95CEC, the i.MX 95 data
 * sheet; TO CONFIRM against the configuration trusted-imx95 ships). */
#define TFW_CM_CPU_HZ 800000000uL

/* LPUART3's functional clock root, the 24 MHz oscillator (TO CONFIRM: the
 * System Manager configuration sets the root). The baud divisor counts it. */
#define TFW_IMX95_LPUART_CLK_HZ 24000000uL

/* The M7's TPM for PWM (src/hal/cortex-m/imx95/hal_pwm.c, tpm.h). The low-
 * power domain has two, tpm1 and tpm2 (Linux imx95.dtsi; tpm.h lists every
 * base), and which one the System Manager gives the M7 is open
 * (docs/multi-arch/imx95-design.md question 17): zero, unsupported, until it
 * is known; every PWM call answers TFW_ERR_UNSUPPORTED and touches no
 * register. Its clock root, the 24 MHz oscillator as LPUART3's, is TO
 * CONFIRM: the System Manager configuration sets it. No image overrides the
 * base; mk/host.mk's test_board_frdm-imx95-m7_pwm_base passes a stand-in one
 * so the board's PWM pin mapping is checked as it will be once it is known. */
#ifndef TFW_IMX95_TPM_BASE
#define TFW_IMX95_TPM_BASE    0x00000000uL
#endif
#define TFW_IMX95_TPM_CLK_HZ  24000000uL

/* Flash geometry for tfw-boot, as addresses in the M7's ITCM. TFW_FLASH_TOTAL
 * is the ITCM's end (include/tfw/boot.h asserts both regions fit below it and
 * do not overlap). The application region is the code budget, FLASH_BUDGET. */
#define TFW_BOOT_REGION_BASE  0x00000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x00008000uL
#define TFW_APP_REGION_SIZE   0x00030000uL   /* 192 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x00040000uL   /* 256 KiB of ITCM */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of the ITCM. TCM: it survives a reset
 * and a reload, not a power cycle. */
#define TFW_CM_PLATFORM_BASE  0x0003F000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* The M7 agent's SCMI channel to the System Manager and the domains the
 * tamper-response engine's A1 and A2 switch through it (src/hal/cortex-m/imx95/
 * hsm_port.c; docs/hsm-self-defence/m7-design.md section 7, slice 5). Every
 * value is the System Manager configuration's (trusted-imx95's), and no
 * document this project holds gives one: OPEN, docs/multi-arch/imx95-design.md
 * question 21 (and, for the PHYs' domain, hsm-self-defence architecture Q10).
 * Zero means unsupported: an action whose id is zero, or any action while the
 * channel is zero, answers TFW_ERR_UNSUPPORTED and sends nothing. */
#define TFW_IMX95_SCMI_MU_BASE   0x00000000uL   /* the MU's base */
#define TFW_IMX95_SCMI_SHM_BASE  0x00000000uL   /* the shared memory area's base */
#define TFW_IMX95_SCMI_SHM_LEN   0u             /* its length in bytes */
#define TFW_IMX95_SCMI_DOORBELL  0u             /* the MU GCR bit */

/* The SAR ADC instance the System Manager gives the M7 (src/hal/cortex-m/
 * imx95/sar_adc.h), open as design questions 13 and 17 are. TO CONFIRM
 * against IMX95RM and the pinned MIMX9596_cm7.h or imx95.dtsi
 * (docs/reference/README.md); no pinned source is held yet, so it stays zero,
 * which means unsupported: init succeeds, every read answers
 * TFW_ERR_UNSUPPORTED, and no register is touched. */
#ifndef TFW_IMX95_ADC_BASE
#define TFW_IMX95_ADC_BASE       0x00000000uL   /* the SAR ADC's base */
#endif
#define TFW_IMX95_HSM_PHY_RESET  0u             /* A1: the PHYs' reset domain */
#define TFW_IMX95_HSM_PHY_POWER  0u             /* A1: the PHYs' power domain */
#define TFW_IMX95_HSM_A55_POWER  0u             /* A2: the A55's power domain */
/* A5 names no domain: SYSTEM_POWER_STATE_SET (DEN0056E section 4.4.2.5, in
 * scmi.c) acts on the whole system. What is open is whether the M7's agent may
 * send it (hsm-self-defence architecture question 7); 1 once it may. */
#define TFW_IMX95_HSM_SHUTDOWN   0u

/* The enclave's side of the engine, S3 and A4 (hsm_port.c; hsm-self-defence
 * engineering.md slice 6). Every value here is a PLACEHOLDER: no document
 * this project holds gives it, and none is invented. Zero means unsupported,
 * as above: while the MU is zero, S3 measures nothing and raises nothing and
 * A4 answers TFW_ERR_UNSUPPORTED, so the engine locks down (R3) having wiped
 * nothing; while the delete's id or every key id is zero, A4 does the same.
 *
 * The M7's MU to the enclave, and its interrupt index in SESSION_OPEN: OPEN,
 * docs/multi-arch/imx95-design.md question 3. */
#define TFW_IMX95_ELE_MU_BASE    0x00000000uL
#define TFW_IMX95_ELE_MU_IRQ     0u
/* The key store A4 opens and the keys it deletes are go-xkms's (architecture
 * H18, question 19), and the enclave binds a store to the domain and MU that
 * created it (question 4), so the M7 may be refused it. */
#define TFW_IMX95_HSM_KEY_STORE  0u             /* the store's id */
#define TFW_IMX95_HSM_KEY_NONCE  0u             /* its nonce */
#define TFW_IMX95_HSM_WIPE_KEYS  0u             /* the key ids, at most four */
/* The delete-key message (ele_mu.h struct tfw_ele_del_layout): RM00284 gives
 * the call and its strict flag, not the message's id or words. */
#define TFW_IMX95_ELE_DEL_CMD          0u       /* the message id */
#define TFW_IMX95_ELE_DEL_CMD_WORDS    0u       /* request words, header counted */
#define TFW_IMX95_ELE_DEL_RSP_WORDS    0u       /* response words */
#define TFW_IMX95_ELE_DEL_W_HANDLE     0u       /* the key management handle's word */
#define TFW_IMX95_ELE_DEL_W_KEY        0u       /* the key id's word */
#define TFW_IMX95_ELE_DEL_W_FLAGS      0u       /* the flags' word */
#define TFW_IMX95_ELE_DEL_FLAGS_SHIFT  0u       /* the flags' bit offset in it */
/* S3's expected-value keys in the policy set (adr-response-policy-format.md
 * question 9), one per field the attestation reports; 0 is no key, and an
 * expected value under a key the port does not know is a mismatch. */
#define TFW_IMX95_S3_KEY_LMDA        0u         /* the lifecycle */
#define TFW_IMX95_S3_KEY_ROM_PATCH   0u         /* the ROM patch hash */
#define TFW_IMX95_S3_KEY_SHA_FW      0u         /* the enclave firmware's hash */
#define TFW_IMX95_S3_KEY_OEM_SRKH    0u         /* the OEM SRK hash */
#define TFW_IMX95_S3_KEY_SRK_REVOKE  0u         /* the committed SRK revocation bits */
/* Where GET_INFO's buffer holds the ROM patch hash and the OEM SRK hash.
 * RM00284 section 3.13 names both fields, but no source this project holds
 * gives their byte offset or length in the response. Length 0: the port
 * cannot read them, so a policy that expects either is a mismatch. */
#define TFW_IMX95_ELE_INFO_ROM_PATCH_OFF  0u
#define TFW_IMX95_ELE_INFO_ROM_PATCH_LEN  0u
#define TFW_IMX95_ELE_INFO_OEM_SRKH_OFF   0u
#define TFW_IMX95_ELE_INFO_OEM_SRKH_LEN   0u
/* Where GET_INFO's buffer holds the SRK revocation bits: length 0, the port
 * cannot read them, so a policy that expects them is a mismatch. */
#define TFW_IMX95_ELE_INFO_SRK_REVOKE_OFF  0u
#define TFW_IMX95_ELE_INFO_SRK_REVOKE_LEN  0u
/* The signal intake (#50 slice 10, hsm_intake.h), zero until known: an id of
 * 0 is never matched and nothing is sent to look for it. S1 is the System
 * Manager's notification, its protocol and message ids open (IMX95SRM). */
#define TFW_IMX95_HSM_S1_PROTO  0u
#define TFW_IMX95_HSM_S1_MSG    0u
/* S2 is polled with hsm_get_event (architecture question 6 (a), unconfirmed
 * until the slice 10 bench test): its event code, the message's id and words
 * (ele_mu.h struct tfw_ele_event_layout) are open (IMX95RM,
 * imx-secure-enclave). */
#define TFW_IMX95_HSM_S2_EVENT         0u
#define TFW_IMX95_ELE_EVT_CMD          0u       /* the message id */
#define TFW_IMX95_ELE_EVT_CMD_WORDS    0u       /* request words, header counted */
#define TFW_IMX95_ELE_EVT_RSP_WORDS    0u       /* response words */
#define TFW_IMX95_ELE_EVT_W_SESSION    0u       /* the session handle's word */
#define TFW_IMX95_ELE_EVT_W_COUNT      0u       /* the response's event count word */
#define TFW_IMX95_ELE_EVT_W_FIRST      0u       /* the response's first event word */
/* S5 is an M7 GPIO pin interrupt (question 1 (a)); its pad and the interrupt
 * the System Manager assigns it are open (board schematic, question 11 (c)).
 * IRQ 0 is a real number, so the vector is installed only once ARMED is 1. */
#define TFW_IMX95_HSM_S5_IRQ    0u
#define TFW_IMX95_HSM_S5_ARMED  0u

/* The device attestation message (ele_mu.h struct tfw_ele_attest_layout;
 * m7-design.md slice 9): RM00284 section 3.12 gives the call, its nonce and
 * the fields it reports, not the message's id or words, nor the report's
 * size and layout (architecture question 17). While the id is zero S3 asks
 * GET_INFO instead, as above. */
#define TFW_IMX95_ELE_ATT_CMD          0u       /* the message id */
#define TFW_IMX95_ELE_ATT_CMD_WORDS    0u       /* request words, header counted */
#define TFW_IMX95_ELE_ATT_RSP_WORDS    0u       /* response words */
#define TFW_IMX95_ELE_ATT_W_SESSION    0u       /* the session handle's word; 0, none */
#define TFW_IMX95_ELE_ATT_W_NONCE      0u       /* the nonce's first word */
#define TFW_IMX95_ELE_ATT_W_ADDR_HI    0u       /* the report buffer's address, high */
#define TFW_IMX95_ELE_ATT_W_ADDR_LO    0u       /* low */
#define TFW_IMX95_ELE_ATT_W_SIZE       0u       /* the report buffer's size */
#define TFW_IMX95_ELE_ATT_REPORT_LEN   0u       /* the report's size in bytes */
/* Where the report holds each field S3 compares, under the keys above.
 * Length 0: the port cannot read it, so a policy that expects it is a
 * mismatch. */
#define TFW_IMX95_ELE_ATT_LMDA_OFF        0u
#define TFW_IMX95_ELE_ATT_LMDA_LEN        0u
#define TFW_IMX95_ELE_ATT_ROM_PATCH_OFF   0u
#define TFW_IMX95_ELE_ATT_ROM_PATCH_LEN   0u
#define TFW_IMX95_ELE_ATT_SHA_FW_OFF      0u
#define TFW_IMX95_ELE_ATT_SHA_FW_LEN      0u
#define TFW_IMX95_ELE_ATT_OEM_SRKH_OFF    0u
#define TFW_IMX95_ELE_ATT_OEM_SRKH_LEN    0u
#define TFW_IMX95_ELE_ATT_SRK_REVOKE_OFF  0u
#define TFW_IMX95_ELE_ATT_SRK_REVOKE_LEN  0u

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_FRDM_IMX95_M7_H */
