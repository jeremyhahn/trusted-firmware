/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TF-M DICE partition (#608 slice 3) on host: tfw_dice_partition_start's
 * derivation, tfw_dice_dispatch's vector checks, and the non-secure client
 * (src/tz/tz_s_client.c) end to end over this suite's psa_call, which hands
 * the vectors to the dispatcher as TF-M's SPM hands them to the partition's
 * SFN (src/tz/tfm/tfw_dice_partition.c): sizes from the vectors, reads and
 * writes by copy, each out length set to the bytes written.
 *
 * The references are computed here from the UDS and the measurement:
 * CDI = HMAC-SHA256(UDS, measurement), the alias key from
 * HMAC-SHA256(CDI, TFW_DICE_ALIAS_LABEL), the UEID the first
 * TFW_ATTEST_ID_LEN bytes of HMAC-SHA256(UDS, TFW_DICE_UEID_LABEL), and the
 * DeviceID key SEED_PUB (test_soft_id_p256's point for UDS 00..1F).
 */
#include <stddef.h>
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/tz_s_api.h"
#include "psa/client.h"
#include "psa_manifest/sid.h"
#include "sha256.h"
#include "deviceid_p256.h"
#include "handoff.h"
#include "id.h"
#include "../../src/caps/attest.h"
#include "../../src/tz/dice_service.h"
#include "../../src/tz/dice_partition.h"

static uint8_t UDS[32];
static uint8_t MEAS[32];
static uint8_t NONCE[32];
static uint8_t DIGEST[32];

static const uint8_t SEED_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};

/* ---- the environment alias.c and the soft identity link against --------- */

uint8_t tfw_boot_handoff_device_seed(uint8_t out[32])
{
    tfw_hmac_sha256(UDS, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, out);
    return 1u;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64]) { return tfw_soft_id_pubkey(out); }
tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg)
{
    return tfw_soft_id_sign(UDS, digest, sig, alg);
}
tfw_crypto_tier_t tfw_crypto_tier(void) { return TFW_CRYPTO_SOFTWARE; }
void tfw_attest_measurement(uint8_t out[TFW_SHA256_LEN]) { memcpy(out, MEAS, 32u); }
const uint8_t *tfw_attest_device_id(void) { return UDS; }

/* ---- the SPM's side of psa_call ------------------------------------------ */

typedef struct {
    const psa_invec *in;
    size_t           in_len;
    psa_outvec      *out;
    size_t           written[TFW_DICE_IOVEC];
    unsigned         writes;
} call_t;

static size_t call_read(void *ctx, uint32_t vec, void *buf, size_t len)
{
    call_t *c = ctx;
    size_t  n = c->in[vec].len < len ? c->in[vec].len : len;

    memcpy(buf, c->in[vec].base, n);
    return n;
}

static void call_write(void *ctx, uint32_t vec, const void *buf, size_t len)
{
    call_t *c = ctx;

    /* The SPM refuses a write past the client's vector. */
    TEST_ASSERT_TRUE(len <= c->out[vec].len - c->written[vec]);
    memcpy((uint8_t *)c->out[vec].base + c->written[vec], buf, len);
    c->written[vec] += len;
    c->writes++;
}

static unsigned     s_calls;
static psa_status_t s_framework_error; /* nonzero: psa_call answers it */
static int          s_lie_vec = -1;     /* >= 0: psa_call reports s_lie_len */
static size_t       s_lie_len;          /* as that out-vector's length */

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec *in_vec, size_t in_len,
                      psa_outvec *out_vec, size_t out_len)
{
    call_t         c;
    tfw_dice_msg_t m;
    tfw_status_t   st;

    s_calls++;
    if (s_framework_error != PSA_SUCCESS) {
        return s_framework_error;
    }
    TEST_ASSERT_EQUAL_INT32((psa_handle_t)TFW_DICE_SERVICE_HANDLE, handle);
    TEST_ASSERT_TRUE(in_len <= TFW_DICE_IOVEC && out_len <= TFW_DICE_IOVEC);
    memset(&c, 0, sizeof c);
    memset(&m, 0, sizeof m);
    c.in = in_vec;
    c.in_len = in_len;
    c.out = out_vec;
    for (size_t i = 0u; i < in_len; i++) { m.in_size[i] = in_vec[i].len; }
    for (size_t i = 0u; i < out_len; i++) { m.out_size[i] = out_vec[i].len; }
    m.read = call_read;
    m.write = call_write;
    m.ctx = &c;
    st = tfw_dice_dispatch(type, &m);
    for (size_t i = 0u; i < out_len; i++) { out_vec[i].len = c.written[i]; }
    if (s_lie_vec >= 0 && (size_t)s_lie_vec < out_len) {
        out_vec[s_lie_vec].len = s_lie_len;
    }
    return (psa_status_t)st;
}

