/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_AUTH and the control steps behind it (src/core/ota_auth.c,
 * src/core/ota_activate.c, src/core/ota_ctl.c) through the dispatcher, with
 * test_ota_stage's link:
 *   - every byte of the OTA_AUTH signature at every value, and every mode
 *     byte: the MARK carries exactly the signature and mode the host sent,
 *     never the reference one or a mix -- the application holds the
 *     authorization and tfw-boot verifies it (test_boot_stage_mutate), so a
 *     mutated one reaches tfw-boot as itself and is refused there;
 *   - every OTA_AUTH length but 64: refused, and the hold dropped;
 *   - an OTA_AUTH replayed: one OTA_AUTH activates once;
 *   - the seven control steps in every order (bounded where OTA_BEGIN makes
 *     the rest equivalent; see the test), against a small
 *     model of the documented order (docs/ota/api.md): each answer is the
 *     model's, and the MARK is written only by an OTA_ACTIVATE that came
 *     straight after an OTA_AUTH, on a stream OTA_END verified.
 * Exhaustive, so there is no seed; each failure names its case.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "ota_ctl.h"
#include "ota_auth.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "caps/sys.h"
#include "sha256.h"
#include "handoff.h"
#include "boot_measure.h"
#include "tfw/protocol.h"
#include "tfw/status.h"
#include "dispatch.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */
#define TOTAL     512u
#define BODY      200u
#define MARK      ((uint32_t)TFW_OTA_STORE_MARK_ADDR)
#define SIG       TFW_OTA_AUTH_SIG_LEN
#define MAX_STEPS 5u

static uint8_t img[TOTAL];
static uint8_t meas[32];
static char    why[128];

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* test_ota_stage.c's build(TOTAL, BODY): the one valid image. */
static void build(void)
{
    tfw_sha256_t c;
    uint8_t     *t = &img[TOTAL - TRAILER];
    uint32_t     i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < BODY; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, img, (uint16_t)BODY);
    tfw_sha256_final(&c, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[8], 3u);
    put32(&t[12], BODY);
    memcpy(&t[16], meas, 32u);
}

static void put_block(uint8_t boot_state)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 7u;
    p.boot_state     = boot_state;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

static void reset_all(void)
{
    tfw_mock_reset();
    tfw_nor_model_reset();
    tfw_sys_reset_state();
    tfw_boot_handoff_init();
    tfw_stage_init();
    put_block(TFW_BOOT_STATE_GOOD);
    (void)tfw_ota_auth_take();   /* a hold from the last case is dropped */
}

void setUp(void)    { build(); reset_all(); }
void tearDown(void) { }

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + MARK;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

/* One request through the dispatcher, in a heap buffer of exactly its length
 * so a read past it is a sanitizer report. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *payload, unsigned n)
{
    uint8_t     *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t     *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, payload, n); }
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);
    st = tfw_dispatch(cmd, &r, &w);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_violations(), why);
    if (tfw_sys_reset_pending()) { tfw_sys_reset_state(); }
    free(in);
    free(out);
    return st;
}

static tfw_status_t begin(void)
{
    uint8_t p[4];
    put32(p, TOTAL);
    return feed(TFW_CMD_OTA_BEGIN, p, 4u);
}

static tfw_status_t data(uint32_t off)
{
    uint8_t  p[4u + CHUNK];
    unsigned n = off < TOTAL ? (TOTAL - off < CHUNK ? TOTAL - off : CHUNK) : CHUNK;

    put32(p, off);
    memcpy(&p[4], &img[off < TOTAL ? off : 0u], n);
    return feed(TFW_CMD_OTA_DATA, p, 4u + n);
}

static tfw_status_t auth(const uint8_t sig[SIG]) { return feed(TFW_CMD_OTA_AUTH, sig, SIG); }

static tfw_status_t activate(uint8_t mode) { return feed(TFW_CMD_OTA_ACTIVATE, &mode, 1u); }

/* BEGIN, every DATA, END: the stager COMPLETE on the valid image. */
static void ended(void)
{
    uint32_t off;

    TEST_ASSERT_EQUAL(TFW_OK, begin());
    for (off = 0u; off < TOTAL; off += CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, data(off));
    }
    TEST_ASSERT_EQUAL(TFW_OK, feed(TFW_CMD_OTA_END, NULL, 0u));
}

