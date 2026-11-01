/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Internal Core API's AES-GCM operation for the host test, backed by
 * the host's OpenSSL libcrypto. Test-only: the TA links OP-TEE's own.
 * It refuses what the codec must never ask for (another algorithm, key
 * size, nonce or tag length) so a wrong call fails the test, not passes.
 */
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include <tee_internal_api.h>

#include "tee_crypto_mock.h"

#define MOCK_KEY_LEN 32u

struct __TEE_ObjectHandle {
    uint8_t key[MOCK_KEY_LEN];
    int populated;
};

struct __TEE_OperationHandle {
    uint32_t mode;
    uint8_t key[MOCK_KEY_LEN];
    int keyed;
    size_t tag_len;
    EVP_CIPHER_CTX *ctx;
};

uint8_t tfw_tee_mock_aad[TFW_TEE_MOCK_AAD_MAX];
size_t tfw_tee_mock_aad_len;
int tfw_tee_mock_live;

void tfw_tee_crypto_mock_reset(void)
{
    memset(tfw_tee_mock_aad, 0, sizeof(tfw_tee_mock_aad));
    tfw_tee_mock_aad_len = 0u;
    tfw_tee_mock_live = 0;
}

TEE_Result TEE_AllocateOperation(TEE_OperationHandle *operation, uint32_t algorithm,
                                 uint32_t mode, uint32_t maxKeySize)
{
    TEE_OperationHandle op;

    if (algorithm != TEE_ALG_AES_GCM || maxKeySize != MOCK_KEY_LEN * 8u ||
        (mode != TEE_MODE_ENCRYPT && mode != TEE_MODE_DECRYPT))
        return TEE_ERROR_NOT_SUPPORTED;
    op = calloc(1u, sizeof(*op));
    if (op == NULL)
        return TEE_ERROR_OUT_OF_MEMORY;
    op->mode = mode;
    *operation = op;
    tfw_tee_mock_live++;
    return TEE_SUCCESS;
}

void TEE_FreeOperation(TEE_OperationHandle operation)
{
    if (operation == TEE_HANDLE_NULL)
        return;
    EVP_CIPHER_CTX_free(operation->ctx);
    free(operation);
    tfw_tee_mock_live--;
}

TEE_Result TEE_AllocateTransientObject(uint32_t objectType, uint32_t maxObjectSize,
                                       TEE_ObjectHandle *object)
{
    TEE_ObjectHandle obj;

    if (objectType != TEE_TYPE_AES || maxObjectSize != MOCK_KEY_LEN * 8u)
        return TEE_ERROR_NOT_SUPPORTED;
    obj = calloc(1u, sizeof(*obj));
    if (obj == NULL)
        return TEE_ERROR_OUT_OF_MEMORY;
    *object = obj;
    tfw_tee_mock_live++;
    return TEE_SUCCESS;
}

void TEE_FreeTransientObject(TEE_ObjectHandle object)
{
    if (object == TEE_HANDLE_NULL)
        return;
    free(object);
    tfw_tee_mock_live--;
}

void TEE_InitRefAttribute(TEE_Attribute *attr, uint32_t attributeID, const void *buffer,
                          size_t length)
{
    attr->attributeID = attributeID;
    attr->content.ref.buffer = (void *)(uintptr_t)buffer;
    attr->content.ref.length = length;
}

TEE_Result TEE_PopulateTransientObject(TEE_ObjectHandle object, const TEE_Attribute *attrs,
                                       uint32_t attrCount)
{
    if (attrCount != 1u || attrs[0].attributeID != TEE_ATTR_SECRET_VALUE ||
        attrs[0].content.ref.length != MOCK_KEY_LEN)
        return TEE_ERROR_BAD_PARAMETERS;
    memcpy(object->key, attrs[0].content.ref.buffer, MOCK_KEY_LEN);
    object->populated = 1;
    return TEE_SUCCESS;
}

TEE_Result TEE_SetOperationKey(TEE_OperationHandle operation, TEE_ObjectHandle key)
{
    if (!key->populated)
        return TEE_ERROR_BAD_PARAMETERS;
    memcpy(operation->key, key->key, MOCK_KEY_LEN);
    operation->keyed = 1;
    return TEE_SUCCESS;
}

