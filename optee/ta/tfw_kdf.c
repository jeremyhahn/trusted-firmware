/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The sealed blob's key (go-xkms docs/optee-sealed-blob/README.md §1): HKDF-
 * SHA-256 (RFC 5869) over the TA unique key, the system PTA's
 * PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY output. The HMAC is the Internal Core API's
 * TEE_ALG_HMAC_SHA256; no hash is written here.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tee_internal_api.h>

#include "tfw_blob.h"

#define SHA256_LEN 32u

/* §1: the label, "go-xkms optee-sealed-blob v1": the PTA's extra_data and the info's head. */
static const uint8_t blob_label[] = {
    0x67, 0x6f, 0x2d, 0x78, 0x6b, 0x6d, 0x73, 0x20, 0x6f, 0x70, 0x74, 0x65, 0x65, 0x2d,
    0x73, 0x65, 0x61, 0x6c, 0x65, 0x64, 0x2d, 0x62, 0x6c, 0x6f, 0x62, 0x20, 0x76, 0x31,
};

/* The label is 28 bytes and the info 45 (§1); a wrong size fails the build. */
typedef char blob_label_is_28_bytes[(sizeof(blob_label) == 28u) ? 1 : -1];
typedef char blob_info_is_45_bytes[(TFW_BLOB_INFO_LEN == 28u + 1u + TFW_BLOB_UUID_LEN) ? 1 : -1];

/*
 * HMAC-SHA-256 of first ‖ second under the 32-byte key. HKDF's PRK and OKM are
 * both one SHA-256 block, so this is all of RFC 5869 §2 needs.
 */
static TEE_Result hmac_sha256(const uint8_t key[SHA256_LEN], const uint8_t *first,
                              size_t first_len, const uint8_t *second, size_t second_len,
                              uint8_t mac[SHA256_LEN])
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    TEE_Attribute attr;
    size_t mac_len = SHA256_LEN;
    TEE_Result res;

    res = TEE_AllocateTransientObject(TEE_TYPE_HMAC_SHA256, SHA256_LEN * 8u, &obj);
    if (res != TEE_SUCCESS)
        return res;
    res = TEE_AllocateOperation(&op, TEE_ALG_HMAC_SHA256, TEE_MODE_MAC, SHA256_LEN * 8u);
    if (res != TEE_SUCCESS)
        goto out;
    TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, key, SHA256_LEN);
    res = TEE_PopulateTransientObject(obj, &attr, 1u);
    if (res != TEE_SUCCESS)
        goto out;
    res = TEE_SetOperationKey(op, obj);
    if (res != TEE_SUCCESS)
        goto out;
    TEE_MACInit(op, NULL, 0u);
    if (first_len != 0u)
        TEE_MACUpdate(op, first, first_len);
    res = TEE_MACComputeFinal(op, second, second_len, mac, &mac_len);
    if (res == TEE_SUCCESS && mac_len != SHA256_LEN)
        res = TEE_ERROR_GENERIC;
out:
    TEE_FreeOperation(op);
    TEE_FreeTransientObject(obj);
    return res;
}

TEE_Result tfw_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *info,
                           size_t info_len, uint8_t okm[TFW_BLOB_KEY_LEN])
{
    static const uint8_t counter_1 = 0x01u;
    uint8_t salt[SHA256_LEN];
    uint8_t prk[SHA256_LEN];
    TEE_Result res;

    /* §1: no salt is a string of HashLen zero bytes (RFC 5869 §2.2). */
    TEE_MemFill(salt, 0u, sizeof(salt));
    /* Extract: PRK = HMAC(salt, IKM). */
    res = hmac_sha256(salt, NULL, 0u, ikm, ikm_len, prk);
    if (res == TEE_SUCCESS) {
        /* Expand, L = 32 = HashLen: OKM = T(1) = HMAC(PRK, info ‖ 0x01). */
        res = hmac_sha256(prk, info, info_len, &counter_1, 1u, okm);
    }
    TEE_MemFill(prk, 0u, sizeof(prk));
    if (res != TEE_SUCCESS)
        TEE_MemFill(okm, 0u, TFW_BLOB_KEY_LEN);
    return res;
}

TEE_Result tfw_blob_key(const uint8_t uuid[TFW_BLOB_UUID_LEN], uint8_t key[TFW_BLOB_KEY_LEN])
{
    uint8_t ikm[TFW_BLOB_KEY_LEN];
    uint8_t info[TFW_BLOB_INFO_LEN];
    TEE_Result res;

    res = tfw_derive_ta_unique_key(blob_label, sizeof(blob_label), ikm, sizeof(ikm));
    if (res != TEE_SUCCESS) {
        TEE_MemFill(ikm, 0u, sizeof(ikm));
        TEE_MemFill(key, 0u, TFW_BLOB_KEY_LEN);
        return res;
    }
    /* §1: info = label ‖ 0x00 ‖ context, the context the TA's UUID. */
    memcpy(info, blob_label, sizeof(blob_label));
    info[sizeof(blob_label)] = 0x00u;
    memcpy(info + sizeof(blob_label) + 1u, uuid, TFW_BLOB_UUID_LEN);
    res = tfw_hkdf_sha256(ikm, sizeof(ikm), info, sizeof(info), key);
    TEE_MemFill(ikm, 0u, sizeof(ikm));
    return res;
}
