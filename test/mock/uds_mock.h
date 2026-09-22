/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_UDS_MOCK_H
#define AG_UDS_MOCK_H
#include <stdint.h>
/* Pass NULL to model an unprovisioned device. */
void           ag_mock_set_uds(const uint8_t *uds32);
const uint8_t *ag_crypto_soft_uds(void);
#endif
