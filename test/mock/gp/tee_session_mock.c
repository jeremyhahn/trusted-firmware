/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Internal Core API's TA-to-TA session calls for the host test, playing
 * OP-TEE's system PTA. Test-only: the TA links OP-TEE's own. It records each
 * call and answers with the results the test sets.
 */
#include <stdlib.h>
#include <string.h>

#include <tee_internal_api.h>

#include "tee_session_mock.h"

struct __TEE_TASessionHandle {
    int open;
};

static struct __TEE_TASessionHandle mock_session;

struct tfw_tee_session_mock tfw_tee_session_mock;

void tfw_tee_session_mock_reset(void)
{
    memset(&tfw_tee_session_mock, 0, sizeof(tfw_tee_session_mock));
    mock_session.open = 0;
}

TEE_Result TEE_OpenTASession(const TEE_UUID *destination, uint32_t cancellationRequestTimeout,
                             uint32_t paramTypes, TEE_Param params[TEE_NUM_PARAMS],
                             TEE_TASessionHandle *session, uint32_t *returnOrigin)
{
    (void)cancellationRequestTimeout;
    (void)params;
    tfw_tee_session_mock.opens++;
    tfw_tee_session_mock.uuid = *destination;
    tfw_tee_session_mock.open_param_types = paramTypes;
    if (returnOrigin != NULL)
        *returnOrigin = 0u;
    if (tfw_tee_session_mock.open_result != TEE_SUCCESS)
        return tfw_tee_session_mock.open_result;
    if (mock_session.open)
        abort();
    mock_session.open = 1;
    tfw_tee_session_mock.live++;
    *session = &mock_session;
    return TEE_SUCCESS;
}

void TEE_CloseTASession(TEE_TASessionHandle session)
{
    tfw_tee_session_mock.closes++;
    if (session != &mock_session || !mock_session.open)
        abort();
    mock_session.open = 0;
    tfw_tee_session_mock.live--;
}

TEE_Result TEE_InvokeTACommand(TEE_TASessionHandle session, uint32_t cancellationRequestTimeout,
                               uint32_t commandID, uint32_t paramTypes,
                               TEE_Param params[TEE_NUM_PARAMS], uint32_t *returnOrigin)
{
    size_t i;

    (void)cancellationRequestTimeout;
    if (session != &mock_session || !mock_session.open)
        abort();
    tfw_tee_session_mock.invokes++;
    tfw_tee_session_mock.command_id = commandID;
    tfw_tee_session_mock.param_types = paramTypes;
    tfw_tee_session_mock.extra_data_len = params[0].memref.size;
    if (params[0].memref.size <= TFW_TEE_MOCK_EXTRA_MAX && params[0].memref.size != 0u)
        memcpy(tfw_tee_session_mock.extra_data, params[0].memref.buffer, params[0].memref.size);
    tfw_tee_session_mock.key_len = params[1].memref.size;
    if (returnOrigin != NULL)
        *returnOrigin = 0u;
    if (tfw_tee_session_mock.invoke_result != TEE_SUCCESS) {
        /* A failing PTA may leave the output half written. */
        memset(params[1].memref.buffer, 0xEE, params[1].memref.size);
        return tfw_tee_session_mock.invoke_result;
    }
    for (i = 0u; i < params[1].memref.size; i++)
        ((uint8_t *)params[1].memref.buffer)[i] =
            tfw_tee_session_mock.answer != NULL ? tfw_tee_session_mock.answer[i]
                                                : (uint8_t)(0x40u + i);
    return TEE_SUCCESS;
}

TEE_Result TEE_GetPropertyAsUUID(TEE_PropSetHandle propsetOrEnumerator, const char *name,
                                 TEE_UUID *value)
{
    /* The TA reads its own appID and nothing else. */
    if (propsetOrEnumerator != TEE_PROPSET_CURRENT_TA || strcmp(name, "gpd.ta.appID") != 0)
        abort();
    tfw_tee_session_mock.app_id_reads++;
    if (tfw_tee_session_mock.app_id_result != TEE_SUCCESS)
        return tfw_tee_session_mock.app_id_result;
    *value = tfw_tee_session_mock.app_id;
    return TEE_SUCCESS;
}
