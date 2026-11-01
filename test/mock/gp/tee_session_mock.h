/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What test/mock/gp/tee_session_mock.c saw of the TA-to-TA session calls,
 * and what it answers. Its PTA writes key byte i as 0x40 + i, or the test's
 * answer if it sets one (at most 32 bytes).
 */
#ifndef TFW_TEE_SESSION_MOCK_H
#define TFW_TEE_SESSION_MOCK_H

#include <stddef.h>
#include <stdint.h>

#include <tee_internal_api.h>

#define TFW_TEE_MOCK_EXTRA_MAX 2048u

struct tfw_tee_session_mock {
    TEE_Result open_result;   /* what TEE_OpenTASession answers */
    TEE_Result invoke_result; /* what TEE_InvokeTACommand answers */
    unsigned int opens;
    unsigned int invokes;
    unsigned int closes;
    int live;                 /* sessions open and not yet closed */
    TEE_UUID uuid;
    uint32_t open_param_types;
    uint32_t command_id;
    uint32_t param_types;
    uint8_t extra_data[TFW_TEE_MOCK_EXTRA_MAX];
    size_t extra_data_len;
    size_t key_len;
    const uint8_t *answer;    /* if set, the PTA's key: the first key_len bytes of it */
    TEE_UUID app_id;          /* what "gpd.ta.appID" answers: the TA's own UUID */
    TEE_Result app_id_result; /* what TEE_GetPropertyAsUUID answers */
    unsigned int app_id_reads;
};

extern struct tfw_tee_session_mock tfw_tee_session_mock;

void tfw_tee_session_mock_reset(void);

#endif /* TFW_TEE_SESSION_MOCK_H */
