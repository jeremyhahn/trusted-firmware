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

/* The TA's entry points, which the TA defines. */
TEE_Result TA_CreateEntryPoint(void);
void TA_DestroyEntryPoint(void);
TEE_Result TA_OpenSessionEntryPoint(uint32_t paramTypes, TEE_Param params[4],
                                    void **sessionContext);
void TA_CloseSessionEntryPoint(void *sessionContext);
TEE_Result TA_InvokeCommandEntryPoint(void *sessionContext, uint32_t commandID,
                                      uint32_t paramTypes, TEE_Param params[4]);

#endif /* TFW_MOCK_TEE_INTERNAL_API_H */
