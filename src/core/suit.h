/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT manifest reader and the activation gate it feeds (issue #78).
 *
 * The profile is docs/ota/adr-ota-image-format-and-trust.md decision 3: a
 * subset of draft-ietf-suit-manifest, SHA-256 and ES256 only. On AVR the
 * reader runs in the application, never in tfw-boot: tfw-boot verifies the
 * trailer every boot and stays the authority; this is a gate before activation
 * that adds the class-id binding and an independently encoded copy of the
 * trailer's facts. ESP32 layer 0 runs it every boot, after the trailer
 * (boot/hal/esp32/l0_gate.c; docs/multi-arch/esp32-p4-design.md section 2.2).
 *
 * Which boards link it is decided by the object list, never by a conditional
 * here: a board whose profile.mk sets OTA_MANIFEST := 1 on the element tier gets
 * src/core/suit.c (mk/firmware.mk), the ESP32 bootloader lists it
 * (boot/hal/esp32/bootloader_components/main/CMakeLists.txt), and no other
 * board carries a byte of it.
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
/* The largest envelope with an integrated payload, payload included: the
 * policy set's buffer. Slice 2 of docs/hsm-self-defence/ fixes it against
 * the M7's memory map. */
#define TFW_SUIT_INTEGRATED_MAX 1024u
/* The longest fragment-only URI naming an integrated payload. */
#define TFW_SUIT_URI_MAX      32u
/* An ES256 public key, x || y. */
#define TFW_SUIT_KEY_LEN      64u

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

/* The same reader for an envelope whose payload is integrated
 * (draft-ietf-suit-manifest-37 §5.5, §7.5), verified under `key` and never
 * the board's image key: the policy set and the script of
 * docs/hsm-self-defence/adr-response-policy-format.md D1, D3. In
 * src/core/suit_integrated.c, which no image reader links. Accepted, in this
 * order and nothing else:
 *
 *   107({ 2: the authentication wrapper above,
 *         3: bstr .cbor { 1: 1, 2: uint32, 3: the common above,
 *                         20: bstr .cbor [ 20, { 21: tstr },   ; install: uri
 *                                          21, uint,           ; fetch
 *                                          3, uint ] },        ; image match
 *         tstr: bstr })                                         ; the payload
 *
 * The URI is fragment-only ("#..."), the payload's envelope key is that URI,
 * and the payload's size and SHA-256 are the manifest's image size and image
 * digest. The checks run in ADR D7's order, the payload's after the
 * signature's; `out` and `*payload` (image_size bytes inside `env`) are
 * written only on TFW_OK, with `*kind`.
 *
 * The class id is held (ADR D2): the vendor must be the board's, and the class
 * must be this board's policy class (TFW_SUIT_POLICY_CLASS_ID) or script
 * class (TFW_SUIT_SCRIPT_CLASS_ID), one the caller names in `expect`. The
 * class says which kind the envelope claims; the payload's top-level type
 * says which it carries (D5 a map, D6 an array), and the two must agree. A
 * board whose class id is all zero has no such class and reads no envelope.
 *
 *   TFW_ERR_RANGE        truncated, a length past what is there or allowed,
 *                        no payload under the manifest's URI, or its size
 *   TFW_ERR_UNSUPPORTED  a member, type, tag or algorithm the profile lacks,
 *                        a tier with no external-key verify, another vendor,
 *                        a class not this board's or not in `expect`, or a
 *                        payload of the other kind than its class claims
 *   TFW_ERR_CRYPTO       the manifest digest, the signature under `key`, or
 *                        the payload's digest does not hold */
#define TFW_SUIT_KIND_POLICY  0x01u
#define TFW_SUIT_KIND_SCRIPT  0x02u
tfw_status_t tfw_suit_read_integrated(const uint8_t *env, uint16_t len,
                                      const uint8_t key[TFW_SUIT_KEY_LEN], uint8_t expect,
                                      tfw_suit_manifest_t *out, const uint8_t **payload,
                                      uint8_t *kind);

/* The manifest digest of an envelope tfw_suit_read_integrated accepted: the
 * SHA-256 of its bstr-wrapped manifest, header included, as its
 * authentication wrapper carries it. The policy floor tells a re-send from
 * another set at the same sequence number by it (ADR D4). */
tfw_status_t tfw_suit_integrated_digest(const uint8_t *env, uint16_t len,
                                        uint8_t digest[TFW_SUIT_DIGEST_LEN]);

/* The gate before activation. Reads `env`, then holds the manifest against the
 * staged image and its trailer:
 *
 *   signature, manifest digest, structure   as tfw_suit_read
 *   vendor id, class id, trailer board id   TFW_ERR_UNSUPPORTED (the binding)
 *   image digest vs staged image, trailer   TFW_ERR_CRYPTO
 *   image size vs staged length, trailer    TFW_ERR_RANGE
 *   sequence vs trailer image_version       TFW_ERR_PROTECTED_REGION
 *   sequence below the platform's floor     TFW_ERR_PROTECTED_REGION
 *
 * Sets p->ota_state to VERIFIED on TFW_OK and to FAILED on any refusal, so
 * tfw_boot_trial_activate refuses a refused image and no boot request is
 * written. Touches nothing else. */
tfw_status_t tfw_suit_gate(tfw_boot_platform_t *p, const tfw_boot_trailer_t *tr,
                           const uint8_t staged_digest[TFW_SUIT_DIGEST_LEN],
                           uint32_t staged_len, const uint8_t *env, uint16_t env_len);

#endif
