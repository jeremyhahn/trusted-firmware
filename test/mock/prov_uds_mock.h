/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* A part with one-time UDS key storage, for src/caps/provision_uds.c's suite.
 * The controls and the record are the test's; the part's API is
 * src/caps/provision_uds.h. */
#ifndef TFW_PROV_UDS_MOCK_H
#define TFW_PROV_UDS_MOCK_H

#include <stdint.h>
#include "tfw/status.h"

/* An empty part: no key burned, every call succeeds, and the draw gives
 * fill, fill+1, ... fill+31. */
void tfw_mock_prov_uds_reset(uint8_t fill);
/* The status every later draw, burn or HMAC answers with; TFW_OK restores
 * success. A failed burn burns nothing. */
void tfw_mock_prov_uds_fail_random(tfw_status_t st);
void tfw_mock_prov_uds_fail_burn(tfw_status_t st);
void tfw_mock_prov_uds_fail_hmac(tfw_status_t st);

/* What the part holds -- only a test can see it, the firmware cannot. 0 when
 * nothing is burned. */
const uint8_t *tfw_mock_prov_uds_burned(void);
unsigned       tfw_mock_prov_uds_draws(void);
unsigned       tfw_mock_prov_uds_burns(void);

#endif /* TFW_PROV_UDS_MOCK_H */
