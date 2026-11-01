/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The sealed blob: go-xkms's docs/optee-sealed-blob/README.md §2 to §5, at
 * go-xkms 02b3da51f. The blob is COSE_Encrypt0_Tagged (RFC 9052 §5.2) in
 * core deterministic encoding (RFC 8949 §4.2.1), sealed with A256GCM
 * (RFC 9053 §4.1) over the Enc_structure of RFC 9052 §5.3. Every primitive
 * is a GlobalPlatform TEE Internal Core API operation: nothing here is AES,
 * GCM or a hash. The key and the external_aad come from the caller.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tee_internal_api.h>

#include "tfw_blob.h"

#define CBOR_BSTR  2u
#define CBOR_ARRAY 4u
#define CBOR_MAP   5u
#define CBOR_TAG   6u
#define CBOR_UINT  0u
#define CBOR_NINT  1u
#define CBOR_SIMPLE 7u

/* Deeper than the blob ever nests; a deeper item is refused as malformed. */
#define CBOR_MAX_DEPTH 16u

/* §2: tag 16, an array of three, the protected header as a bstr. */
static const uint8_t blob_head[2] = {0xd0, 0x83};
static const uint8_t blob_protected[TFW_BLOB_PROTECTED_LEN] = {
    0x49, 0xa2, 0x01, 0x03, 0x3a, 0x00, 0x01, 0x00, 0x00, 0x01};
/* §2: the unprotected header, { 5: bstr(12) }, before its 12 bytes. */
static const uint8_t blob_unprotected[3] = {0xa1, 0x05, 0x4c};
/* §3: Enc_structure's array head and its context, "Encrypt0". */
static const uint8_t enc_context[10] = {0x83, 0x68, 0x45, 0x6e, 0x63,
                                        0x72, 0x79, 0x70, 0x74, 0x30};

/* The version's label, -65537, is CBOR negative integer 65536. */
#define BLOB_VERSION_LABEL 65536u
#define BLOB_VERSION       1u
#define BLOB_ALG_LABEL     1u
#define BLOB_ALG_A256GCM   3u

/* The smallest value each of the 1, 2, 4 and 8-byte arguments may carry. */
static const uint64_t cbor_min_arg[4] = {24u, 0x100u, 0x10000u, 0x100000000u};

static size_t cbor_head_len(uint64_t arg)
{
    if (arg < 24u)
        return 1u;
    if (arg <= 0xFFu)
        return 2u;
    if (arg <= 0xFFFFu)
        return 3u;
    if (arg <= 0xFFFFFFFFu)
        return 5u;
    return 9u;
}

/* Writes the shortest head (RFC 8949 §4.2.1) of major type major. */
static size_t cbor_head_put(uint8_t *out, uint8_t major, uint64_t arg)
{
    size_t len = cbor_head_len(arg);
    size_t i;

    if (len == 1u) {
        out[0] = (uint8_t)(((unsigned)major << 5) | arg);
        return 1u;
    }
    out[0] = (uint8_t)(((unsigned)major << 5) | (len == 2u ? 24u : len == 3u ? 25u
                                       : len == 5u ? 26u : 27u));
    for (i = 1u; i < len; i++)
        out[i] = (uint8_t)(arg >> (8u * (len - 1u - i)));
    return len;
}

/*
 * Reads the head at *off. The buffer ending inside it is truncation; a
 * reserved additional value (28 to 30) or an indefinite length (31) is
 * malformed, neither being core deterministic. A head longer than its
 * argument needs clears *minimal.
 */
static enum tfw_blob_refusal cbor_head_get(const uint8_t *buf, size_t len, size_t *off,
                                           uint8_t *major, uint64_t *arg, int *minimal)
{
    uint8_t info;
    size_t n;
    size_t i;

    if (*off >= len)
        return TFW_BLOB_TRUNCATED;
    *major = (uint8_t)(buf[*off] >> 5);
    info = (uint8_t)(buf[*off] & 0x1Fu);
    (*off)++;
    if (info < 24u) {
        *arg = info;
        return TFW_BLOB_ACCEPTED;
    }
    if (info > 27u)
        return TFW_BLOB_MALFORMED;
    n = (size_t)1u << (info - 24u);
    if (len - *off < n)
        return TFW_BLOB_TRUNCATED;
    *arg = 0u;
    for (i = 0u; i < n; i++)
        *arg = (*arg << 8) | buf[(*off)++];
    if (*major != CBOR_SIMPLE && *arg < cbor_min_arg[info - 24u])
        *minimal = 0;
    return TFW_BLOB_ACCEPTED;
}

