/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_OTA_STAGE_H
#define TFW_OTA_STAGE_H

#include "tfw/boot.h"
#include "tfw/status.h"
#include "reader.h"
#include "writer.h"

/*
 * The application-side OTA stager -- issue #75, ADR decision 6.
 *
 * OTA_BEGIN, OTA_DATA, OTA_END and OTA_STATUS carry the same payloads and the
 * same states as tfw-boot's session (boot/boot_session.c), so a host drives
 * either the same way. The difference is where the bytes go: here they stream
 * into the external store's STAGE area, and a verified image leaves a MARK
 * record for tfw-boot to find. Nothing here touches the application's flash.
 */

/* OTA_STATUS's state byte -- tfw-boot's numbering, which is wire. */
#define TFW_STAGE_IDLE       0u
#define TFW_STAGE_RECEIVING  1u
#define TFW_STAGE_COMPLETE   2u
#define TFW_STAGE_FAILED     3u

/* The MARK record tfw-boot reads, TFW_STAGE_MARK_*, is in include/tfw/boot.h:
 * the stager writes it and tfw-boot reads it, so it is defined once. */

/* Parks the store's select high, with the shared bus's other selects, before
 * anything talks on the bus. A NOR whose select floats low answers the
 * transport's SPI traffic. main.c calls it on every image; one without the
 * stager links src/core/ota_stage_none.c, which does nothing. */
void tfw_stage_init(void);

tfw_status_t tfw_stage_cmd_begin(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_stage_cmd_data(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_stage_cmd_end(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_stage_cmd_status(tfw_reader_t *r, tfw_writer_t *w);

/* The image the store's MARK names (issue #77): its trailer's measurement into
 * `hash` and its version into *version. With `full` set, the staged bytes are
 * read back and hashed against that measurement as OTA_END did. TFW_OK only
 * when the MARK is whole, its length in bounds and its hash the trailer's (and,
 * with `full`, the bytes hash to it); TFW_ERR_BAD_STATE otherwise. Reads only:
 * the store, the MARK and the session are left as they were. */
tfw_status_t tfw_stage_marked(uint8_t *hash /* [32] */, uint32_t *version,
                              uint8_t full);

/* OTA_ACTIVATE's half (issue #77): the image OTA_END verified in this session,
 * hashed again, then the MARK written -- the only writer of it. TFW_ERR_BAD_STATE
 * and nothing written unless the session is complete and the bytes verify. */
tfw_status_t tfw_stage_activate(void);

/* The session OTA_END verified (ruling 121): the stream's length into *len and
 * the measurement it verified into `hash`, for OTA_ACTIVATE's manifest check.
 * TFW_ERR_BAD_STATE and nothing written unless the session is complete. */
tfw_status_t tfw_stage_session(uint32_t *len, uint8_t hash[TFW_BOOT_MEASURE_LEN]);

/* OTA_ACTIVATE's manifest check (ruling 121): the SUIT-subset envelope the
 * stream carries after the image -- a little-endian u16 length, then the
 * envelope, in the padding before the trailer -- read from the store and held
 * to the staged trailer by src/core/suit.c's tfw_suit_gate, the one reader the
 * ESP32's layer 0 calls. TFW_ERR_RANGE when no envelope fits there (erased
 * padding reads as 0xFFFF), otherwise the gate's status. src/core/ota_manifest.c
 * on a board that links the reader (OTA_MANIFEST := 1, element tier);
 * src/core/ota_manifest_none.c, which answers TFW_OK, everywhere else. */
tfw_status_t tfw_ota_manifest(tfw_boot_platform_t *p);

/* Whether the next reset hands tfw-boot an image to take (#51): this
 * session's OTA_ACTIVATE wrote the MARK, and no OTA_BEGIN has erased it since.
 * Nothing new in RAM: the session's state and the MARK's magic in the store.
 * A MARK from an earlier boot is the image already installed, or one tfw-boot
 * refused, and answers 0 -- so does every image without the stager. */
uint8_t      tfw_stage_install_pending(void);

/* OTA_ACTIVATE's authorization (#137): the held signature written after the
 * MARK (TFW_STAGE_MARK_O_SIG), before tfw_stage_activate writes the MARK's
 * magic. TFW_ERR_BAD_STATE and nothing written unless the session is complete;
 * otherwise the store's status. */
tfw_status_t tfw_stage_authorize(const uint8_t sig[TFW_OTA_AUTH_SIG_LEN], uint8_t mode);

/* OTA_BEGIN's first step on a one-slot board (the 328P and 32U4, issue #52):
 * the running image, the whole application region, copied into the store's
 * PREV, where tfw-boot copies it back from when the round fails
 * (boot/boot_prev.c). src/core/ota_prev.c; on a board whose fallback is a
 * second internal slot, src/core/ota_prev_none.c, which does nothing. The
 * store's first failing status, with PREV part written. */
tfw_status_t tfw_ota_prev_backup(void);

#endif
