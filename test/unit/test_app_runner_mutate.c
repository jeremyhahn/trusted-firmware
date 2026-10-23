/* The application runner under mutation (issue #163): src/core/app_runner.c
 * driven by seeded random sequences of start, poll, stop, evidence and
 * after-update calls, in any order, over descriptors mutated field by field,
 * against the recording application of test/mock/app_mock.c.
 *
 * test_app.c defines each answer one call at a time. What this suite pins is
 * that the answers hold in any order and from any state: a call the state does
 * not allow calls no hook, leaves the state and the overrun count unchanged,
 * and answers what test_app.c defines -- evidence TFW_ERR_UNSUPPORTED with the
 * caller's buffer untouched, after-update TFW_OK -- and a descriptor with any
 * field broken is refused without init ever being called, while one whose
 * every field is in range starts.
 *
 * The expected answer of each step comes from a model held here: the state,
 * the overrun count and the hook log. Whether a mutated descriptor is good is
 * known by construction (each field is set to a value chosen in or out of its
 * range), never by asking the runner.
 *
 * Deterministic: xorshift32 seeded with a literal; a failure prints the seed,
 * the run and the step.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "app_runner.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "update_request.h"
#include "tfw/app.h"
#include "tfw/config.h"
#include "tfw/protocol.h"

#define SEED   0xA99C0DE5u
#define RUNS   3000u
#define STEPS  30u          /* at most two hook letters a step: 60 < LOG_CAP */
#define FIELDS 7u

enum { F_MAGIC, F_ABI, F_NAME, F_PROFILE, F_CAPS, F_SLOTS, F_CFG };

static uint32_t rng;
static unsigned run_no, step_no;

/* The model. */
static tfw_app_state_t m_state;
static uint16_t        m_overruns;
static char            m_log[2u * STEPS + 1u];
static unsigned        m_len;
static uint8_t         bad_fields;      /* bit f set: field f is out of range */
static tfw_status_t    init_st, after_st, ev_st;
static tfw_ms_t        run_ms;
static uint8_t        *ev, *out, *before;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fail(const char *what)
{
    printf("seed 0x%08lx: run %u step %u: %s (log \"%s\", want \"%s\")\n", (unsigned long)SEED, run_no,
           step_no, what, tfw_mock_app_log(), m_log);
    TEST_FAIL_MESSAGE(what);
}

static void expect_hook(char c)
{
    m_log[m_len++] = c;
    m_log[m_len]   = '\0';
}

static void check_model(void)
{
    if (tfw_app_runner_state() != m_state) { fail("the state differs from the model"); }
    if (tfw_app_runner_overruns() != m_overruns) { fail("the overrun count differs from the model"); }
    if (strcmp(tfw_mock_app_log(), m_log) != 0) { fail("the hooks called differ from the model"); }
}

/* --------------------------------------------------------- the descriptor */

static uint8_t max_slots(void)
{
    return (uint8_t)(tfw_update_request_supported() ? TFW_OTA_SLOTS : 0u);
}

static void fill_text(char *p, unsigned n, unsigned len)
{
    unsigned i;
    memset(p, 0, n);
    for (i = 0u; i < len; i++) { p[i] = (char)(0x21u + rnd() % 0x5Eu); }
}

/* Field f set to a value in its range (good) or out of it. */
static void set_field(unsigned f, int good)
{
    tfw_app_descriptor_t *d = tfw_mock_app_descriptor();
    const uint16_t        lim = (uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE);
    const uint32_t        out_bits = ~(uint32_t)TFW_FEATURE_BITMAP;
    uint8_t               max = max_slots();

    if (f == F_CAPS && out_bits == 0u) { good = 1; }   /* every capability is in this image */
    switch (f) {
    case F_MAGIC:
        d->magic = (uint32_t)(good ? TFW_APP_MAGIC : (TFW_APP_MAGIC ^ ((uint32_t)1u << (rnd() % 32u))));
        break;
    case F_ABI:
        d->abi = (uint8_t)(good ? TFW_APP_ABI : (TFW_APP_ABI + 1u + rnd() % 255u));
        break;
    case F_NAME:
        if (good) {
            fill_text(d->name, TFW_APP_NAME_LEN, 1u + rnd() % (TFW_APP_NAME_LEN - 1u));
        } else if (rnd() & 1u) {
            fill_text(d->name, TFW_APP_NAME_LEN, TFW_APP_NAME_LEN);   /* no terminator */
        } else {
            fill_text(d->name, TFW_APP_NAME_LEN, rnd() % TFW_APP_NAME_LEN);
            d->name[0] = '\0';                                        /* empty */
        }
        break;
    case F_PROFILE:
        fill_text(d->profile, TFW_APP_PROFILE_LEN,
                  good ? rnd() % TFW_APP_PROFILE_LEN : TFW_APP_PROFILE_LEN);
        break;
    case F_CAPS:
        d->required_caps = rnd() & (uint32_t)TFW_FEATURE_BITMAP;
        if (!good) {
            uint32_t bit;
            do { bit = (uint32_t)1u << (rnd() % 32u); } while ((bit & out_bits) == 0u);
            d->required_caps |= bit;
        }
        break;
    case F_SLOTS:
        d->ota_slots = (uint8_t)(good ? rnd() % (max + 1u) : max + 1u + rnd() % (255u - max));
        break;
    default:
        d->config_len = (uint16_t)(good ? ((rnd() & 1u) ? lim : rnd() % (lim + 1u))
                                        : lim + 1u + rnd() % (0xFFFFu - lim));
        break;
    }
    if (good) {
        bad_fields = (uint8_t)(bad_fields & ~(1u << f));
    } else {
        bad_fields = (uint8_t)(bad_fields | (1u << f));
    }
}

