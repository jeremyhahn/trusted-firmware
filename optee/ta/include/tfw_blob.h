/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The sealed-blob codec inside the TA (optee/ta/tfw_blob.c): go-xkms's
 * docs/optee-sealed-blob/README.md §2 to §5. TA-side only: it speaks the
 * GlobalPlatform TEE Internal Core API, so the client never includes it.
 */
#ifndef TFW_BLOB_H
#define TFW_BLOB_H

#include <stddef.h>
#include <stdint.h>

#include <tee_internal_api.h>

#define TFW_BLOB_KEY_LEN   32u /* A256GCM, RFC 9053 §4.1 */
#define TFW_BLOB_NONCE_LEN 12u /* the IV, §2 and §4 */
#define TFW_BLOB_TAG_LEN   16u /* the GCM tag, last in the ciphertext */

/* §2: the protected header with its bstr head, a2 01 03 3a 00 01 00 00 01. */
#define TFW_BLOB_PROTECTED_LEN 10u
/* §2: the 27 bytes before the ciphertext's bstr head. */
#define TFW_BLOB_OVERHEAD 27u

/*
 * Which of §5's rules refused a blob, in §5's order, go-xkms's codec errors
 * by name. A refusal returns no plaintext.
 */
enum tfw_blob_refusal {
    TFW_BLOB_ACCEPTED = 0,
    TFW_BLOB_KEY_LENGTH,     /* ErrKeyLength: TEE_ERROR_BAD_PARAMETERS */
    TFW_BLOB_TRUNCATED,      /* ErrTruncated: TEE_ERROR_BAD_FORMAT */
    TFW_BLOB_MALFORMED,      /* ErrMalformed: TEE_ERROR_BAD_FORMAT */
    TFW_BLOB_VERSION,        /* ErrVersion: TEE_ERROR_BAD_FORMAT */
    TFW_BLOB_ALGORITHM,      /* ErrAlgorithm: TEE_ERROR_BAD_FORMAT */
    TFW_BLOB_HEADER,         /* ErrHeader: TEE_ERROR_BAD_FORMAT */
    TFW_BLOB_AUTHENTICATION  /* ErrAuthentication: TEE_ERROR_MAC_INVALID */
};

/*
 * Seals plaintext under the 32-byte key with the given nonce, external_aad
 * going into §3's Enc_structure. *blob_len is the buffer's size in and the
 * blob's out; TEE_ERROR_SHORT_BUFFER returns the size needed. The nonce is
 * the caller's: §4 makes it a fresh TEE_GenerateRandom draw every SEAL.
 */
TEE_Result tfw_blob_seal(const uint8_t *key, size_t key_len, const uint8_t *external_aad,
                         size_t external_aad_len, const uint8_t nonce[TFW_BLOB_NONCE_LEN],
                         const uint8_t *plaintext, size_t plaintext_len, uint8_t *blob,
                         size_t *blob_len);

/*
 * Checks blob against §5 in order and opens it. *plaintext_len is the
 * buffer's size in and the plaintext's out; TEE_ERROR_SHORT_BUFFER returns
 * the size needed. *refusal names the rule that refused the blob.
 */
TEE_Result tfw_blob_unseal(const uint8_t *key, size_t key_len, const uint8_t *external_aad,
                           size_t external_aad_len, const uint8_t *blob, size_t blob_len,
                           uint8_t *plaintext, size_t *plaintext_len,
                           enum tfw_blob_refusal *refusal);

/*
 * Derive key_len bytes unique to this TA from OP-TEE's system PTA, binding
 * extra_data (tfw_derive.c). Refuses an extra_data over 1024 bytes or a
 * key_len outside 16 to 32 with TEE_ERROR_BAD_PARAMETERS before any session
 * opens; otherwise returns the PTA's own result, the key zeroed on failure.
 */
TEE_Result tfw_derive_ta_unique_key(const uint8_t *extra_data, size_t extra_data_len,
                                    uint8_t *key, size_t key_len);

#endif /* TFW_BLOB_H */