/* The MARK as OTA_ACTIVATE wrote it: the valid image, sig and mode. */
static void assert_mark(const uint8_t sig[SIG], uint8_t mode)
{
    const uint8_t *m = tfw_nor_model_mem() + MARK;

    TEST_ASSERT_TRUE_MESSAGE(marked(), why);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(meas, &m[TFW_STAGE_MARK_O_HASH], 32u, why);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sig, &m[TFW_STAGE_MARK_O_SIG], SIG, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(mode, m[TFW_STAGE_MARK_O_MODE], why);
}

static void reference_sig(uint8_t sig[SIG])
{
    unsigned i;
    for (i = 0u; i < SIG; i++) { sig[i] = (uint8_t)(0xA5u ^ (i * 29u)); }
}

/* --------------------------------------------------- the authorization */

static void test_every_signature_byte_at_every_value_is_carried_verbatim(void)
{
    uint8_t  sig[SIG];
    uint8_t  ref[SIG];
    unsigned i;
    unsigned v;

    reference_sig(ref);
    ended();
    for (i = 0u; i < SIG; i++) {
        for (v = 0u; v < 256u; v++) {
            memcpy(sig, ref, SIG);
            sig[i] = (uint8_t)v;
            snprintf(why, sizeof why, "signature byte %u = 0x%02X", i, v);
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, auth(sig), why);
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, activate(0u), why);
            assert_mark(sig, 0u);
        }
    }
}

static void test_every_mode_byte(void)
{
    uint8_t  ref[SIG];
    unsigned v;

    reference_sig(ref);
    ended();
    for (v = 0u; v < 256u; v++) {
        snprintf(why, sizeof why, "mode 0x%02X", v);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, auth(ref), why);
        if (v > 1u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_RANGE, activate((uint8_t)v), why);
            /* The refused request was the last one: the hold is gone. */
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, activate(0u), why);
            assert_mark(ref, 1u);   /* mode 1's MARK, untouched */
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, activate((uint8_t)v), why);
            assert_mark(ref, (uint8_t)v);
        }
    }
}

static void test_every_auth_length_but_64_is_refused_and_drops_the_hold(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD + 1u];
    uint8_t  ref[SIG];
    unsigned n;

    reference_sig(ref);
    memset(p, 0x5Au, sizeof p);
    ended();
    for (n = 0u; n <= TFW_MAX_PAYLOAD + 1u; n++) {
        if (n == SIG) {
            continue;
        }
        snprintf(why, sizeof why, "OTA_AUTH of %u bytes", n);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, auth(ref), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, feed(TFW_CMD_OTA_AUTH, p, n), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, activate(0u), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_CONFIRM, NULL, 0u), why);
        TEST_ASSERT_FALSE_MESSAGE(marked(), why);
    }
}

static void test_a_replayed_authorization_activates_once(void)
{
    uint8_t ref[SIG];

    reference_sig(ref);
    snprintf(why, sizeof why, "replay");
    ended();
    TEST_ASSERT_EQUAL(TFW_OK, auth(ref));
    TEST_ASSERT_EQUAL(TFW_OK, activate(0u));
    assert_mark(ref, 0u);
    /* The same ACTIVATE again, its OTA_AUTH spent. */
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(1u));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
    assert_mark(ref, 0u);
    /* An OTA_AUTH spent on a refused CONFIRM is spent too. */
    TEST_ASSERT_EQUAL(TFW_OK, auth(ref));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, feed(TFW_CMD_OTA_CONFIRM, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));
    /* A new round, the old authorization replayed: the application holds it
     * and writes it as sent; tfw-boot verifies it against this image. */
    ended();
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL(TFW_OK, auth(ref));
    TEST_ASSERT_EQUAL(TFW_OK, activate(0u));
    assert_mark(ref, 0u);
}

