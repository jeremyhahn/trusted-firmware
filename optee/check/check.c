/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-ta-check (docs/optee-trustzone/README.md, "How go-enclave's backendtest
 * reaches the TA"): a session to the TA through the GlobalPlatform TEE Client
 * API v1.0, as libteec implements it, and each command against the literal
 * vectors: GET_INFO and GET_RANDOM, their bad parameters, and the ids the TA
 * does not implement. Every call carries the vector's paramTypes and is judged
 * on the vector's return code, so the wire values are the literals, not the
 * TA's header.
 */

#include <stdint.h>
#include <string.h>
#include <tee_client_api.h>

#include "check.h"
#include "tfw_ta.h"
/* By its path: the firmware's own testdata/vectors.h is on the host's -I list. */
#include "../testdata/vectors.h"

/* The Client API's own constants are the vectors' numbers. */
_Static_assert(TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE) ==
                   TFW_TA_VEC_PT_NONE, "paramTypes NONE");
_Static_assert(TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE) ==
                   TFW_TA_VEC_PT_GET_INFO, "paramTypes GET_INFO");
_Static_assert(TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE) ==
                   TFW_TA_VEC_PT_GET_RANDOM, "paramTypes GET_RANDOM");
_Static_assert(TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE) ==
                   TFW_TA_VEC_PT_VALUE_IN, "paramTypes VALUE_INPUT");
_Static_assert(TEEC_SUCCESS == TFW_TA_VEC_SUCCESS, "TEEC_SUCCESS");
_Static_assert(TEEC_ERROR_BAD_PARAMETERS == TFW_TA_VEC_BAD_PARAMETERS, "BAD_PARAMETERS");
_Static_assert(TEEC_ERROR_NOT_SUPPORTED == TFW_TA_VEC_NOT_SUPPORTED, "NOT_SUPPORTED");
_Static_assert(TEEC_ORIGIN_TRUSTED_APP == TFW_TA_VEC_ORIGIN_TRUSTED_APP, "origin");
_Static_assert(TEEC_LOGIN_PUBLIC == TFW_TA_VEC_LOGIN_PUBLIC, "login");

/* The two draws GET_RANDOM's case compares. */
#define TFW_CHECK_DRAW 32u

struct tfw_check {
    FILE *out;
    int passed;
    int failed;
};

static void tfw_check_verdict(struct tfw_check *c, const char *name, int ok)
{
    fprintf(c->out, "tfw-ta-check: %s %s\n", ok ? "ok" : "FAIL", name);
    if (ok) {
        c->passed++;
    } else {
        c->failed++;
    }
}

static void tfw_check_op(TEEC_Operation *op, uint32_t paramTypes)
{
    memset(op, 0, sizeof(*op));
    op->paramTypes = paramTypes;
}

/* A success comes from anywhere; an error must be the TA's own answer. */
static int tfw_check_answer(TEEC_Result rc, uint32_t origin, uint32_t want)
{
    return rc == want && (want == TFW_TA_VEC_SUCCESS || origin == TFW_TA_VEC_ORIGIN_TRUSTED_APP);
}

static int tfw_check_invoke(TEEC_Session *s, uint32_t cmd, TEEC_Operation *op, uint32_t want)
{
    uint32_t origin = 0;
    TEEC_Result rc = TEEC_InvokeCommand(s, cmd, op, &origin);
    return tfw_check_answer(rc, origin, want);
}

