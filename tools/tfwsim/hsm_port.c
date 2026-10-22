/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The response engine's port in the element-tier simulator (docs/
 * hsm-self-defence/ slice 4). A host has no PHY, PMIC or enclave behind it, so
 * every action is done at once; the channel's replies say what the engine
 * decided, which is what a host exercising the channel needs. The tenant key
 * is the throwaway one the literal envelopes are signed with
 * (testdata/suit_vectors.h, private scalar 0x40..0x5F), never a real key: a
 * simulator verifies exactly what the vectors carry.
 */

#include "hsm_port.h"
#include "suit_vectors.h"

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    (void)action;
    return TFW_OK;
}

const uint8_t *tfw_hsm_port_key(void)
{
    return SUIT_TENANT_PUB;
}
