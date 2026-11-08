/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The trusted application's interface, shared by the TA and tfw-ta-check
 * (docs/optee-trustzone/README.md, "The trusted application"): its UUID and
 * its command ids, as literals. Ids extend and never renumber; an id the TA
 * does not know answers TEE_ERROR_NOT_SUPPORTED, so an older TA reads a newer
 * command as unrecognised. optee/testdata/vectors.h pins every value here.
 */
#ifndef TFW_TA_H
#define TFW_TA_H

/*
 * 23751b49-3a76-4e49-b63c-e5853742fa1f, RFC 9562 version 4, as a TEE_UUID or
 * TEEC_UUID initialiser: timeLow, timeMid, timeHiAndVersion, clockSeqAndNode.
 * It never changes; an incompatible TA takes a new one.
 */
#define TFW_TA_UUID                                                           \
    { 0x23751b49u, 0x3a76u, 0x4e49u,                                          \
      { 0xb6u, 0x3cu, 0xe5u, 0x85u, 0x37u, 0x42u, 0xfau, 0x1fu } }

/*
 * The command ids. 0x00000000 is reserved and never assigned. Any id not
 * below answers TEE_ERROR_NOT_SUPPORTED.
 */
#define TFW_TA_CMD_GET_INFO   0x00000001u /* param 0 VALUE_OUTPUT: a version, b flags */
#define TFW_TA_CMD_GET_RANDOM 0x00000002u /* param 0 MEMREF_OUTPUT: 1 to 4096 bytes */
/* 0: plaintext in; 1: associated data in, empty; 2: sealed blob out (#288) */
#define TFW_TA_CMD_SEAL       0x00000003u
/* 0: sealed blob in; 1: associated data in, empty; 2: plaintext out (#288) */
#define TFW_TA_CMD_UNSEAL     0x00000004u

/* GET_INFO's answer: the interface version in value.a, flags (reserved) in value.b. */
#define TFW_TA_INTERFACE_VERSION 1u
#define TFW_TA_INFO_FLAGS        0u

/* GET_RANDOM's bounds on the output buffer, in bytes. */
#define TFW_TA_RANDOM_MIN 1u
#define TFW_TA_RANDOM_MAX 4096u

/* SEAL's bound on the plaintext, and so UNSEAL's on the blob, in bytes (#321). */
#define TFW_TA_SEAL_MAX 8192u
/*
 * The blob of a TFW_TA_SEAL_MAX plaintext, from tfw_blob.h's lengths: the 27 bytes
 * before the ciphertext, its 3-byte bstr head (59 xx xx, 256 to 65535 bytes), the
 * plaintext and the tag: 8238. UNSEAL holds this blob and a plaintext scratch, about
 * 16 KiB, in the TA's 32 KiB heap beside the AES-GCM operation.
 */
#define TFW_TA_SEAL_BLOB_MAX (TFW_BLOB_OVERHEAD + 3u + TFW_TA_SEAL_MAX + TFW_BLOB_TAG_LEN)

#endif /* TFW_TA_H */
