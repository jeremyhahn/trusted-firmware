/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The part-specific half of the eFuse-UDS provisioning backend
 * (provision_uds.c): what a part whose UDS is a one-time key it burns itself
 * must supply. The ESP32's is src/hal/esp32/prov_uds.c; the host's is
 * test/mock/prov_uds_mock.c. Substituted at link time, like include/tfw/hal.h,
 * and linked only into a PROVISION=1 image of a tier that names it.
 *
 * Not one of these returns the UDS. The draw hands it to the burn and the
 * caller scrubs it; after the burn the part is the only holder, and the HMAC
 * is computed by the part under it.
 */
#ifndef TFW_PROVISION_UDS_H
#define TFW_PROVISION_UDS_H

#include <stddef.h>
#include <stdint.h>
#include "tfw/status.h"

/* 1 when the part already holds a UDS key, else 0. */
int tfw_prov_uds_present(void);

/* 32 bytes from the part's true random source, with that source running for
 * the length of the draw. TFW_ERR_BUSY when the source cannot be started. */
tfw_status_t tfw_prov_uds_random(uint8_t uds[32]);

/* Burn uds as the UDS key, read- and write-protected, and confirm the part
 * reports it unreadable. TFW_ERR_NO_SLOT when no key block is free,
 * TFW_ERR_CRYPTO when the burn or the confirmation fails. */
tfw_status_t tfw_prov_uds_burn(const uint8_t uds[32]);

/* HMAC-SHA256 of msg under the burned UDS, computed by the part.
 * TFW_ERR_CRYPTO when the part refuses. */
tfw_status_t tfw_prov_uds_hmac(const uint8_t *msg, size_t len, uint8_t out[32]);

#endif /* TFW_PROVISION_UDS_H */
