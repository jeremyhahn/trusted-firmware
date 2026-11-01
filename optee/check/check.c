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

/* SEAL vector 1's first ciphertext byte: §2's 27 bytes, then the bstr head 58 37. */
#define TFW_CHECK_CIPHERTEXT 29u

/* SEAL or UNSEAL of in into out, with the associated data empty; *out_len in and out. */
static int tfw_check_seal_cmd(TEEC_Session *s, uint32_t cmd, const void *in, size_t in_len,
                              void *out, size_t *out_len, uint32_t want)
{
    TEEC_Operation op;
    int ok;
    tfw_check_op(&op, cmd == TFW_TA_VEC_CMD_SEAL ? TFW_TA_VEC_PT_SEAL : TFW_TA_VEC_PT_UNSEAL);
    op.params[0].tmpref.buffer = (void *)(uintptr_t)in;
    op.params[0].tmpref.size = in_len;
    op.params[2].tmpref.buffer = out;
    op.params[2].tmpref.size = *out_len;
    ok = tfw_check_invoke(s, cmd, &op, want);
    *out_len = op.params[2].tmpref.size;
    return ok;
}

/*
 * Seals vector 1's plaintext. The blob is keyed by this device's TA unique key,
 * so only its size can match go-xkms's blob, never its bytes.
 */
static int tfw_check_seal(TEEC_Session *s, uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)])
{
    size_t len = sizeof(TFW_TA_VEC_SEAL1_BLOB);
    return tfw_check_seal_cmd(s, TFW_TA_VEC_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                              sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), blob, &len,
                              TFW_TA_VEC_SUCCESS) &&
           len == sizeof(TFW_TA_VEC_SEAL1_BLOB);
}

static int tfw_check_seal_unseal(TEEC_Session *s)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    uint8_t out[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    size_t out_len = sizeof(out);
    memset(out, 0, sizeof(out));
    return tfw_check_seal(s, blob) &&
           tfw_check_seal_cmd(s, TFW_TA_VEC_CMD_UNSEAL, blob, sizeof(blob), out, &out_len,
                              TFW_TA_VEC_SUCCESS) &&
           out_len == sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT) &&
           memcmp(out, TFW_TA_VEC_SEAL1_PLAINTEXT, out_len) == 0;
}

/* Two SEALs of one plaintext differ: each draws its own nonce. */
static int tfw_check_seal_fresh(TEEC_Session *s)
{
    uint8_t a[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    uint8_t b[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    return tfw_check_seal(s, a) && tfw_check_seal(s, b) && memcmp(a, b, sizeof(a)) != 0;
}

static int tfw_check_unseal_tampered(TEEC_Session *s)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    uint8_t out[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    size_t out_len = sizeof(out);
    if (!tfw_check_seal(s, blob))
        return 0;
    blob[TFW_CHECK_CIPHERTEXT] ^= 0x80u;
    return tfw_check_seal_cmd(s, TFW_TA_VEC_CMD_UNSEAL, blob, sizeof(blob), out, &out_len,
                              TFW_TA_VEC_MAC_INVALID);
}

/* One byte short answers TEE_ERROR_SHORT_BUFFER and the size needed. */
static int tfw_check_seal_short(TEEC_Session *s)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    size_t len = sizeof(blob) - 1u;
    return tfw_check_seal_cmd(s, TFW_TA_VEC_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                              sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), blob, &len,
                              TFW_TA_VEC_SHORT_BUFFER) &&
           len == sizeof(TFW_TA_VEC_SEAL1_BLOB);
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
    tfw_check_verdict(&c, "seal-unseal", tfw_check_seal_unseal(&s));
    tfw_check_verdict(&c, "seal-fresh-nonce", tfw_check_seal_fresh(&s));
    tfw_check_verdict(&c, "unseal-mac-invalid", tfw_check_unseal_tampered(&s));
    tfw_check_verdict(&c, "seal-short-buffer", tfw_check_seal_short(&s));
    tfw_check_verdict(&c, "seal-bad-types",
                      tfw_check_with(&s, TFW_TA_VEC_CMD_SEAL, TFW_TA_VEC_PT_GET_RANDOM,
                                     TFW_TA_VEC_BAD_PARAMETERS));
    tfw_check_verdict(&c, "unseal-bad-types",
                      tfw_check_with(&s, TFW_TA_VEC_CMD_UNSEAL, TFW_TA_VEC_PT_GET_RANDOM,
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
