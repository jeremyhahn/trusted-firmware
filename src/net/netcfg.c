/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The network block: where the device is and where it connects. See
 * include/tfw/eth.h for the layout and docs/ethernet-transport/ for why it is
 * static, provisioned data rather than DHCP.
 */

#include "tfw/eth.h"
#include "tfw/hal.h"
#include "crc16.h"

tfw_status_t tfw_netcfg_decode(const uint8_t *raw, tfw_netcfg_t *out)
{
    uint8_t i;

    if (raw[0] != 'T' || raw[1] != 'F' || raw[2] != 'N' || raw[3] != 'C' ||
        raw[4] != TFW_NETCFG_VERSION ||
        tfw_crc16(raw, 30u) != (uint16_t)(raw[30] | (uint16_t)raw[31] << 8)) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    /* A zero peer or port is refused before anything is written, so a
     * refused block never leaves *out partly decoded. */
    if ((raw[28] | raw[29]) == 0u ||
        (raw[24] | raw[25] | raw[26] | raw[27]) == 0u) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    out->flags = raw[5];
    for (i = 0u; i < 6u; i++) {
        out->mac[i] = raw[6u + i];
    }
    for (i = 0u; i < 4u; i++) {
        out->ip[i]      = raw[12u + i];
        out->mask[i]    = raw[16u + i];
        out->gateway[i] = raw[20u + i];
        out->peer[i]    = raw[24u + i];
    }
    out->port = (uint16_t)((uint16_t)raw[28] << 8 | raw[29]);
    return TFW_OK;
}

tfw_status_t tfw_netcfg_load(tfw_netcfg_t *out)
{
    uint8_t raw[TFW_NETCFG_LEN];

    if (tfw_hal_eeprom_read(TFW_NETCFG_ADDR, raw, (uint8_t)sizeof raw) != TFW_OK) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    return tfw_netcfg_decode(raw, out);
}