/* ---- helpers ------------------------------------------------------------- */

static void cdi_ref(uint8_t out[32])
{
    tfw_hmac_sha256(UDS, 32u, MEAS, 32u, out);
}

static void alias_pub_ref(uint8_t out[64])
{
    uint8_t cdi[32], seed[32];

    cdi_ref(cdi);
    tfw_hmac_sha256(cdi, 32u, (const uint8_t *)TFW_DICE_ALIAS_LABEL,
                    TFW_DICE_ALIAS_LABEL_LEN, seed);
    TEST_ASSERT_TRUE(tfw_deviceid_p256_public(seed, out));
}

static void ueid_ref(uint8_t out[32])
{
    tfw_hmac_sha256(UDS, 32u, (const uint8_t *)TFW_DICE_UEID_LABEL,
                    TFW_DICE_UEID_LABEL_LEN, out);
}

/* The whole alias certificate for NONCE through the client, page by page. */
static uint32_t client_cert(uint8_t *buf, size_t cap, size_t page)
{
    uint32_t off = 0u, total = 0u;
    size_t   got;

    do {
        got = 0u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_cert(NONCE, off, buf + off,
                                                        page, &got, &total));
        TEST_ASSERT_TRUE(got > 0u && got <= page && total <= cap);
        off += (uint32_t)got;
    } while (off < total);
    return total;
}

/* The same certificate from the service directly. */
static uint32_t service_cert(uint8_t *buf, uint32_t cap)
{
    tfw_dice_cert_page_t r;
    uint32_t             off = 0u;

    do {
        memcpy(r.nonce, NONCE, sizeof r.nonce);
        r.off = off;
        r.out = buf + off;
        r.cap = 255u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_cert(&r));
        TEST_ASSERT_TRUE(r.total <= cap);
        off += r.got;
    } while (off < r.total);
    return r.total;
}

static int contains(const uint8_t *hay, size_t n, const uint8_t *needle,
                    size_t len)
{
    for (size_t i = 0u; i + len <= n; i++) {
        if (memcmp(hay + i, needle, len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A direct dispatch over in-vectors in0/in1 (NULL for none) and out-vectors of
 * the given sizes, preset to 0xEE; the call's record is left in s_call. */
static call_t     s_call;
static psa_invec  s_in[2];
static psa_outvec s_out[2];
static uint8_t    s_out0[300], s_out1[8];

static tfw_status_t dispatch(int32_t op, const void *in0, size_t len0,
                             const void *in1, size_t len1,
                             size_t out0, size_t out1)
{
    tfw_dice_msg_t m;

    memset(&s_call, 0, sizeof s_call);
    memset(&m, 0, sizeof m);
    memset(s_out0, 0xEE, sizeof s_out0);
    memset(s_out1, 0xEE, sizeof s_out1);
    s_in[0].base = in0;
    s_in[0].len = in0 != NULL ? len0 : 0u;
    s_in[1].base = in1;
    s_in[1].len = in1 != NULL ? len1 : 0u;
    s_out[0].base = s_out0;
    s_out[0].len = out0;
    s_out[1].base = s_out1;
    s_out[1].len = out1;
    s_call.in = s_in;
    s_call.in_len = 2u;
    s_call.out = s_out;
    m.in_size[0] = s_in[0].len;
    m.in_size[1] = s_in[1].len;
    m.out_size[0] = out0;
    m.out_size[1] = out1;
    m.read = call_read;
    m.write = call_write;
    m.ctx = &s_call;
    return tfw_dice_dispatch(op, &m);
}

static void assert_nothing_written(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, s_call.writes);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, s_out0, sizeof s_out0);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, s_out1, sizeof s_out1);
}

void setUp(void)
{
    for (uint8_t i = 0u; i < sizeof UDS; i++) { UDS[i] = i; }
    memset(MEAS, 0x27, sizeof MEAS);
    for (uint8_t i = 0u; i < sizeof NONCE; i++) { NONCE[i] = (uint8_t)(0xA0u + i); }
    tfw_sha256((const uint8_t *)"dice-608", 8u, DIGEST);
    s_calls = 0u;
    s_framework_error = PSA_SUCCESS;
    s_lie_vec = -1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_partition_start(UDS, MEAS, TFW_CRYPTO_SOFTWARE));
}

