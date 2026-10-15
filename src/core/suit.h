/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT manifest reader and the activation gate it feeds (issue #78).
 *
 * The profile is docs/ota/adr-ota-image-format-and-trust.md decision 3: a
 * subset of draft-ietf-suit-manifest, SHA-256 and ES256 only. The reader runs
 * in the application, never in tfw-boot: tfw-boot verifies the trailer every
 * boot and stays the authority; this is a gate before activation that adds the
 * class-id binding and an independently encoded copy of the trailer's facts.
 *
 * Which boards link it is decided by the object list (mk/firmware.mk), never
 * by a conditional here: a board whose profile.mk sets OTA_MANIFEST := 1 on the
 * element tier gets src/core/suit.c, and no other board carries a byte of it.
 *
 * Accepted, byte for byte in this order and nothing else:
 *
 *   107({ 2: bstr .cbor [ bstr .cbor [-16, bstr(32)],            ; manifest digest
 *                         bstr .cbor 18([ h'A10126', {} / {4: bstr},
 *                                         nil, bstr(64) ]) ],     ; ES256, detached
 *         3: bstr .cbor { 1: 1, 2: uint32,                        ; version, sequence
 *                         3: bstr .cbor { 2: [ [+ bstr] ],        ; one component
 *                                         4: bstr .cbor [ 20, {   ; override-parameters
 *                                              1: bstr(16),       ; vendor id
 *                                              2: bstr(16),       ; class id
 *                                              3: bstr .cbor [-16, bstr(32)],
 *                                              14: uint32 } ] } } })
 *
 * The manifest digest covers the bstr-wrapped manifest, header included; the
 * signature covers the COSE Sig_structure whose payload is the bstr-wrapped
 * manifest digest. Nothing in the manifest is read until both hold.
 */
#ifndef TFW_CORE_SUIT_H
#define TFW_CORE_SUIT_H

#include <stdint.h>

#include "tfw/boot.h"
#include "tfw/status.h"

#define TFW_SUIT_ID_LEN       16u
#define TFW_SUIT_DIGEST_LEN   32u
/* The largest envelope read. The profile's envelope is about 230 bytes with a
 * key id; anything past this is refused before a byte of it is parsed. */
#define TFW_SUIT_ENVELOPE_MAX 320u

typedef struct {
    uint32_t seq;                              /* the security version */
    uint32_t image_size;
    uint8_t  vendor[TFW_SUIT_ID_LEN];
    uint8_t  class_id[TFW_SUIT_ID_LEN];
    uint8_t  image_digest[TFW_SUIT_DIGEST_LEN];
} tfw_suit_manifest_t;

/* Parse and authenticate an envelope. TFW_OK only when the structure is the
 * profile's exactly, the manifest digest matches and the signature verifies
 * against the board's image key; `out` is written only then.
 *
 *   TFW_ERR_RANGE        truncated, or a length past what is there or allowed
 *   TFW_ERR_UNSUPPORTED  a member, type, tag or algorithm the profile lacks
 *   TFW_ERR_CRYPTO       the manifest digest or the signature does not hold */
tfw_status_t tfw_suit_read(const uint8_t *env, uint16_t len, tfw_suit_manifest_t *out);

/* The gate before activation. Reads `env`, then holds the manifest against the
 * staged image and its trailer:
 *
 *   signature, manifest digest, structure   as tfw_suit_read
 *   vendor id, class id, trailer board id   TFW_ERR_UNSUPPORTED (the binding)
 *   image digest vs staged image, trailer   TFW_ERR_CRYPTO
 *   image size vs staged length, trailer    TFW_ERR_RANGE
 *   sequence vs trailer image_version       TFW_ERR_PROTECTED_REGION
 *
 * Sets p->ota_state to VERIFIED on TFW_OK and to FAILED on any refusal, so
 * tfw_boot_trial_activate refuses a refused image and no boot request is
 * written. Touches nothing else. */
tfw_status_t tfw_suit_gate(tfw_boot_platform_t *p, const tfw_boot_trailer_t *tr,
                           const uint8_t staged_digest[TFW_SUIT_DIGEST_LEN],
                           uint32_t staged_len, const uint8_t *env, uint16_t env_len);

#endif