/* Steps *off over one complete item, nested items included. */
static enum tfw_blob_refusal cbor_skip(const uint8_t *buf, size_t len, size_t *off,
                                       int *minimal, unsigned depth)
{
    enum tfw_blob_refusal r;
    uint8_t major;
    uint64_t arg;
    uint64_t i;

    if (depth > CBOR_MAX_DEPTH)
        return TFW_BLOB_MALFORMED;
    r = cbor_head_get(buf, len, off, &major, &arg, minimal);
    if (r != TFW_BLOB_ACCEPTED)
        return r;
    switch (major) {
    case CBOR_BSTR:
    case 3u:
        if (arg > len - *off)
            return TFW_BLOB_TRUNCATED;
        *off += (size_t)arg;
        return TFW_BLOB_ACCEPTED;
    case CBOR_ARRAY:
    case CBOR_MAP:
        /* Every item is at least a byte: more items than bytes cannot fit. */
        if (arg > len - *off)
            return TFW_BLOB_TRUNCATED;
        if (major == CBOR_MAP)
            arg *= 2u;
        for (i = 0u; i < arg; i++) {
            r = cbor_skip(buf, len, off, minimal, depth + 1u);
            if (r != TFW_BLOB_ACCEPTED)
                return r;
        }
        return TFW_BLOB_ACCEPTED;
    case CBOR_TAG:
        return cbor_skip(buf, len, off, minimal, depth + 1u);
    default:
        return TFW_BLOB_ACCEPTED;
    }
}

/* Reads the head at *off and says whether it is of major type major. */
static int cbor_head_is(const uint8_t *buf, size_t len, size_t *off, uint8_t major,
                        uint64_t *arg, int *minimal)
{
    uint8_t got;

    return cbor_head_get(buf, len, off, &got, arg, minimal) == TFW_BLOB_ACCEPTED &&
           got == major;
}

/*
 * Says whether the map in buf holds the integer label (label_major,
 * label_arg) with the unsigned value want. Anything that is not such a
 * map holds no label.
 */
static int map_holds(const uint8_t *buf, size_t len, uint8_t label_major,
                     uint64_t label_arg, uint64_t want)
{
    size_t off = 0u;
    size_t at;
    int minimal = 1;
    uint8_t major;
    uint64_t n;
    uint64_t arg;
    uint64_t i;

    if (!cbor_head_is(buf, len, &off, CBOR_MAP, &n, &minimal) || n > len)
        return 0;
    for (i = 0u; i < n; i++) {
        at = off;
        if (cbor_head_get(buf, len, &off, &major, &arg, &minimal) != TFW_BLOB_ACCEPTED)
            return 0;
        if (major == label_major && arg == label_arg)
            return cbor_head_is(buf, len, &off, CBOR_UINT, &arg, &minimal) && arg == want;
        off = at;
        if (cbor_skip(buf, len, &off, &minimal, 0u) != TFW_BLOB_ACCEPTED ||
            cbor_skip(buf, len, &off, &minimal, 0u) != TFW_BLOB_ACCEPTED)
            return 0;
    }
    return 0;
}

/* The blob's three fields, where §5's checks found them. */
struct blob_view {
    const uint8_t *protected_bytes;
    size_t protected_len;
    const uint8_t *unprotected;
    size_t unprotected_len;
    const uint8_t *ciphertext;
    size_t ciphertext_len;
};

/* §5's rules before authentication, checked in §5's order. */
static enum tfw_blob_refusal blob_parse(const uint8_t *blob, size_t len, struct blob_view *v)
{
    enum tfw_blob_refusal r;
    size_t off = 0u;
    size_t end;
    int minimal = 1;
    int unused = 1;
    int shaped;
    uint64_t arg;

    /* A complete CBOR item. Its walk also finds every non-minimal head. */
    r = cbor_skip(blob, len, &off, &minimal, 0u);
    if (r != TFW_BLOB_ACCEPTED)
        return r;
    end = off;

