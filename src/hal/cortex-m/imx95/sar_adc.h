/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The i.MX 95's 12-bit SAR ADC, the block the i.MX 93 also carries (SPEC row
 * `imx95`, epic #22). Offsets and bits only; the base is the board's
 * (TFW_IMX95_ADC_BASE in board_profile.h), zero while no source gives the
 * instance the System Manager hands the M7.
 *
 * Source: Linux drivers/iio/adc/imx93_adc.c (docs/reference/README.md, the
 * imx93_adc row), read for the register layout only, no code taken. This
 * project does not hold IMX95RM: every offset and bit here is TO CONFIRM
 * against it, as soc_regs.h marks its own.
 *
 * Kept out of soc_regs.h so the I2C and PWM lanes do not touch the same lines.
 */
#ifndef TFW_IMX95_SAR_ADC_H
#define TFW_IMX95_SAR_ADC_H

#include "cm_regs.h"

#define TFW_IMX95_ADC_REG(base, off) TFW_CM_REG((base) + (off))

/* Register offsets (imx93_adc.c, IMX93_ADC_*). TO CONFIRM. */
#define TFW_IMX95_ADC_MCR      0x000u   /* main configuration */
#define TFW_IMX95_ADC_MSR      0x004u   /* main status */
#define TFW_IMX95_ADC_ISR      0x010u   /* interrupt status, write 1 to clear */
#define TFW_IMX95_ADC_CEOCFR0  0x014u   /* channel end-of-conversion flags, w1c */
#define TFW_IMX95_ADC_IMR      0x020u   /* interrupt mask */
#define TFW_IMX95_ADC_CIMR0    0x024u   /* channel interrupt mask */
#define TFW_IMX95_ADC_NCMR0    0x0A4u   /* normal conversion channel mask */
#define TFW_IMX95_ADC_PCDR(n)  (0x100u + 4u * (uint32_t)(n))  /* channel n data */
#define TFW_IMX95_ADC_CALSTAT  0x39Cu   /* calibration status (diagnostic) */

/* MCR (imx93_adc.c IMX93_ADC_MCR_*_MASK). TO CONFIRM. */
#define TFW_IMX95_ADC_MCR_MODE     (1uL << 29)  /* 1 scan, 0 one-shot */
#define TFW_IMX95_ADC_MCR_NSTART   (1uL << 24)  /* start a normal conversion */
#define TFW_IMX95_ADC_MCR_CALSTART (1uL << 14)  /* start calibration */
#define TFW_IMX95_ADC_MCR_ADCLKSE  (1uL << 8)   /* 1 bus clock, 0 bus clock / 2 */
#define TFW_IMX95_ADC_MCR_PWDN     (1uL << 0)   /* power down */

/* MSR. TO CONFIRM. */
#define TFW_IMX95_ADC_MSR_CALFAIL       (1uL << 30)
#define TFW_IMX95_ADC_MSR_CALBUSY       (1uL << 29)
#define TFW_IMX95_ADC_MSR_ADCSTATUS     0x7uL       /* bits 2:0 */
#define TFW_IMX95_ADC_STATUS_IDLE       0u
#define TFW_IMX95_ADC_STATUS_POWER_DOWN 1u

/* ISR and IMR. TO CONFIRM. */
#define TFW_IMX95_ADC_ISR_ECH  (1uL << 0)   /* end of the chain */
#define TFW_IMX95_ADC_ISR_EOC  (1uL << 1)   /* end of a channel's conversion */

/* PCDRn: the 12-bit result. TO CONFIRM. */
#define TFW_IMX95_ADC_PCDR_CDATA 0x0FFFuL

/* Channels in CEOCFR0, NCMR0 and the PCDRs: the block has eight. TO CONFIRM. */
#define TFW_IMX95_ADC_CHANNELS 8u

#endif /* TFW_IMX95_SAR_ADC_H */
