/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TA's unique key from stock OP-TEE: one PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY
 * call on the system PTA. No core patch; the caller gives extra_data and the
 * key length.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tee_internal_api.h>

#include "tfw_blob.h"

/*
 * PTA_SYSTEM_UUID, PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY, TA_DERIVED_EXTRA_DATA_MAX_SIZE,
 * TA_DERIVED_KEY_MIN_SIZE and TA_DERIVED_KEY_MAX_SIZE, under our names, from
 * lib/libutee/include/pta_system.h, OP-TEE optee_os tag 4.11.0
 * (BSD-2-Clause). There, params[0] is a MEMREF_INPUT holding the extra data,
 * params[1] a MEMREF_OUTPUT receiving the key, params[2] and [3] NONE.
 * Defined here because the host test build has no TA dev kit.
 */
static const TEE_UUID tfw_pta_system_uuid = {
    0x3a2f8978u, 0x5dc0u, 0x11e8u, { 0x9cu, 0x2du, 0xfau, 0x7au, 0xe0u, 0x1bu, 0xbeu, 0xbcu }
};
#define TFW_PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY 1u
#define TFW_DERIVE_EXTRA_DATA_MAX           1024u
#define TFW_DERIVE_KEY_MIN                  16u
#define TFW_DERIVE_KEY_MAX                  32u

TEE_Result tfw_derive_ta_unique_key(const uint8_t *extra_data, size_t extra_data_len,
                                    uint8_t *key, size_t key_len)
{
    TEE_TASessionHandle session = TEE_HANDLE_NULL;
    TEE_Param params[TEE_NUM_PARAMS];
    uint32_t origin = 0u;
    TEE_Result res;

    if (key == NULL || key_len < TFW_DERIVE_KEY_MIN || key_len > TFW_DERIVE_KEY_MAX ||
        extra_data_len > TFW_DERIVE_EXTRA_DATA_MAX ||
        (extra_data == NULL && extra_data_len != 0u))
        return TEE_ERROR_BAD_PARAMETERS;

    memset(params, 0, sizeof(params));
    res = TEE_OpenTASession(&tfw_pta_system_uuid, TEE_TIMEOUT_INFINITE,
                            TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
                                            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE),
                            params, &session, &origin);
    if (res != TEE_SUCCESS) {
        memset(key, 0, key_len);
        return res;
    }

    memset(params, 0, sizeof(params));
    params[0].memref.buffer = (void *)(uintptr_t)extra_data;
    params[0].memref.size = extra_data_len;
    params[1].memref.buffer = key;
    params[1].memref.size = key_len;
    res = TEE_InvokeTACommand(session, TEE_TIMEOUT_INFINITE, TFW_PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY,
                              TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                              TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
                                              TEE_PARAM_TYPE_NONE),
                              params, &origin);
    TEE_CloseTASession(session);
    if (res != TEE_SUCCESS)
        memset(key, 0, key_len);
    return res;
}
