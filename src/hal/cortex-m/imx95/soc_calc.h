/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's arithmetic and tables, with no register access, so the
 * host tests link the same file the M7 image does (test/unit/
 * test_imx95_soc_calc.c).
 */
#ifndef TFW_CM_IMX95_SOC_CALC_H
#define TFW_CM_IMX95_SOC_CALC_H

#include <stdint.h>
#include "tfw/status.h"

/* The LPUART's BAUD fields: OSR, the oversampling ratio less one, in bits 28
 * to 24; SBR, the modulo divisor, in bits 12 to 0; BOTHEDGE, bit 17, which
 * the block requires at a ratio of 4 to 7. The baud is clk / (OSR+1) / SBR. */
#define TFW_IMX95_LPUART_BAUD_OSR_SHIFT 24u
#define TFW_IMX95_LPUART_BAUD_BOTHEDGE  0x00020000u
#define TFW_IMX95_LPUART_SBR_MAX        8191u
#define TFW_IMX95_LPUART_OSR_MIN        4u
#define TFW_IMX95_LPUART_OSR_MAX        32u

/* The worst error accepted, in parts per thousand of the asked baud: 3 %,
 * inside an 8N1 receiver's tolerance on both ends of the link. */
#define TFW_IMX95_LPUART_ERR_PPT_MAX 30u

/* The BAUD value nearest `baud` from `clk_hz`: every ratio from 4 to 32 is
 * tried, the one with the smallest error wins, and of equal errors the higher
 * ratio, which samples each bit more often. TFW_ERR_RANGE for a zero baud or
 * clock, or when no divisor comes within TFW_IMX95_LPUART_ERR_PPT_MAX. */
tfw_status_t tfw_imx95_lpuart_baud(uint32_t clk_hz, uint32_t baud, uint32_t *baud_reg);

#define TFW_IMX95_GPIO_PORTS 5u

/* The base of RGPIO bank `port` (0 is GPIO1), or 0 past the last bank. */
uint32_t tfw_imx95_gpio_base(uint8_t port);

#endif /* TFW_CM_IMX95_SOC_CALC_H */
