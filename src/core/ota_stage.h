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

/* OTA_BEGIN's first step on a one-slot board (the 328P and 32U4, issue #52):
 * the running image, the whole application region, copied into the store's
 * PREV, where tfw-boot copies it back from when the round fails
 * (boot/boot_prev.c). src/core/ota_prev.c; on a board whose fallback is a
 * second internal slot, src/core/ota_prev_none.c, which does nothing. The
 * store's first failing status, with PREV part written. */
tfw_status_t tfw_ota_prev_backup(void);

#endif