static void test_no_activate_on_a_trial_or_confirmed_image(void)
{
    static const uint8_t states[] = { TFW_BOOT_STATE_TRIAL, TFW_BOOT_STATE_CONFIRMED };
    uint8_t  ref[SIG];
    unsigned i;

    reference_sig(ref);
    for (i = 0u; i < sizeof states; i++) {
        snprintf(why, sizeof why, "boot state %u", (unsigned)states[i]);
        reset_all();
        ended();
        put_block(states[i]);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, auth(ref), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, activate(0u), why);
        TEST_ASSERT_FALSE_MESSAGE(marked(), why);
    }
}

/* ------------------------------------------------------------ the order */

/* The stager's own view, read without the dispatcher so a hold is kept. */
static void stager(uint8_t *state, uint32_t *received)
{
    uint8_t      out[8];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, NULL, 0u);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_cmd_status(&r, &w));
    *state    = out[0];
    *received = (uint32_t)out[1] | ((uint32_t)out[2] << 8) | ((uint32_t)out[3] << 16) | ((uint32_t)out[4] << 24);
}

/* In OTA_STATUS's numbering (ota_stage.h). */
typedef enum { ST_IDLE, ST_RECV, ST_COMPLETE, ST_FAILED } mstate_t;

typedef struct {
    mstate_t state;
    uint32_t received;
    uint8_t  marked;
    uint8_t  authed;   /* the last request was an OTA_AUTH that took */
} model_t;

static const char steps[] = "BDEAVCS";
#define NSTEPS 6u   /* the steps after OTA_BEGIN */

/* One step on the device and on the model; every answer must be the model's. */
static void step(model_t *m, char s)
{
    uint8_t      ref[SIG];
    tfw_status_t want;
    tfw_status_t got;
    uint8_t      took_auth = 0u;
    uint8_t      state;
    uint32_t     received;

    switch (s) {
    case 'B':
        want = TFW_OK;
        got  = begin();
        m->state = ST_RECV; m->received = 0u; m->marked = 0u;
        break;
    case 'D':
        /* the rest of the stream, or one chunk past it */
        if (m->state != ST_RECV) {
            want = TFW_ERR_BUSY;
            got  = data(m->received);
        } else if (m->received < TOTAL) {
            want = TFW_OK;
            do {
                got = data(m->received);
                m->received += (TOTAL - m->received < CHUNK) ? TOTAL - m->received : CHUNK;
            } while (got == TFW_OK && m->received < TOTAL);
        } else {
            want = TFW_ERR_RANGE;
            got  = data(TOTAL);
        }
        break;
    case 'E':
        if (m->state != ST_RECV) {
            want = TFW_ERR_BUSY;
        } else if (m->received != TOTAL) {
            want = TFW_ERR_FRAMING; m->state = ST_FAILED;
        } else {
            want = TFW_OK; m->state = ST_COMPLETE;
        }
        got = feed(TFW_CMD_OTA_END, NULL, 0u);
        break;
    case 'A':
        reference_sig(ref);
        want = TFW_OK;
        got  = auth(ref);
        took_auth = 1u;
        break;
    case 'V':
        if (!m->authed) {
            want = TFW_ERR_AUTH_REQUIRED;
        } else if (m->state != ST_COMPLETE) {
            want = TFW_ERR_BAD_STATE;
        } else {
            want = TFW_OK; m->marked = 1u;
        }
        got = activate(0u);
        break;
    case 'C':
        /* the platform block says GOOD: no trial to confirm */
        want = m->authed ? TFW_ERR_BAD_STATE : TFW_ERR_AUTH_REQUIRED;
        got  = feed(TFW_CMD_OTA_CONFIRM, NULL, 0u);
        break;
    default:
        want = TFW_OK;
        got  = feed(TFW_CMD_OTA_STATUS, NULL, 0u);
        break;
    }
    m->authed = (uint8_t)(took_auth && got == TFW_OK);
    TEST_ASSERT_EQUAL_MESSAGE(want, got, why);
    TEST_ASSERT_EQUAL_MESSAGE(m->marked, (uint8_t)marked(), why);
    TEST_ASSERT_EQUAL_MESSAGE((uint8_t)(m->marked && m->state == ST_COMPLETE), tfw_stage_install_pending(), why);
    if (m->marked) {
        reference_sig(ref);
        assert_mark(ref, 0u);
    }
    stager(&state, &received);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)m->state, state, why);
    if (m->state == ST_RECV) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(m->received, received, why);
    }
}