    /* The walk proved every head and length lies inside blob[0, end). */
    off = 0u;
    shaped = cbor_head_is(blob, end, &off, CBOR_TAG, &arg, &unused) && arg == 16u &&
             cbor_head_is(blob, end, &off, CBOR_ARRAY, &arg, &unused) && arg == 3u &&
             cbor_head_is(blob, end, &off, CBOR_BSTR, &arg, &unused);
    if (shaped) {
        v->protected_bytes = blob + off;
        v->protected_len = (size_t)arg;
        off += (size_t)arg;
        v->unprotected = blob + off;
        shaped = off < end && (blob[off] >> 5) == CBOR_MAP &&
                 cbor_skip(blob, end, &off, &unused, 0u) == TFW_BLOB_ACCEPTED;
    }
    if (shaped) {
        v->unprotected_len = (size_t)(blob + off - v->unprotected);
        shaped = cbor_head_is(blob, end, &off, CBOR_BSTR, &arg, &unused);
    }
    if (shaped) {
        v->ciphertext = blob + off;
        v->ciphertext_len = (size_t)arg;
        if (v->ciphertext_len < TFW_BLOB_TAG_LEN)
            return TFW_BLOB_TRUNCATED;
    }
    if (!shaped || end != len)
        return TFW_BLOB_MALFORMED;

    if (!map_holds(v->protected_bytes, v->protected_len, CBOR_NINT, BLOB_VERSION_LABEL,
                   BLOB_VERSION))
        return TFW_BLOB_VERSION;
    if (!map_holds(v->protected_bytes, v->protected_len, CBOR_UINT, BLOB_ALG_LABEL,
                   BLOB_ALG_A256GCM))
        return TFW_BLOB_ALGORITHM;
    if (v->protected_len != TFW_BLOB_PROTECTED_LEN - 1u ||
        memcmp(v->protected_bytes, blob_protected + 1, TFW_BLOB_PROTECTED_LEN - 1u) != 0 ||
        v->unprotected_len != sizeof(blob_unprotected) + TFW_BLOB_NONCE_LEN ||
        memcmp(v->unprotected, blob_unprotected, sizeof(blob_unprotected)) != 0)
        return TFW_BLOB_HEADER;
    if (!minimal)
        return TFW_BLOB_MALFORMED;
    return TFW_BLOB_ACCEPTED;
}

/*
 * Starts an A256GCM operation keyed with key, its AAD the Enc_structure
 * of §3 around external_aad. On success *op is the caller's to free.
 */
static TEE_Result gcm_begin(TEE_OperationHandle *op, uint32_t mode, const uint8_t *key,
                            const uint8_t *nonce, const uint8_t *external_aad,
                            size_t external_aad_len, size_t payload_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    TEE_Attribute attr;
    TEE_Result res;
    uint8_t head[9];
    size_t head_len;

    *op = TEE_HANDLE_NULL;
    res = TEE_AllocateOperation(op, TEE_ALG_AES_GCM, mode, TFW_BLOB_KEY_LEN * 8u);
    if (res != TEE_SUCCESS)
        return res;
    res = TEE_AllocateTransientObject(TEE_TYPE_AES, TFW_BLOB_KEY_LEN * 8u, &obj);
    if (res == TEE_SUCCESS) {
        TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, key, TFW_BLOB_KEY_LEN);
        res = TEE_PopulateTransientObject(obj, &attr, 1u);
    }
    if (res == TEE_SUCCESS)
        res = TEE_SetOperationKey(*op, obj);
    if (obj != TEE_HANDLE_NULL)
        TEE_FreeTransientObject(obj);
    head_len = cbor_head_put(head, CBOR_BSTR, external_aad_len);
    if (res == TEE_SUCCESS)
        res = TEE_AEInit(*op, nonce, TFW_BLOB_NONCE_LEN, TFW_BLOB_TAG_LEN * 8u,
                         sizeof(enc_context) + sizeof(blob_protected) + head_len +
                             external_aad_len,
                         payload_len);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(*op);
        *op = TEE_HANDLE_NULL;
        return res;
    }
    TEE_AEUpdateAAD(*op, enc_context, sizeof(enc_context));
    TEE_AEUpdateAAD(*op, blob_protected, sizeof(blob_protected));
    TEE_AEUpdateAAD(*op, head, head_len);
    TEE_AEUpdateAAD(*op, external_aad, external_aad_len);
    return TEE_SUCCESS;
}

