/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's stand-in for libteec's <tee_client_api.h>: the part of the
 * GlobalPlatform TEE Client API v1.0 tfw-ta-check uses, with the
 * specification's names and numbers, so optee/check/check.c compiles
 * unchanged for the host tests. The functions are test/mock/gp_bridge.c's,
 * which hands each call to the TA's entry points in the same process. The
 * context and session contents are the specification's "implementation
 * defined" part, and are the bridge's.
 */
#ifndef TFW_MOCK_TEE_CLIENT_API_H
#define TFW_MOCK_TEE_CLIENT_API_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t TEEC_Result;

typedef struct {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint8_t clockSeqAndNode[8];
} TEEC_UUID;

typedef struct {
    int open;
} TEEC_Context;

typedef struct {
    TEEC_Context *ctx;
    void *ta;
} TEEC_Session;

typedef struct {
    void *buffer;
    size_t size;
} TEEC_TempMemoryReference;

typedef struct {
    uint32_t a;
    uint32_t b;
} TEEC_Value;

typedef union {
    TEEC_TempMemoryReference tmpref;
    TEEC_Value value;
} TEEC_Parameter;

typedef struct {
    uint32_t started;
    uint32_t paramTypes;
    TEEC_Parameter params[4];
} TEEC_Operation;

#define TEEC_SUCCESS              0x00000000u
#define TEEC_ERROR_GENERIC        0xFFFF0000u
#define TEEC_ERROR_BAD_PARAMETERS 0xFFFF0006u
#define TEEC_ERROR_ITEM_NOT_FOUND 0xFFFF0008u
#define TEEC_ERROR_NOT_SUPPORTED  0xFFFF000Au

#define TEEC_ORIGIN_API         0x00000001u
#define TEEC_ORIGIN_COMMS       0x00000002u
#define TEEC_ORIGIN_TEE         0x00000003u
#define TEEC_ORIGIN_TRUSTED_APP 0x00000004u

#define TEEC_LOGIN_PUBLIC 0x00000000u

#define TEEC_NONE               0x00000000u
#define TEEC_VALUE_INPUT        0x00000001u
#define TEEC_VALUE_OUTPUT       0x00000002u
#define TEEC_VALUE_INOUT        0x00000003u
#define TEEC_MEMREF_TEMP_INPUT  0x00000005u
#define TEEC_MEMREF_TEMP_OUTPUT 0x00000006u
#define TEEC_MEMREF_TEMP_INOUT  0x00000007u

#define TEEC_PARAM_TYPES(p0, p1, p2, p3)                                      \
    ((uint32_t)((p0) | ((p1) << 4) | ((p2) << 8) | ((p3) << 12)))

TEEC_Result TEEC_InitializeContext(const char *name, TEEC_Context *context);
void TEEC_FinalizeContext(TEEC_Context *context);
TEEC_Result TEEC_OpenSession(TEEC_Context *context, TEEC_Session *session,
                             const TEEC_UUID *destination, uint32_t connectionMethod,
                             const void *connectionData, TEEC_Operation *operation,
                             uint32_t *returnOrigin);
void TEEC_CloseSession(TEEC_Session *session);
TEEC_Result TEEC_InvokeCommand(TEEC_Session *session, uint32_t commandID,
                               TEEC_Operation *operation, uint32_t *returnOrigin);

#endif /* TFW_MOCK_TEE_CLIENT_API_H */