/* Each sequence from power-on: the stager IDLE, which only an OTA_BEGIN the
 * store refuses leaves it in after the first -- so one is sent with no part
 * on the bus. */
static void power_on(void)
{
    uint8_t  erased[TFW_STAGE_MARK_O_MODE + 1u];
    uint8_t  state;
    uint32_t received;

    /* Not reset_all: a whole part and EEPROM per sequence is most of the run
     * under the sanitizers. The MARK is erased as no OTA_BEGIN would leave it,
     * and no step writes the EEPROM: CONFIRM always refuses here. */
    memset(erased, 0xFF, sizeof erased);
    tfw_nor_model_load(MARK, erased, sizeof erased);
    tfw_sys_reset_state();
    tfw_nor_model_set_bus(TFW_NOR_MODEL_ABSENT_HIGH);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, begin());
    tfw_nor_model_set_bus(TFW_NOR_MODEL_PRESENT);
    (void)tfw_ota_auth_take();
    stager(&state, &received);
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_IDLE, state);
}

/* One sequence from power-on; answers whether it left the store marked. */
static unsigned run(const char *seq)
{
    model_t m;
    size_t  i;

    power_on();
    memset(&m, 0, sizeof m);
    for (i = 0u; seq[i] != '\0'; i++) {
        snprintf(why, sizeof why, "sequence %s, step %u", seq, (unsigned)i);
        step(&m, seq[i]);
    }
    return m.marked;
}

/* The code-th of the NSTEPS^len tails over the steps after OTA_BEGIN. */
static void tail(char *out, uint32_t len, uint32_t code)
{
    uint32_t i;
    for (i = 0u; i < len; i++, code /= NSTEPS) { out[i] = steps[1u + code % NSTEPS]; }
    out[len] = '\0';
}

/* Every head + tail, the tail every sequence of up to max steps. */
static unsigned all(const char *head, uint32_t max)
{
    char     seq[2u * MAX_STEPS + 2u];
    size_t   h = strlen(head);
    uint32_t len;
    uint32_t count;
    uint32_t code;
    unsigned activated = 0u;

    memcpy(seq, head, h);
    for (len = 0u, count = 1u; len <= max; len++, count *= NSTEPS) {
        for (code = 0u; code < count; code++) {
            tail(&seq[h], len, code);
            if (seq[0] != '\0') {
                activated += run(seq);
            }
        }
    }
    return activated;
}

/* OTA_BEGIN resets the stager, erases the MARK and drops any hold whatever came
 * before it (each sequence checks that), and copies the whole application to
 * PREV, so it is most of the run: every order of the other six steps up to
 * MAX_STEPS long from power-on, after one OTA_BEGIN up to one fewer, and
 * around a second OTA_BEGIN two either side. */
static void test_every_order_of_the_control_steps_answers_as_the_model(void)
{
    char     head[8];
    uint32_t len;
    uint32_t count;
    uint32_t code;
    unsigned activated;
    uint8_t  before[TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN];

    memcpy(before, tfw_mock_eeprom(), sizeof before);
    activated  = all("", MAX_STEPS);
    TEST_ASSERT_EQUAL(0u, activated);   /* nothing staged, nothing activates */
    activated  = all("B", MAX_STEPS - 1u);
    for (len = 0u, count = 1u; len <= 2u; len++, count *= NSTEPS) {
        for (code = 0u; code < count; code++) {
            head[0] = 'B';
            tail(&head[1], len, code);
            head[1u + len] = 'B';
            head[2u + len] = '\0';
            activated += all(head, 2u);
        }
    }
    TEST_ASSERT_TRUE(activated > 0u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, tfw_mock_eeprom(), TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_signature_byte_at_every_value_is_carried_verbatim);
    RUN_TEST(test_every_mode_byte);
    RUN_TEST(test_every_auth_length_but_64_is_refused_and_drops_the_hold);
    RUN_TEST(test_a_replayed_authorization_activates_once);
    RUN_TEST(test_no_activate_on_a_trial_or_confirmed_image);
    RUN_TEST(test_every_order_of_the_control_steps_answers_as_the_model);
    return UNITY_END();
}