/* -------------------------------------------------------------- the steps */

static void do_start(void)
{
    tfw_app_runner_start();
    m_overruns = 0u;
    if (bad_fields != 0u) {
        m_state = TFW_APP_STATE_UNSUPPORTED;
    } else {
        expect_hook('i');
        m_state = (init_st == TFW_OK) ? TFW_APP_STATE_RUNNING : TFW_APP_STATE_FAILED;
    }
}

static void do_poll(void)
{
    tfw_app_runner_poll();
    if (m_state == TFW_APP_STATE_RUNNING) {
        expect_hook('r');
        if (run_ms > TFW_APP_RUN_BUDGET_MS && m_overruns != 0xFFFFu) { m_overruns++; }
    }
}

static void do_stop(uint8_t for_update)
{
    tfw_app_runner_stop(for_update);
    if (m_state == TFW_APP_STATE_RUNNING) {
        if (for_update) { expect_hook('b'); }
        expect_hook('s');
        m_state = TFW_APP_STATE_STOPPED;
    }
}

static void do_evidence(void)
{
    tfw_status_t st;
    unsigned     i;

    for (i = 0u; i < TFW_APP_EVIDENCE_LEN; i++) { out[i] = (uint8_t)rnd(); }
    memcpy(before, out, TFW_APP_EVIDENCE_LEN);
    st = tfw_app_runner_evidence(out);
    if (m_state == TFW_APP_STATE_RUNNING) { expect_hook('e'); }
    if (m_state == TFW_APP_STATE_RUNNING && ev_st == TFW_OK) {
        if (st != TFW_OK || memcmp(out, ev, TFW_APP_EVIDENCE_LEN) != 0) {
            fail("running evidence was not passed through");
        }
    } else if (st != TFW_ERR_UNSUPPORTED || memcmp(out, before, TFW_APP_EVIDENCE_LEN) != 0) {
        fail("evidence refused but not UNSUPPORTED over an untouched buffer");
    }
}

static void do_after_update(void)
{
    const tfw_status_t st = tfw_app_runner_after_update();

    if (m_state == TFW_APP_STATE_RUNNING) {
        expect_hook('a');
        if (st != after_st) { fail("after-update did not answer the application's status"); }
    } else if (st != TFW_OK) {
        fail("after-update on a runner not running did not answer TFW_OK");
    }
}

/* Anything the application will answer next: init, after-update, evidence,
 * how long a run takes. Changes no runner state and calls no hook. */
static void do_rearm(void)
{
    static const tfw_status_t sts[4] = { TFW_OK, TFW_ERR_UNSUPPORTED, TFW_ERR_BAD_STATE, TFW_ERR_RANGE };
    unsigned i;

    init_st  = sts[rnd() % 4u];
    after_st = sts[rnd() % 4u];
    ev_st    = sts[rnd() % 4u];
    run_ms   = (tfw_ms_t)(rnd() % (2u * TFW_APP_RUN_BUDGET_MS + 2u));
    for (i = 0u; i < TFW_APP_EVIDENCE_LEN; i++) { ev[i] = (uint8_t)rnd(); }
    tfw_mock_app_set_init(init_st);
    tfw_mock_app_set_after_update(after_st);
    tfw_mock_app_set_evidence(ev_st, ev);
    tfw_mock_app_set_run_ms(run_ms);
}