void tearDown(void)
{
    tfw_dice_service_clear();
}

/* ---- start: the DICE layer 0 derivation ---------------------------------- */

static void test_deviceid_pub_is_the_uds_deviceid(void)
{
    uint8_t got[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_deviceid_pub(got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_PUB, got, 64);
}

static void test_alias_pub_is_from_the_cdi_of_uds_and_measurement(void)
{
    uint8_t got[64], ref[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(got));
    alias_pub_ref(ref);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, 64);
}

static void test_another_measurement_moves_the_alias_key_only(void)
{
    uint8_t dev0[64], alias0[64], dev1[64], alias1[64], ref[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_deviceid_pub(dev0));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias0));
    MEAS[31] ^= 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_partition_start(UDS, MEAS, TFW_CRYPTO_SOFTWARE));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_deviceid_pub(dev1));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias1));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dev0, dev1, 64);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(alias0, alias1, 64));
    alias_pub_ref(ref);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, alias1, 64);
}

static void test_alias_cert_carries_the_ueid_measurement_and_alias_pub(void)
{
    static uint8_t got[1024];
    uint8_t        ueid[32], alias[64];
    uint32_t       n = client_cert(got, sizeof got, 255u);

    ueid_ref(ueid);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias));
    TEST_ASSERT_TRUE(contains(got, n, ueid, TFW_ATTEST_ID_LEN));
    TEST_ASSERT_TRUE(contains(got, n, MEAS, 32u));
    TEST_ASSERT_TRUE(contains(got, n, alias, 32u));
    TEST_ASSERT_TRUE(contains(got, n, alias + 32, 32u));
}

static void test_start_keeps_no_secret_in_any_answer(void)
{
    static uint8_t got[1024];
    uint8_t        cdi[32], sig[64];
    uint32_t       n = client_cert(got, sizeof got, 255u);

    cdi_ref(cdi);
    TEST_ASSERT_FALSE(contains(got, n, cdi, 32u));
    TEST_ASSERT_FALSE(contains(got, n, UDS, 32u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_sign(DIGEST, sig));
    TEST_ASSERT_FALSE(contains(sig, sizeof sig, cdi, 32u));
}

/* ---- the client over psa_call: same bytes as the service ----------------- */

static void test_start_tier_is_the_certificate_tier(void)
{
    static uint8_t sw[1024], se[1024];
    uint8_t        alias0[64], alias1[64];
    uint32_t       n_sw = client_cert(sw, sizeof sw, 255u);
    uint32_t       n_se;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias0));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_partition_start(UDS, MEAS, TFW_CRYPTO_SECURE_ELEMENT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias1));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(alias0, alias1, 64);
    n_se = client_cert(se, sizeof se, 255u);
    TEST_ASSERT_FALSE(n_se == n_sw && memcmp(sw, se, n_sw) == 0);
}

