/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's TEE for the TA's tests (test/mock/gp_bridge.h). The Client API
 * calls go straight to the TA's entry points, marshalled as OP-TEE marshals
 * them: a temporary memory reference becomes a memref of the same buffer and
 * size, a value a value, and paramTypes passes through, its numbers being the
 * same in both APIs. The TA is not single-instance, so each open creates an
 * instance and each close or refused open destroys it. An error the TA
 * returns has origin TEEC_ORIGIN_TRUSTED_APP. TEE_GenerateRandom is a fixed
 * xorshift stream: the tests need bytes that differ, not entropy.
 */

#include <string.h>
#include <tee_client_api.h>
#include <tee_internal_api.h>

#include "gp_bridge.h"
#include "tfw_ta.h"

struct tfw_gp_mock tfw_gp_mock;

static uint32_t tfw_gp_random_state;

void tfw_gp_mock_reset(void)
{
    memset(&tfw_gp_mock, 0, sizeof(tfw_gp_mock));
    tfw_gp_random_state = 0x2545F491u;
}

void TEE_GenerateRandom(void *randomBuffer, size_t randomBufferLen)
{
    uint8_t *p = randomBuffer;
    size_t i;
    for (i = 0; i < randomBufferLen; i++) {
        tfw_gp_random_state ^= tfw_gp_random_state << 13;
        tfw_gp_random_state ^= tfw_gp_random_state >> 17;
        tfw_gp_random_state ^= tfw_gp_random_state << 5;
        p[i] = tfw_gp_mock.stuck_random ? 0u : (uint8_t)tfw_gp_random_state;
    }
    tfw_gp_mock.random_bytes += randomBufferLen;
}

static void tfw_gp_to_ta(const TEEC_Operation *op, TEE_Param params[4])
{
    unsigned i;
    memset(params, 0, 4 * sizeof(params[0]));
    for (i = 0; op != NULL && i < 4u; i++) {
        uint32_t t = TEE_PARAM_TYPE_GET(op->paramTypes, i);
        if (t >= TEE_PARAM_TYPE_MEMREF_INPUT) {
            params[i].memref.buffer = op->params[i].tmpref.buffer;
            params[i].memref.size = op->params[i].tmpref.size;
        } else if (t != TEE_PARAM_TYPE_NONE) {
            params[i].value.a = op->params[i].value.a;
            params[i].value.b = op->params[i].value.b;
        }
    }
}

static void tfw_gp_from_ta(TEEC_Operation *op, const TEE_Param params[4])
{
    unsigned i;
    for (i = 0; op != NULL && i < 4u; i++) {
        uint32_t t = TEE_PARAM_TYPE_GET(op->paramTypes, i);
        if (t >= TEE_PARAM_TYPE_MEMREF_INPUT) {
            op->params[i].tmpref.size = params[i].memref.size;
        } else if (t != TEE_PARAM_TYPE_NONE) {
            op->params[i].value.a = params[i].value.a;
            op->params[i].value.b = params[i].value.b;
        }
    }
}

static uint32_t tfw_gp_types(const TEEC_Operation *op)
{
    return op != NULL ? op->paramTypes : TEEC_NONE;
}

TEEC_Result TEEC_InitializeContext(const char *name, TEEC_Context *context)
{
    (void)name;
    if (tfw_gp_mock.init_result != TEEC_SUCCESS) {
        return tfw_gp_mock.init_result;
    }
    context->open = 1;
    tfw_gp_mock.contexts++;
    return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(TEEC_Context *context)
{
    context->open = 0;
    tfw_gp_mock.contexts--;
}

TEEC_Result TEEC_OpenSession(TEEC_Context *context, TEEC_Session *session,
                             const TEEC_UUID *destination, uint32_t connectionMethod,
                             const void *connectionData, TEEC_Operation *operation,
                             uint32_t *returnOrigin)
{
    static const TEEC_UUID ta = TFW_TA_UUID;
    TEE_Param params[4];
    TEE_Result rc;
    (void)connectionData;
    if (tfw_gp_mock.open_result != TEEC_SUCCESS) {
        *returnOrigin = TEEC_ORIGIN_TEE;
        return tfw_gp_mock.open_result;
    }
    if (memcmp(destination, &ta, sizeof(ta)) != 0 || connectionMethod != TEEC_LOGIN_PUBLIC) {
        *returnOrigin = TEEC_ORIGIN_TEE;
        return TEEC_ERROR_ITEM_NOT_FOUND;
    }
    rc = TA_CreateEntryPoint();
    if (rc != TEE_SUCCESS) {
        *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
        return rc;
    }
    tfw_gp_to_ta(operation, params);
    rc = TA_OpenSessionEntryPoint(tfw_gp_types(operation), params, &session->ta);
    if (rc != TEE_SUCCESS && !tfw_gp_mock.open_always) {
        TA_DestroyEntryPoint();
        *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
        return rc;
    }
    tfw_gp_from_ta(operation, params);
    session->ctx = context;
    tfw_gp_mock.sessions++;
    *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
    return TEEC_SUCCESS;
}

void TEEC_CloseSession(TEEC_Session *session)
{
    TA_CloseSessionEntryPoint(session->ta);
    TA_DestroyEntryPoint();
    session->ctx = NULL;
    tfw_gp_mock.sessions--;
}

TEEC_Result TEEC_InvokeCommand(TEEC_Session *session, uint32_t commandID,
                               TEEC_Operation *operation, uint32_t *returnOrigin)
{
    TEE_Param params[4];
    TEE_Result rc;
    if (tfw_gp_mock.force && commandID == tfw_gp_mock.force_cmd) {
        *returnOrigin = tfw_gp_mock.force_origin;
        return tfw_gp_mock.force_rc;
    }
    tfw_gp_to_ta(operation, params);
    rc = TA_InvokeCommandEntryPoint(session->ta, commandID, tfw_gp_types(operation), params);
    params[0].value.a ^= tfw_gp_mock.value_a_xor;
    tfw_gp_from_ta(operation, params);
    *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
    return rc;
}