TEE_Result TEE_AEInit(TEE_OperationHandle operation, const void *nonce, size_t nonceLen,
                      uint32_t tagLen, size_t AADLen, size_t payloadLen)
{
    int enc = operation->mode == TEE_MODE_ENCRYPT;

    (void)AADLen;
    (void)payloadLen;
    if (!operation->keyed || nonceLen != 12u || tagLen != 128u)
        return TEE_ERROR_NOT_SUPPORTED;
    EVP_CIPHER_CTX_free(operation->ctx);
    operation->ctx = EVP_CIPHER_CTX_new();
    if (operation->ctx == NULL ||
        EVP_CipherInit_ex(operation->ctx, EVP_aes_256_gcm(), NULL, operation->key,
                          (const unsigned char *)nonce, enc) != 1)
        return TEE_ERROR_GENERIC;
    operation->tag_len = tagLen / 8u;
    tfw_tee_mock_aad_len = 0u;
    return TEE_SUCCESS;
}

void TEE_AEUpdateAAD(TEE_OperationHandle operation, const void *AADdata, size_t AADdataLen)
{
    int out;

    if (AADdataLen == 0u)
        return;
    if (tfw_tee_mock_aad_len + AADdataLen <= TFW_TEE_MOCK_AAD_MAX)
        memcpy(tfw_tee_mock_aad + tfw_tee_mock_aad_len, AADdata, AADdataLen);
    tfw_tee_mock_aad_len += AADdataLen;
    if (EVP_CipherUpdate(operation->ctx, NULL, &out, (const unsigned char *)AADdata,
                         (int)AADdataLen) != 1)
        abort();
}

/* The payload through the cipher; the caller finishes it. */
static TEE_Result mock_ae_payload(TEE_OperationHandle operation, const void *srcData,
                                  size_t srcLen, void *destData, size_t *destLen)
{
    int out = 0;

    if (operation->ctx == NULL)
        return TEE_ERROR_BAD_PARAMETERS;
    if (*destLen < srcLen) {
        *destLen = srcLen;
        return TEE_ERROR_SHORT_BUFFER;
    }
    if (srcLen != 0u &&
        EVP_CipherUpdate(operation->ctx, (unsigned char *)destData, &out,
                         (const unsigned char *)srcData, (int)srcLen) != 1)
        return TEE_ERROR_GENERIC;
    *destLen = (size_t)out;
    return TEE_SUCCESS;
}

TEE_Result TEE_AEEncryptFinal(TEE_OperationHandle operation, const void *srcData,
                              size_t srcLen, void *destData, size_t *destLen, void *tag,
                              size_t *tagLen)
{
    unsigned char last[16];
    int out;
    TEE_Result res;

    if (operation->mode != TEE_MODE_ENCRYPT)
        return TEE_ERROR_BAD_PARAMETERS;
    if (*tagLen < operation->tag_len) {
        *tagLen = operation->tag_len;
        return TEE_ERROR_SHORT_BUFFER;
    }
    res = mock_ae_payload(operation, srcData, srcLen, destData, destLen);
    if (res != TEE_SUCCESS)
        return res;
    if (EVP_CipherFinal_ex(operation->ctx, last, &out) != 1 || out != 0 ||
        EVP_CIPHER_CTX_ctrl(operation->ctx, EVP_CTRL_GCM_GET_TAG, (int)operation->tag_len,
                            tag) != 1)
        return TEE_ERROR_GENERIC;
    *tagLen = operation->tag_len;
    return TEE_SUCCESS;
}

TEE_Result TEE_AEDecryptFinal(TEE_OperationHandle operation, const void *srcData,
                              size_t srcLen, void *destData, size_t *destLen, void *tag,
                              size_t tagLen)
{
    unsigned char last[16];
    int out;
    TEE_Result res;

    if (operation->mode != TEE_MODE_DECRYPT || tagLen != operation->tag_len)
        return TEE_ERROR_BAD_PARAMETERS;
    res = mock_ae_payload(operation, srcData, srcLen, destData, destLen);
    if (res != TEE_SUCCESS)
        return res;
    if (EVP_CIPHER_CTX_ctrl(operation->ctx, EVP_CTRL_GCM_SET_TAG, (int)tagLen, tag) != 1)
        return TEE_ERROR_GENERIC;
    if (EVP_CipherFinal_ex(operation->ctx, last, &out) != 1)
        return TEE_ERROR_MAC_INVALID;
    return TEE_SUCCESS;
}
