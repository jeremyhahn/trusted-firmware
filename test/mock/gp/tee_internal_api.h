/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's stand-in for the TA dev kit's <tee_internal_api.h>: the part of
 * the GlobalPlatform TEE Internal Core API v1.3.1 the TA uses, with the
 * specification's names and numbers, so optee/ta/tfw_ta.c compiles unchanged
 * for the host tests (test/unit/test_optee_ta.c). Only that link puts this
 * directory on the include path. It carries no OP-TEE extension.
 */
#ifndef TFW_MOCK_TEE_INTERNAL_API_H
#define TFW_MOCK_TEE_INTERNAL_API_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t TEE_Result;

/* GP 1.3.1: a memory reference's size is a size_t. */
typedef union {
    struct {
        void *buffer;
        size_t size;
    } memref;
    struct {
        uint32_t a;
        uint32_t b;
    } value;
} TEE_Param;

#define TEE_SUCCESS              0x00000000u
#define TEE_ERROR_GENERIC        0xFFFF0000u
#define TEE_ERROR_BAD_PARAMETERS 0xFFFF0006u
#define TEE_ERROR_NOT_SUPPORTED  0xFFFF000Au

#define TEE_PARAM_TYPE_NONE          0u
#define TEE_PARAM_TYPE_VALUE_INPUT   1u
#define TEE_PARAM_TYPE_VALUE_OUTPUT  2u
#define TEE_PARAM_TYPE_VALUE_INOUT   3u
#define TEE_PARAM_TYPE_MEMREF_INPUT  5u
#define TEE_PARAM_TYPE_MEMREF_OUTPUT 6u
#define TEE_PARAM_TYPE_MEMREF_INOUT  7u

#define TEE_PARAM_TYPES(t0, t1, t2, t3)                                       \
    ((uint32_t)((t0) | ((t1) << 4) | ((t2) << 8) | ((t3) << 12)))
#define TEE_PARAM_TYPE_GET(t, i) (((t) >> ((i) * 4u)) & 0xFu)

void TEE_GenerateRandom(void *randomBuffer, size_t randomBufferLen);

/*
 * The cryptographic operations the sealed-blob codec calls, with the
 * Internal Core API's names, numbers and signatures. test/mock/gp/
 * tee_crypto_mock.c backs them with the host's libcrypto: test-only code,
 * never the TA's.
 */
#define TEE_ERROR_BAD_FORMAT     0xFFFF0005u
#define TEE_ERROR_ITEM_NOT_FOUND 0xFFFF0008u
#define TEE_ERROR_OUT_OF_MEMORY  0xFFFF000Cu
#define TEE_ERROR_SHORT_BUFFER   0xFFFF0010u
#define TEE_ERROR_MAC_INVALID    0xFFFF3071u

#define TEE_HANDLE_NULL 0

#define TEE_ALG_AES_GCM       0x40000810u
#define TEE_TYPE_AES          0xA0000010u
#define TEE_ATTR_SECRET_VALUE 0xC0000000u

#define TEE_MODE_ENCRYPT 0u
#define TEE_MODE_DECRYPT 1u

typedef struct __TEE_OperationHandle *TEE_OperationHandle;
typedef struct __TEE_ObjectHandle *TEE_ObjectHandle;

typedef struct {
    uint32_t attributeID;
    union {
        struct {
            void *buffer;
            size_t length;
        } ref;
        struct {
            uint32_t a;
            uint32_t b;
        } value;
    } content;
} TEE_Attribute;

TEE_Result TEE_AllocateOperation(TEE_OperationHandle *operation, uint32_t algorithm,
                                 uint32_t mode, uint32_t maxKeySize);
void TEE_FreeOperation(TEE_OperationHandle operation);
TEE_Result TEE_AllocateTransientObject(uint32_t objectType, uint32_t maxObjectSize,
                                       TEE_ObjectHandle *object);
void TEE_FreeTransientObject(TEE_ObjectHandle object);
void TEE_InitRefAttribute(TEE_Attribute *attr, uint32_t attributeID, const void *buffer,
                          size_t length);
TEE_Result TEE_PopulateTransientObject(TEE_ObjectHandle object, const TEE_Attribute *attrs,
                                       uint32_t attrCount);
TEE_Result TEE_SetOperationKey(TEE_OperationHandle operation, TEE_ObjectHandle key);
TEE_Result TEE_AEInit(TEE_OperationHandle operation, const void *nonce, size_t nonceLen,
                      uint32_t tagLen, size_t AADLen, size_t payloadLen);
void TEE_AEUpdateAAD(TEE_OperationHandle operation, const void *AADdata, size_t AADdataLen);
TEE_Result TEE_AEEncryptFinal(TEE_OperationHandle operation, const void *srcData,
                              size_t srcLen, void *destData, size_t *destLen, void *tag,
                              size_t *tagLen);
TEE_Result TEE_AEDecryptFinal(TEE_OperationHandle operation, const void *srcData,
                              size_t srcLen, void *destData, size_t *destLen, void *tag,
                              size_t tagLen);

/*
 * The TA-to-TA session calls the key derivation makes, with the Internal
 * Core API's names, numbers and signatures. test/mock/gp/tee_session_mock.c
 * plays the system PTA and records each call: test-only code.
 */
#define TEE_NUM_PARAMS       4u
#define TEE_TIMEOUT_INFINITE 0xFFFFFFFFu

typedef struct {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint8_t clockSeqAndNode[8];
} TEE_UUID;

typedef struct __TEE_TASessionHandle *TEE_TASessionHandle;

TEE_Result TEE_OpenTASession(const TEE_UUID *destination, uint32_t cancellationRequestTimeout,
                             uint32_t paramTypes, TEE_Param params[TEE_NUM_PARAMS],
                             TEE_TASessionHandle *session, uint32_t *returnOrigin);
void TEE_CloseTASession(TEE_TASessionHandle session);
TEE_Result TEE_InvokeTACommand(TEE_TASessionHandle session, uint32_t cancellationRequestTimeout,
                               uint32_t commandID, uint32_t paramTypes,
                               TEE_Param params[TEE_NUM_PARAMS], uint32_t *returnOrigin);

/* The TA's entry points, which the TA defines. */
TEE_Result TA_CreateEntryPoint(void);
void TA_DestroyEntryPoint(void);
TEE_Result TA_OpenSessionEntryPoint(uint32_t paramTypes, TEE_Param params[4],
                                    void **sessionContext);
void TA_CloseSessionEntryPoint(void *sessionContext);
TEE_Result TA_InvokeCommandEntryPoint(void *sessionContext, uint32_t commandID,
                                      uint32_t paramTypes, TEE_Param params[4]);

#endif /* TFW_MOCK_TEE_INTERNAL_API_H */
