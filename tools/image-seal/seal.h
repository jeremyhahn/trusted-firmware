/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build's image sealer for tfw-boot rows (row ota; ADR decision 2): it
 * gives trailer v1's signed span for an application image, and lays out the
 * signed trailer in the two images a device takes.
 *
 *   the stream   what OTA_DATA carries to the stager (src/core/ota_stage.c):
 *                the image, the SUIT envelope where the board checks one,
 *                0xFF to a TFW_OTA_PAGE_MAX multiple, the trailer in its last
 *                TFW_BOOT_TRAILER_LEN bytes
 *   the region   the application region as tfw-boot's serial session writes
 *                it (boot/boot_ota.c), and as tfw_boot_stage_copy leaves it:
 *                the image from the base, the trailer at the region's end
 *
 * NO SIGNING CODE. The key is go-xkms's (ADR decision 10): this tree emits the
 * 48 bytes to sign and assembles what comes back, as tools/suit-emit/ does for
 * the manifest.
 *
 * A host tool built per board, so the board id written into byte 6 is the
 * same TFW_BOARD_ID the board's tfw-boot compares against. The trailer is
 * written and read back by boot/boot_image.c, the device's own code: there is
 * no second encoder of trailer v1 to drift.
 */
#ifndef TFW_TOOLS_IMAGE_SEAL_H
#define TFW_TOOLS_IMAGE_SEAL_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/*
 * The trailer's signed span for `image`, `len` bytes: magic, version 1,
 * TFW_BOARD_ID, `flags`, `version`, `len` and SHA-256 over [0, len).
 *
 * Refuses what the board's tfw-boot would refuse, so no signature is spent on
 * an image that cannot boot:
 *   TFW_ERR_RANGE        len is zero, or reaches the trailer
 *   TFW_ERR_CRYPTO       version is zero
 *   TFW_ERR_UNSUPPORTED  a flag this trailer version does not define, or a
 *                        board with no id
 */
tfw_status_t tfw_seal_tbs(const uint8_t *image, uint32_t len, uint32_t version,
                          uint8_t flags, uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN]);

/*
 * The trailer: `tbs` and go-xkms's 64-byte r || s signature over its SHA-256.
 *
 * The span is checked against the image before anything is written, because
 * a signature over a span for another build would give an image every device
 * refuses: TFW_ERR_CRYPTO when its measurement or length is not this image's,
 * and tfw_seal_tbs's refusals otherwise. The signature itself is not checked
 * here; no public key is in this tree either.
 */
tfw_status_t tfw_seal_trailer(const uint8_t *image, uint32_t len,
                              const uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN],
                              const uint8_t sig[TFW_BOOT_SIG_LEN],
                              uint8_t trailer[TFW_BOOT_TRAILER_LEN]);

/* The stream's length for a `len`-byte image and an `env_len`-byte SUIT
 * envelope, 0 when the stream carries none. */
uint32_t tfw_seal_stream_len(uint32_t len, uint16_t env_len);

/* The stream, tfw_seal_stream_len(len, env_len) bytes into `out`. With an
 * envelope, the bytes after the image are its length as a little-endian u16
 * and then the envelope, in the padding before the trailer: what OTA_ACTIVATE's
 * manifest check reads (src/core/ota_stage.h, tfw_ota_manifest; ruling 121).
 * The trailer's image_len still ends at the image, so tfw-boot measures the
 * same bytes either way. */
void tfw_seal_stream(const uint8_t *image, uint32_t len,
                     const uint8_t *env, uint16_t env_len,
                     const uint8_t trailer[TFW_BOOT_TRAILER_LEN], uint8_t *out);

/* The region, TFW_BOOT_APP_SIZE bytes into `out`. `len` is at most
 * TFW_BOOT_MAX_IMAGE_LEN, which tfw_seal_trailer has checked. */
void tfw_seal_region(const uint8_t *image, uint32_t len,
                     const uint8_t trailer[TFW_BOOT_TRAILER_LEN], uint8_t *out);

#endif