static void begin_run(void)
{
    tfw_mock_reset();
    tfw_mock_app_reset();
    m_len      = 0u;
    m_log[0]   = '\0';
    bad_fields = 0u;
    init_st = after_st = TFW_OK;
    ev_st   = TFW_ERR_UNSUPPORTED;
    run_ms  = 0u;
}

void setUp(void)
{
    ev     = (uint8_t *)malloc(TFW_APP_EVIDENCE_LEN);
    out    = (uint8_t *)malloc(TFW_APP_EVIDENCE_LEN);
    before = (uint8_t *)malloc(TFW_APP_EVIDENCE_LEN);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_NOT_NULL(before);
    begin_run();
}

void tearDown(void)
{
    free(ev);
    free(out);
    free(before);
}

/* Every call the state does not allow, three times in a row, from the state
 * the runner is in now: no hook, no change, the defined answers. */
static void out_of_order_calls_change_nothing(void)
{
    unsigned k;

    for (k = 0u; k < 3u; k++) {
        do_poll();
        check_model();
        do_evidence();
        check_model();
        do_after_update();
        check_model();
        do_stop((uint8_t)(k & 1u));
        check_model();
    }
}

/* ------------------------------------------------------------------ cases */

/* First, so the runner has never been started: IDLE answers like the rest. */
static void test_app_runner_mutate_an_idle_runner_ignores_every_out_of_order_call(void)
{
    m_state    = TFW_APP_STATE_IDLE;
    m_overruns = 0u;
    tfw_mock_app_set_after_update(TFW_ERR_BAD_STATE);
    tfw_mock_app_set_evidence(TFW_OK, ev);
    after_st = TFW_ERR_BAD_STATE;
    ev_st    = TFW_OK;
    check_model();
    out_of_order_calls_change_nothing();
}

/* FAILED, UNSUPPORTED and STOPPED each reached, then every call made out of
 * order with the application armed to answer OK if it were asked. */
static void test_app_runner_mutate_failed_refused_and_stopped_ignore_every_call(void)
{
    unsigned which;

    rng = SEED;
    for (which = 0u; which < 3u; which++) {
        begin_run();
        run_no = which;
        if (which == 0u) {
            init_st = TFW_ERR_BAD_STATE;
            tfw_mock_app_set_init(init_st);
        } else if (which == 1u) {
            set_field(rnd() % FIELDS, 0);
        }
        do_start();
        if (which == 2u) {
            do_poll();
            do_stop(1u);
        }
        check_model();
        tfw_mock_app_set_evidence(TFW_OK, ev);
        ev_st = TFW_OK;
        tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
        run_ms = TFW_APP_RUN_BUDGET_MS + 1u;
        out_of_order_calls_change_nothing();
    }
}

/* Each field alone, every way out of range, many times: refused, init never
 * called; set back in range, the same descriptor starts. */
static void test_app_runner_mutate_any_one_bad_field_is_refused_before_init(void)
{
    unsigned f, k;

    rng = SEED ^ 0x5A5A5A5Au;
    for (f = 0u; f < FIELDS; f++) {
        for (k = 0u; k < 200u; k++) {
            begin_run();
            run_no  = f;
            step_no = k;
            set_field(f, 0);
            do_start();
            check_model();
            set_field(f, 1);
            do_start();
            check_model();
        }
    }
}

static void test_app_runner_mutate_random_call_orders_follow_the_model(void)
{
    m_state    = tfw_app_runner_state();
    m_overruns = tfw_app_runner_overruns();
    rng = SEED;
    for (run_no = 0u; run_no < RUNS; run_no++) {
        begin_run();
        for (step_no = 0u; step_no < STEPS; step_no++) {
            switch (rnd() % 9u) {
            case 0u: do_start(); break;
            case 1u: case 2u: do_poll(); break;
            case 3u: do_stop((uint8_t)(rnd() & 1u)); break;
            case 4u: do_evidence(); break;
            case 5u: do_after_update(); break;
            case 6u: do_rearm(); break;
            case 7u: set_field(rnd() % FIELDS, (rnd() % 3u) != 0u); break;
            default: set_field(rnd() % FIELDS, 1); break;
            }
            check_model();
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_runner_mutate_an_idle_runner_ignores_every_out_of_order_call);
    RUN_TEST(test_app_runner_mutate_failed_refused_and_stopped_ignore_every_call);
    RUN_TEST(test_app_runner_mutate_any_one_bad_field_is_refused_before_init);
    RUN_TEST(test_app_runner_mutate_random_call_orders_follow_the_model);
    return UNITY_END();
}