/* The header's UUID is the vector's, field by field, in RFC 9562's order. */
static int tfw_check_uuid(const TEEC_UUID *u)
{
    const uint8_t *v = TFW_TA_VEC_UUID;
    return u->timeLow == ((uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 |
                          (uint32_t)v[2] << 8 | v[3]) &&
           u->timeMid == (uint16_t)(v[4] << 8 | v[5]) &&
           u->timeHiAndVersion == (uint16_t)(v[6] << 8 | v[7]) &&
           memcmp(u->clockSeqAndNode, v + 8, 8) == 0;
}

static int tfw_check_get_info(TEEC_Session *s)
{
    TEEC_Operation op;
    tfw_check_op(&op, TFW_TA_VEC_PT_GET_INFO);
    op.params[0].value.a = 0xFFFFFFFFu;
    op.params[0].value.b = 0xFFFFFFFFu;
    return tfw_check_invoke(s, TFW_TA_VEC_CMD_GET_INFO, &op, TFW_TA_VEC_SUCCESS) &&
           op.params[0].value.a == TFW_TA_VEC_INFO_VERSION &&
           op.params[0].value.b == TFW_TA_VEC_INFO_FLAGS;
}

static int tfw_check_with(TEEC_Session *s, uint32_t cmd, uint32_t paramTypes, uint32_t want)
{
    static uint8_t buf[TFW_CHECK_DRAW];
    TEEC_Operation op;
    tfw_check_op(&op, paramTypes);
    op.params[0].tmpref.buffer = buf;
    op.params[0].tmpref.size = sizeof(buf);
    return tfw_check_invoke(s, cmd, &op, want);
}

static int tfw_check_draw(TEEC_Session *s, void *buf, size_t size, uint32_t want)
{
    TEEC_Operation op;
    tfw_check_op(&op, TFW_TA_VEC_PT_GET_RANDOM);
    op.params[0].tmpref.buffer = buf;
    op.params[0].tmpref.size = size;
    return tfw_check_invoke(s, TFW_TA_VEC_CMD_GET_RANDOM, &op, want);
}

/* Two draws, both answered, and not the same bytes. */
static int tfw_check_get_random(TEEC_Session *s)
{
    uint8_t a[TFW_CHECK_DRAW];
    uint8_t b[TFW_CHECK_DRAW];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    return tfw_check_draw(s, a, sizeof(a), TFW_TA_VEC_SUCCESS) &&
           tfw_check_draw(s, b, sizeof(b), TFW_TA_VEC_SUCCESS) &&
           memcmp(a, b, sizeof(a)) != 0;
}

/* One byte past the largest buffer, so the size alone is wrong. */
static uint8_t tfw_check_big[TFW_TA_VEC_RANDOM_MAX + 1u];

/* An open with a parameter is refused by the TA, and leaves no session. */
static int tfw_check_open_refused(TEEC_Context *ctx, const TEEC_UUID *uuid)
{
    TEEC_Session s;
    TEEC_Operation op;
    uint32_t origin = 0;
    TEEC_Result rc;
    tfw_check_op(&op, TFW_TA_VEC_PT_VALUE_IN);
    rc = TEEC_OpenSession(ctx, &s, uuid, TEEC_LOGIN_PUBLIC, NULL, &op, &origin);
    if (rc == TEEC_SUCCESS) {
        TEEC_CloseSession(&s);
        return 0;
    }
    return tfw_check_answer(rc, origin, TFW_TA_VEC_BAD_PARAMETERS);
}

int tfw_ta_check(FILE *out)
{
    static const uint32_t unknown[] = {
        TFW_TA_VEC_CMD_RESERVED, TFW_TA_VEC_CMD_LAST,
    };
    static const char *const unknown_name[] = {
        "unknown-0x00000000", "unknown-0xffffffff",
    };
    const TEEC_UUID uuid = TFW_TA_UUID;
    struct tfw_check c = { out, 0, 0 };
    TEEC_Context ctx;
    TEEC_Session s;
    TEEC_Operation op;
    uint32_t origin = 0;
    TEEC_Result rc;
    size_t i;

    tfw_check_verdict(&c, "uuid", tfw_check_uuid(&uuid));
    rc = TEEC_InitializeContext(NULL, &ctx);
    if (rc != TEEC_SUCCESS) {
        fprintf(out, "tfw-ta-check: FAIL context 0x%08x\n", (unsigned)rc);
        return -1;
    }
    tfw_check_op(&op, TFW_TA_VEC_PT_NONE);
    rc = TEEC_OpenSession(&ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, &op, &origin);
    if (rc != TEEC_SUCCESS) {
        fprintf(out, "tfw-ta-check: FAIL session 0x%08x origin %u\n", (unsigned)rc, (unsigned)origin);
        TEEC_FinalizeContext(&ctx);
        return -1;
    }

    tfw_check_verdict(&c, "get-info", tfw_check_get_info(&s));
    tfw_check_verdict(&c, "get-info-bad-types",
                      tfw_check_with(&s, TFW_TA_VEC_CMD_GET_INFO, TFW_TA_VEC_PT_GET_RANDOM,
                                     TFW_TA_VEC_BAD_PARAMETERS));
    tfw_check_verdict(&c, "get-random", tfw_check_get_random(&s));
    tfw_check_verdict(&c, "get-random-min",
                      tfw_check_draw(&s, tfw_check_big, TFW_TA_VEC_RANDOM_MIN, TFW_TA_VEC_SUCCESS));
    tfw_check_verdict(&c, "get-random-max",
                      tfw_check_draw(&s, tfw_check_big, TFW_TA_VEC_RANDOM_MAX, TFW_TA_VEC_SUCCESS));
    tfw_check_verdict(&c, "get-random-empty",
                      tfw_check_draw(&s, tfw_check_big, 0, TFW_TA_VEC_BAD_PARAMETERS));
    tfw_check_verdict(&c, "get-random-over",
                      tfw_check_draw(&s, tfw_check_big, TFW_TA_VEC_RANDOM_MAX + 1u,
                                     TFW_TA_VEC_BAD_PARAMETERS));
    tfw_check_verdict(&c, "get-random-bad-types",
                      tfw_check_with(&s, TFW_TA_VEC_CMD_GET_RANDOM, TFW_TA_VEC_PT_GET_INFO,
                                     TFW_TA_VEC_BAD_PARAMETERS));
    for (i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++) {
        tfw_check_verdict(&c, unknown_name[i],
                          tfw_check_with(&s, unknown[i], TFW_TA_VEC_PT_NONE,
                                         TFW_TA_VEC_NOT_SUPPORTED));
    }
    TEEC_CloseSession(&s);
    tfw_check_verdict(&c, "open-bad-parameters", tfw_check_open_refused(&ctx, &uuid));
    TEEC_FinalizeContext(&ctx);

    fprintf(out, "tfw-ta-check: %s, %d of %d passed\n", c.failed ? "FAIL" : "PASS", c.passed,
            c.passed + c.failed);
    return c.failed;
}
