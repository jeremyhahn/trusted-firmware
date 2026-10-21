/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The address provider for an Ethernet image built without the DHCP client
 * (src/net/dhcp.c, make DHCP=1): the network block's own addresses. A block
 * that asks for DHCP is refused, so the link stays down and backs off rather
 * than connecting from an address nobody gave it.
 */

#include "tfw/eth.h"

tfw_status_t tfw_dhcp_poll(tfw_netcfg_t *cfg)
{
    uint8_t i;

    if (!(cfg->flags & TFW_NETCFG_DHCP)) {
        return TFW_OK;
    }
    /* No address on a refusal, as dhcp.c gives none before a lease: a block
     * that asks for DHCP carries zeros here, and nothing may connect from them. */
    for (i = 0u; i < 4u; i++) {
        cfg->ip[i]      = 0u;
        cfg->mask[i]    = 0u;
        cfg->gateway[i] = 0u;
    }
    return TFW_ERR_UNSUPPORTED;
}

uint8_t tfw_dhcp_due(void)
{
    return 0u;
}

tfw_status_t tfw_dhcp_renew(const tfw_netcfg_t *cfg)
{
    (void)cfg;
    return TFW_OK;
}