static void test_client_cert_is_the_service_cert_at_any_page_size(void)
{
    static uint8_t ref[1024], got[1024];
    uint32_t       n = service_cert(ref, sizeof ref);

    TEST_ASSERT_EQUAL_UINT32(n, client_cert(got, sizeof got, 255u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
    memset(got, 0, sizeof got);
    TEST_ASSERT_EQUAL_UINT32(n, client_cert(got, sizeof got, 17u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
    /* A buffer past the page limit still gets one page at most. */
    memset(got, 0, sizeof got);
    TEST_ASSERT_EQUAL_UINT32(n, client_cert(got, sizeof got, 1000u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
}

static void test_client_sign_verifies_under_the_alias_pub_only(void)
{
    uint8_t alias[64], dev[64], sig[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_pub(alias));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_deviceid_pub(dev));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_s_dice_alias_sign(DIGEST, sig));
    TEST_ASSERT_TRUE(uECC_verify(alias, DIGEST, 32u, sig, uECC_secp256r1()));
    TEST_ASSERT_FALSE(uECC_verify(dev, DIGEST, 32u, sig, uECC_secp256r1()));
}

static void test_client_offset_past_the_end_is_range(void)
{
    uint8_t  page[255];
    size_t   got = 7u;
    uint32_t total = 0xEEEEEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_cert(NONCE, 70000u, page,
                                                           sizeof page, &got, &total));
    TEST_ASSERT_EQUAL_size_t(7u, got);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, total);
}

static void test_client_null_arguments_are_refused_without_a_call(void)
{
    uint8_t  buf[255];
    size_t   got;
    uint32_t total;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_deviceid_pub(NULL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_pub(NULL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_sign(NULL, buf));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_sign(DIGEST, NULL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_cert(NULL, 0u, buf, 255u, &got, &total));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_cert(NONCE, 0u, NULL, 255u, &got, &total));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_cert(NONCE, 0u, buf, 255u, NULL, &total));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_s_dice_alias_cert(NONCE, 0u, buf, 255u, &got, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, s_calls);
}

/* An answer whose out-vector length is not the operation's is the
 * framework's fault, not a result: the client refuses it. */
static void test_client_refuses_an_answer_of_the_wrong_length(void)
{
    uint8_t  buf[255], key[64];
    size_t   got = 7u;
    uint32_t total;

    s_lie_vec = 0;
    s_lie_len = 63u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_deviceid_pub(key));
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_pub(key));
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_sign(DIGEST, key));
    s_lie_len = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_cert(NONCE, 0u, buf, 17u, &got, &total));
    s_lie_len = 18u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_cert(NONCE, 0u, buf, 17u, &got, &total));
    s_lie_vec = 1;
    s_lie_len = 3u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_cert(NONCE, 0u, buf, 17u, &got, &total));
    TEST_ASSERT_EQUAL_size_t(7u, got);
}

static void test_framework_error_is_internal(void)
{
    uint8_t buf[64];

    s_framework_error = PSA_ERROR_CONNECTION_REFUSED;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_deviceid_pub(buf));
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_s_dice_alias_sign(DIGEST, buf));
}

/* ---- dispatch: wrong vectors refused, nothing written -------------------- */

static void test_wrong_input_sizes_are_bad_argc_untouched(void)
{
    uint8_t  big[40];
    uint32_t off = 0u;
    uint16_t short_off = 0u;

    memset(big, 0x11, sizeof big);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_DEVICEID_PUB, big, 1u, NULL, 0u, 64u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_PUB, NULL, 0u, big, 1u, 64u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 31u, &off, 4u, 255u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_CERT, big, 33u, &off, 4u, 255u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 32u, &short_off, 2u, 255u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 32u, NULL, 0u, 255u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_SIGN, DIGEST, 31u, NULL, 0u, 64u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_SIGN, big, 33u, NULL, 0u, 64u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_DICE_OP_ALIAS_SIGN, DIGEST, 32u, big, 1u, 64u, 0u));
    assert_nothing_written();
}

static void test_short_outputs_are_range_untouched(void)
{
    uint32_t off = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, dispatch(TFW_DICE_OP_DEVICEID_PUB, NULL, 0u, NULL, 0u, 63u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, dispatch(TFW_DICE_OP_ALIAS_PUB, NULL, 0u, NULL, 0u, 0u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, dispatch(TFW_DICE_OP_ALIAS_SIGN, DIGEST, 32u, NULL, 0u, 63u, 0u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 32u, &off, 4u, 0u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 32u, &off, 4u, 255u, 3u));
    assert_nothing_written();
}

static void test_exact_outputs_are_written_once(void)
{
    uint32_t off = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_DICE_OP_DEVICEID_PUB, NULL, 0u, NULL, 0u, 64u, 0u));
    TEST_ASSERT_EQUAL_size_t(64u, s_call.written[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_PUB, s_out0, 64);
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_DICE_OP_ALIAS_CERT, NONCE, 32u, &off, 4u, 17u, 4u));
    TEST_ASSERT_EQUAL_size_t(17u, s_call.written[0]);
    TEST_ASSERT_EQUAL_size_t(4u, s_call.written[1]);
    TEST_ASSERT_EQUAL_UINT(2u, s_call.writes);
}

