/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_UDS_MOCK_H
#define TFW_UDS_MOCK_H
#include <stdint.h>
/* Pass NULL to model an unprovisioned device. */
void           tfw_mock_set_uds(const uint8_t *uds32);
const uint8_t *tfw_crypto_soft_uds(void);
#endif
