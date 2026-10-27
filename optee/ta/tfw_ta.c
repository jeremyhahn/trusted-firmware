/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The trusted application for go-enclave's arm-trustzone backend (#253;
 * docs/optee-trustzone/README.md, "The trusted application"). It speaks the
 * GlobalPlatform TEE Internal Core API v1.3.1 as written: the five TA entry
 * points, TEE_Param and TEE_GenerateRandom. It holds no key and keeps no state
 * across sessions, so every session context is NULL.
 *
 * v1 implements GET_INFO and GET_RANDOM. Every other command id answers
 * TEE_ERROR_NOT_SUPPORTED; a wrong paramTypes answers TEE_ERROR_BAD_PARAMETERS.
 * The return codes are GP's: the TA defines none of its own.
 */

#include <tee_internal_api.h>

#include "tfw_ta.h"

#define TFW_TA_PT_NONE                                                        \
    TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,                 \
                    TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE)
#define TFW_TA_PT_GET_INFO                                                    \
    TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE,         \
                    TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE)
#define TFW_TA_PT_GET_RANDOM                                                  \
    TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,        \
                    TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE)

TEE_Result TA_CreateEntryPoint(void)
{
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

/* Login is the client's (TEEC_LOGIN_PUBLIC); the open takes no parameter. */
TEE_Result TA_OpenSessionEntryPoint(uint32_t paramTypes, TEE_Param params[4],
                                    void **sessionContext)
{
    (void)params;
    *sessionContext = NULL;
    if (paramTypes != TFW_TA_PT_NONE) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sessionContext)
{
    (void)sessionContext;
}

static TEE_Result tfw_ta_get_info(uint32_t paramTypes, TEE_Param params[4])
{
    if (paramTypes != TFW_TA_PT_GET_INFO) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    params[0].value.a = TFW_TA_INTERFACE_VERSION;
    params[0].value.b = TFW_TA_INFO_FLAGS;
    return TEE_SUCCESS;
}

static TEE_Result tfw_ta_get_random(uint32_t paramTypes, TEE_Param params[4])
{
    if (paramTypes != TFW_TA_PT_GET_RANDOM) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (params[0].memref.size < TFW_TA_RANDOM_MIN ||
        params[0].memref.size > TFW_TA_RANDOM_MAX ||
        params[0].memref.buffer == NULL) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    TEE_GenerateRandom(params[0].memref.buffer, params[0].memref.size);
    return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sessionContext, uint32_t commandID,
                                      uint32_t paramTypes, TEE_Param params[4])
{
    (void)sessionContext;
    switch (commandID) {
    case TFW_TA_CMD_GET_INFO:
        return tfw_ta_get_info(paramTypes, params);
    case TFW_TA_CMD_GET_RANDOM:
        return tfw_ta_get_random(paramTypes, params);
    default:
        return TEE_ERROR_NOT_SUPPORTED;
    }
}