TEE_Result tfw_blob_seal(const uint8_t *key, size_t key_len, const uint8_t *external_aad,
                         size_t external_aad_len, const uint8_t nonce[TFW_BLOB_NONCE_LEN],
                         const uint8_t *plaintext, size_t plaintext_len, uint8_t *blob,
                         size_t *blob_len)
{
    TEE_OperationHandle op;
    TEE_Result res;
    size_t head_len;
    size_t need;
    size_t ct_len;
    size_t tag_len = TFW_BLOB_TAG_LEN;
    size_t off = 0u;

    /* The only bound on the plaintext is that the blob's size fits a size_t. */
    if (key_len != TFW_BLOB_KEY_LEN ||
        plaintext_len > SIZE_MAX - (TFW_BLOB_OVERHEAD + 9u + TFW_BLOB_TAG_LEN))
        return TEE_ERROR_BAD_PARAMETERS;
    head_len = cbor_head_len(plaintext_len + TFW_BLOB_TAG_LEN);
    need = TFW_BLOB_OVERHEAD + head_len + plaintext_len + TFW_BLOB_TAG_LEN;
    if (*blob_len < need) {
        *blob_len = need;
        return TEE_ERROR_SHORT_BUFFER;
    }

    memcpy(blob + off, blob_head, sizeof(blob_head));
    off += sizeof(blob_head);
    memcpy(blob + off, blob_protected, sizeof(blob_protected));
    off += sizeof(blob_protected);
    memcpy(blob + off, blob_unprotected, sizeof(blob_unprotected));
    off += sizeof(blob_unprotected);
    memcpy(blob + off, nonce, TFW_BLOB_NONCE_LEN);
    off += TFW_BLOB_NONCE_LEN;
    off += cbor_head_put(blob + off, CBOR_BSTR, plaintext_len + TFW_BLOB_TAG_LEN);

    res = gcm_begin(&op, TEE_MODE_ENCRYPT, key, nonce, external_aad, external_aad_len,
                    plaintext_len);
    if (res != TEE_SUCCESS)
        return res;
    ct_len = plaintext_len;
    res = TEE_AEEncryptFinal(op, plaintext, plaintext_len, blob + off, &ct_len,
                             blob + off + plaintext_len, &tag_len);
    TEE_FreeOperation(op);
    if (res != TEE_SUCCESS)
        return res;
    if (ct_len != plaintext_len || tag_len != TFW_BLOB_TAG_LEN)
        return TEE_ERROR_GENERIC;
    *blob_len = need;
    return TEE_SUCCESS;
}

TEE_Result tfw_blob_unseal(const uint8_t *key, size_t key_len, const uint8_t *external_aad,
                           size_t external_aad_len, const uint8_t *blob, size_t blob_len,
                           uint8_t *plaintext, size_t *plaintext_len,
                           enum tfw_blob_refusal *refusal)
{
    struct blob_view v;
    TEE_OperationHandle op;
    TEE_Result res;
    uint8_t tag[TFW_BLOB_TAG_LEN];
    size_t pt_len;
    size_t out_len;

    if (key_len != TFW_BLOB_KEY_LEN) {
        *refusal = TFW_BLOB_KEY_LENGTH;
        return TEE_ERROR_BAD_PARAMETERS;
    }
    *refusal = blob_parse(blob, blob_len, &v);
    if (*refusal != TFW_BLOB_ACCEPTED)
        return TEE_ERROR_BAD_FORMAT;

    pt_len = v.ciphertext_len - TFW_BLOB_TAG_LEN;
    if (*plaintext_len < pt_len) {
        *plaintext_len = pt_len;
        return TEE_ERROR_SHORT_BUFFER;
    }
    res = gcm_begin(&op, TEE_MODE_DECRYPT, key, v.unprotected + sizeof(blob_unprotected),
                    external_aad, external_aad_len, pt_len);
    if (res != TEE_SUCCESS)
        return res;
    /* The Internal Core API takes the tag through a non-const pointer. */
    memcpy(tag, v.ciphertext + pt_len, TFW_BLOB_TAG_LEN);
    out_len = *plaintext_len;
    res = TEE_AEDecryptFinal(op, v.ciphertext, pt_len, plaintext, &out_len, tag,
                             TFW_BLOB_TAG_LEN);
    TEE_FreeOperation(op);
    if (res != TEE_SUCCESS) {
        /* No plaintext leaves on a refusal, not even a partial one. */
        if (*plaintext_len != 0u)
            memset(plaintext, 0, *plaintext_len);
        if (res == TEE_ERROR_MAC_INVALID)
            *refusal = TFW_BLOB_AUTHENTICATION;
        return res;
    }
    *plaintext_len = out_len;
    return TEE_SUCCESS;
}
