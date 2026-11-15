/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The console's pads on the i.MX 95 M7. The System Manager owns the IOMUX;
 * the M7 asks it, with SCMI pin control (scmi.h,
 * PINCTRL_SETTINGS_CONFIGURE), to route LPUART7 to its pads before the
 * console starts (hal_uart.c).
 */
#ifndef TFW_CM_IMX95_CONSOLE_PINS_H
#define TFW_CM_IMX95_CONSOLE_PINS_H

#include <stdint.h>

#include "scmi_mu.h"

struct tfw_imx95_console_pins {
    uint32_t tx_pin;    /* the SM's pin ids */
    uint32_t rx_pin;
    uint32_t mux;       /* the pads' LPUART alternate */
    uint32_t pad_cfg;   /* the pad register's value; 0: not sent */
};

/* PINCTRL_SETTINGS_CONFIGURE on TX, then RX: the mux, and the pad
 * configuration when not 0. 0 when both are accepted; TFW_SCMI_E_* or the
 * platform's status of the first refused, and RX is then not asked.
 * TFW_SCMI_E_CHANNEL with no channel (a zero MU or shared memory base). */
int32_t tfw_imx95_console_pins_set(struct tfw_scmi_chan *c,
                                   const struct tfw_imx95_console_pins *p);

/* The board's channel and pins (board_profile.h). hal_uart.c calls it before
 * the console starts; the console starts whatever it returns, as the SM may
 * have routed the pads already. */
int32_t tfw_imx95_console_route(void);

#endif /* TFW_CM_IMX95_CONSOLE_PINS_H */
