/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The console's pads, asked of the System Manager (console_pins.h).
 */
#include <stdint.h>

#include "console_pins.h"
#include "scmi.h"
#include "tfw/config.h"

static int32_t pin_set(struct tfw_scmi_chan *c, uint32_t pin, uint32_t mux, uint32_t pad_cfg)
{
    struct tfw_scmi_msg cmd, rsp;
    const uint32_t types[2]  = { TFW_SCMI_PINCTRL_TYPE_MUX, TFW_SCMI_PINCTRL_TYPE_CONFIG };
    const uint32_t values[2] = { mux, pad_cfg };
    int32_t  rc;

    rc = tfw_scmi_pinctrl_settings_configure(&cmd, tfw_scmi_token(c), pin, types, values,
                                             pad_cfg != 0u ? 2u : 1u);
    if (rc != 0) {
        return rc;
    }
    return tfw_scmi_xfer(c, &cmd, &rsp);
}

int32_t tfw_imx95_console_pins_set(struct tfw_scmi_chan *c,
                                   const struct tfw_imx95_console_pins *p)
{
    int32_t rc;

    if (c->mu == 0u || c->shm == 0u) {
        return TFW_SCMI_E_CHANNEL;
    }
    rc = pin_set(c, p->tx_pin, p->mux, p->pad_cfg);
    if (rc != 0) {
        return rc;
    }
    return pin_set(c, p->rx_pin, p->mux, p->pad_cfg);
}

static struct tfw_scmi_chan g_chan = {
    TFW_IMX95_SCMI_MU_BASE, TFW_IMX95_SCMI_SHM_BASE, TFW_IMX95_SCMI_SHM_LEN,
    TFW_IMX95_SCMI_DOORBELL, 0u, TFW_IMX95_SCMI_CRC
};

static const struct tfw_imx95_console_pins g_pins = {
    TFW_IMX95_CONSOLE_TX_PAD, TFW_IMX95_CONSOLE_RX_PAD, TFW_IMX95_CONSOLE_PAD_MUX,
    TFW_IMX95_CONSOLE_PAD_CFG
};

int32_t tfw_imx95_console_route(void)
{
    return tfw_imx95_console_pins_set(&g_chan, &g_pins);
}
