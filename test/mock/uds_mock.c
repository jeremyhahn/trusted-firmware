/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The device secret, for tests. Settable, so BOTH paths are reachable: the
 * refusal an unprovisioned device must give, and the evidence a provisioned one
 * must produce. Linked instead of uds_flash.c; the provider cannot tell.
 */

#include <string.h>
#include "uds_mock.h"

static uint8_t uds[32];
static uint8_t have;

void ag_mock_set_uds(const uint8_t *u)
{
    if (u) { memcpy(uds, u, sizeof uds); have = 1u; }
    else   { memset(uds, 0, sizeof uds); have = 0u; }
}

const uint8_t *ag_crypto_soft_uds(void)
{
    return have ? uds : 0;
}
