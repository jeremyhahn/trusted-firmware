/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The trusted application for go-enclave's arm-trustzone backend (#253;
 * docs/optee-trustzone/README.md, "The trusted application"). It speaks the
 * GlobalPlatform TEE Internal Core API v1.3.1 as written: the five TA entry
 * points, TEE_Param and TEE_GenerateRandom. It holds no key and keeps no state
 * across sessions, so every session context is NULL.
 *
 * v1 implements GET_INFO and GET_RANDOM. SEAL and UNSEAL (#288) hold a key
 * only for the length of one command: the TA unique key from the system PTA,
 * through HKDF-SHA-256, is wiped before the command returns. Every other
 * command id answers TEE_ERROR_NOT_SUPPORTED; a wrong paramTypes answers
 * TEE_ERROR_BAD_PARAMETERS. The return codes are GP's: the TA defines none of
 * its own.
 */

#include <tee_internal_api.h>

#include "tfw_blob.h"
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
#define TFW_TA_PT_SEAL                                                        \
    TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT, \
                    TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE)
/* UNSEAL's shapes are SEAL's: blob in, associated data in, plaintext out. */
#define TFW_TA_PT_UNSEAL TFW_TA_PT_SEAL

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

/* A memref a command may read or write: a buffer, or an empty size with none. */
static int tfw_ta_memref_ok(const TEE_Param *p)
{
    return p->memref.buffer != NULL || p->memref.size == 0u;
}

/*
 * The associated data of SEAL and UNSEAL must be empty. go-xkms's format has
 * no field for it: §3's external_aad is the TA's UUID. A caller that thought
 * its data was bound is refused, not told it was.
 */
static int tfw_ta_seal_params_ok(const TEE_Param params[4])
{
    return tfw_ta_memref_ok(&params[0]) && tfw_ta_memref_ok(&params[2]) &&
           params[1].memref.size == 0u;
}

/* TEE_UUID to RFC 9562 §4's octets, tee_uuid_to_octets' order. */
static void tfw_ta_uuid_octets(uint8_t octets[TFW_BLOB_UUID_LEN], const TEE_UUID *uuid)
{
    size_t i;

    octets[0] = (uint8_t)(uuid->timeLow >> 24);
    octets[1] = (uint8_t)(uuid->timeLow >> 16);
    octets[2] = (uint8_t)(uuid->timeLow >> 8);
    octets[3] = (uint8_t)uuid->timeLow;
    octets[4] = (uint8_t)(uuid->timeMid >> 8);
    octets[5] = (uint8_t)uuid->timeMid;
    octets[6] = (uint8_t)(uuid->timeHiAndVersion >> 8);
    octets[7] = (uint8_t)uuid->timeHiAndVersion;
    for (i = 0u; i < 8u; i++)
        octets[8u + i] = uuid->clockSeqAndNode[i];
}

/* The TA's own UUID (GP "gpd.ta.appID") and its blob key. The caller wipes the key. */
static TEE_Result tfw_ta_blob_key(uint8_t uuid[TFW_BLOB_UUID_LEN], uint8_t key[TFW_BLOB_KEY_LEN])
{
    TEE_UUID app_id;
    TEE_Result res;

    res = TEE_GetPropertyAsUUID(TEE_PROPSET_CURRENT_TA, "gpd.ta.appID", &app_id);
    if (res != TEE_SUCCESS)
        return res;
    tfw_ta_uuid_octets(uuid, &app_id);
    return tfw_blob_key(uuid, key);
}

static TEE_Result tfw_ta_seal(uint32_t paramTypes, TEE_Param params[4])
{
    uint8_t uuid[TFW_BLOB_UUID_LEN];
    uint8_t key[TFW_BLOB_KEY_LEN];
    uint8_t nonce[TFW_BLOB_NONCE_LEN];
    size_t blob_len;
    TEE_Result res;

    if (paramTypes != TFW_TA_PT_SEAL || !tfw_ta_seal_params_ok(params))
        return TEE_ERROR_BAD_PARAMETERS;
    res = tfw_ta_blob_key(uuid, key);
    if (res != TEE_SUCCESS)
        goto out;
    /* §4: a fresh 96-bit nonce from the TA's generator on every SEAL, never reused. */
    TEE_GenerateRandom(nonce, sizeof(nonce));
    blob_len = params[2].memref.size;
    res = tfw_blob_seal(key, sizeof(key), uuid, sizeof(uuid), nonce, params[0].memref.buffer,
                        params[0].memref.size, params[2].memref.buffer, &blob_len);
    if (res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER)
        params[2].memref.size = blob_len;
out:
    TEE_MemFill(key, 0u, sizeof(key));
    return res;
}

static TEE_Result tfw_ta_unseal(uint32_t paramTypes, TEE_Param params[4])
{
    uint8_t uuid[TFW_BLOB_UUID_LEN];
    uint8_t key[TFW_BLOB_KEY_LEN];
    enum tfw_blob_refusal refusal;
    size_t plaintext_len;
    TEE_Result res;

    if (paramTypes != TFW_TA_PT_UNSEAL || !tfw_ta_seal_params_ok(params) ||
        params[0].memref.buffer == NULL)
        return TEE_ERROR_BAD_PARAMETERS;
    res = tfw_ta_blob_key(uuid, key);
    if (res != TEE_SUCCESS)
        goto out;
    plaintext_len = params[2].memref.size;
    /* TEE_ERROR_MAC_INVALID is a bad tag and TEE_ERROR_BAD_FORMAT every other refusal. */
    res = tfw_blob_unseal(key, sizeof(key), uuid, sizeof(uuid), params[0].memref.buffer,
                          params[0].memref.size, params[2].memref.buffer, &plaintext_len,
                          &refusal);
    if (res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER)
        params[2].memref.size = plaintext_len;
out:
    TEE_MemFill(key, 0u, sizeof(key));
    return res;
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
    case TFW_TA_CMD_SEAL:
        return tfw_ta_seal(paramTypes, params);
    case TFW_TA_CMD_UNSEAL:
        return tfw_ta_unseal(paramTypes, params);
    default:
        return TEE_ERROR_NOT_SUPPORTED;
    }
}
