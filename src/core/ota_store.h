/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_OTA_STORE_H
#define TFW_OTA_STORE_H

#include <stdint.h>
#include "tfw/status.h"

/*
 * The external store the application stages an OTA image into -- issue #75,
 * docs/ota/adr-ota-image-format-and-trust.md decision 6.
 *
 * One driver file implements this, chosen by OTA_STORE in the board's
 * profile.mk and linked by mk/firmware.mk; there is no table of drivers and no
 * pointer to pick one at run time. Addresses are the store's own, and the
 * layout -- STAGE, the manifest sector, PREV, the MARK sector -- is board data
 * in board_profile.h (TFW_OTA_STORE_*).
 *
 * A write programs bytes that are erased; it never erases. The caller erases a
 * sector before it writes into it, because only the caller knows whether the
 * sector already holds bytes of the same image.
 */

#define TFW_STORE_SECTOR  4096ul   /* the erase unit */
#define TFW_STORE_CHUNK   64u      /* the most one read or write moves */

/* Selects the part and parks every other select on the shared bus high. Safe to
 * call again: the stager calls it at each OTA_BEGIN. */
tfw_status_t tfw_store_init(void);

/* Erases the sector holding addr. Returns once the part reports ready, or
 * TFW_ERR_TIMEOUT when it does not within the part's worst case. */
tfw_status_t tfw_store_erase(uint32_t addr);

/* len <= TFW_STORE_CHUNK, else TFW_ERR_RANGE. */
tfw_status_t tfw_store_read(uint32_t addr, uint8_t *buf, uint8_t len);
tfw_status_t tfw_store_write(uint32_t addr, const uint8_t *src, uint8_t len);

#endif
