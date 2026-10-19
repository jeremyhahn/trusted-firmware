/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ES256 gate of the ESP32 layer 0: trailer v1 and the SUIT manifest after
 * an image ESP-IDF has already verified (docs/multi-arch/esp32-p4-design.md
 * section 2.2). Portable C over the boot HAL's flash and NVM reads, so the host tests
 * run it against the mock HAL; boot_l0.c calls it with what esp_image_verify
 * returned.
 *
 * A separate evaluation rather than boot/boot_policy.c's tfw_boot_evaluate_at:
 * that one reads the trailer at the fixed TFW_BOOT_TRAILER_ADDR and measures
 * the image itself, where on ESP32 the trailer follows the image and ESP-IDF
 * has measured it. Both call the same primitives, and boot_policy.c is
 * unchanged for AVR.
 */
#ifndef TFW_BOOT_L0_GATE_H
#define TFW_BOOT_L0_GATE_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/*
 * The image at flash offset `base`, in a slot of `slot_size` bytes. `esp_len`
 * is the image's length up to and including ESP-IDF's appended SHA-256, and
 * `image_digest` is that SHA-256 -- esp_image_verify's image_digest, the
 * measurement. The trailer is read at base + esp_len.
 *
 * In order: the trailer parsed (TFW_ERR_UNSUPPORTED, TFW_ERR_CRYPTO or
 * TFW_ERR_RANGE from tfw_boot_trailer_parse), bound to TFW_BOARD_ID
 * (TFW_ERR_UNSUPPORTED), its version against `floor`
 * (TFW_ERR_PROTECTED_REGION), its measurement and length against the image
 * (TFW_ERR_CRYPTO), the image key against the pin in the platform block
 * (TFW_ERR_NOT_PROVISIONED; a block that does not parse, or cannot be read,
 * gives tfw_boot_platform_parse's or the NVM read's status), then the ES256
 * signature over the signed span (TFW_ERR_CRYPTO), then the SUIT envelope after
 * the trailer through src/core/suit.c's tfw_suit_gate, held against the image
 * and the trailer: a length of 0, past TFW_SUIT_ENVELOPE_MAX or past the slot
 * (erased flash among them) is TFW_ERR_RANGE, and the rest is tfw_suit_gate's
 * -- signature or digest TFW_ERR_CRYPTO, vendor, class or board
 * TFW_ERR_UNSUPPORTED, size TFW_ERR_RANGE, a sequence that is not the
 * trailer's version TFW_ERR_PROTECTED_REGION. A trailer that does not fit the
 * slot is TFW_ERR_RANGE before anything is read. TFW_OK fills *out; a refusal
 * may have filled it too, and the caller boots nothing on one.
 */
tfw_status_t tfw_l0_gate(uint32_t base, uint32_t slot_size, uint32_t esp_len,
                         const uint8_t image_digest[TFW_BOOT_MEASURE_LEN],
                         uint32_t floor, tfw_boot_trailer_t *out);

/*
 * OTA_ACTIVATE's authorization for the image whose trailer tfw_l0_gate just
 * passed: `rec` is the TFW_BOOT_AUTH_LEN-byte record from the tfw_auth
 * partition (include/tfw/boot.h). TFW_OK when its signature verifies with the
 * image key over the Sig_structure for tr's measurement and version and the
 * record's mode; TFW_ERR_AUTH_REQUIRED for no record (the magic absent), a
 * mode past 1, or a signature that does not verify.
 */
tfw_status_t tfw_l0_authorized(const tfw_boot_trailer_t *tr, const uint8_t rec[TFW_BOOT_AUTH_LEN]);

/* The record from the tfw_auth partition, in the bootloader
 * (boot_hal_esp32.c): TFW_ERR_AUTH_REQUIRED with no such partition. */
tfw_status_t tfw_l0_auth_read(uint8_t rec[TFW_BOOT_AUTH_LEN]);

#endif /* TFW_BOOT_L0_GATE_H */