static void test_unknown_operations_are_refused_untouched(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, dispatch(0, NULL, 0u, NULL, 0u, 64u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, dispatch(5, NULL, 0u, NULL, 0u, 64u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, dispatch(-1, NULL, 0u, NULL, 0u, 64u, 4u));
    assert_nothing_written();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_dispatch(TFW_DICE_OP_ALIAS_PUB, NULL));
}

/* A message without its read or write callback is the glue's fault: the
 * dispatcher refuses it before any operation runs, so nothing is written. */
static void test_missing_read_or_write_is_range_untouched(void)
{
    tfw_dice_msg_t m;
    uint32_t       off = 0u;

    memset(&s_call, 0, sizeof s_call);
    memset(&m, 0, sizeof m);
    memset(s_out0, 0xEE, sizeof s_out0);
    memset(s_out1, 0xEE, sizeof s_out1);
    s_in[0].base = NONCE;
    s_in[0].len = 31u;
    s_in[1].base = &off;
    s_in[1].len = 4u;
    s_out[0].base = s_out0;
    s_out[0].len = 64u;
    s_out[1].base = s_out1;
    s_out[1].len = 4u;
    s_call.in = s_in;
    s_call.in_len = 2u;
    s_call.out = s_out;
    m.out_size[0] = 64u;
    m.ctx = &s_call;
    m.write = call_write; /* no read: DEVICEID_PUB would read nothing */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_dispatch(TFW_DICE_OP_DEVICEID_PUB, &m));
    assert_nothing_written();
    m.read = call_read;
    m.write = NULL;      /* no write: the 31-byte nonce would be BAD_ARGC */
    m.in_size[0] = 31u;
    m.in_size[1] = 4u;
    m.out_size[1] = 4u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_dispatch(TFW_DICE_OP_ALIAS_CERT, &m));
    assert_nothing_written();
}

/* ---- not started: every call answers NOT_PROVISIONED --------------------- */

static void test_without_start_every_call_is_not_provisioned(void)
{
    uint8_t  buf[255];
    size_t   got = 7u;
    uint32_t total = 0xEEEEEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_partition_start(NULL, MEAS, TFW_CRYPTO_SOFTWARE));
    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_s_dice_deviceid_pub(buf));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_s_dice_alias_pub(buf));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_s_dice_alias_sign(DIGEST, buf));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED,
                      tfw_s_dice_alias_cert(NONCE, 0u, buf, sizeof buf, &got, &total));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
    TEST_ASSERT_EQUAL_size_t(7u, got);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, total);
    /* A start refused after a good one leaves nothing of the good one. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_partition_start(UDS, MEAS, TFW_CRYPTO_SOFTWARE));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_partition_start(UDS, NULL, TFW_CRYPTO_SOFTWARE));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_s_dice_alias_pub(buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deviceid_pub_is_the_uds_deviceid);
    RUN_TEST(test_alias_pub_is_from_the_cdi_of_uds_and_measurement);
    RUN_TEST(test_another_measurement_moves_the_alias_key_only);
    RUN_TEST(test_alias_cert_carries_the_ueid_measurement_and_alias_pub);
    RUN_TEST(test_start_keeps_no_secret_in_any_answer);
    RUN_TEST(test_start_tier_is_the_certificate_tier);
    RUN_TEST(test_client_cert_is_the_service_cert_at_any_page_size);
    RUN_TEST(test_client_sign_verifies_under_the_alias_pub_only);
    RUN_TEST(test_client_offset_past_the_end_is_range);
    RUN_TEST(test_client_null_arguments_are_refused_without_a_call);
    RUN_TEST(test_client_refuses_an_answer_of_the_wrong_length);
    RUN_TEST(test_framework_error_is_internal);
    RUN_TEST(test_wrong_input_sizes_are_bad_argc_untouched);
    RUN_TEST(test_short_outputs_are_range_untouched);
    RUN_TEST(test_exact_outputs_are_written_once);
    RUN_TEST(test_unknown_operations_are_refused_untouched);
    RUN_TEST(test_missing_read_or_write_is_range_untouched);
    RUN_TEST(test_without_start_every_call_is_not_provisioned);
    return UNITY_END();
}
