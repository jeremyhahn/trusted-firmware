/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * boot/boot_main.c's main(), on the host (#163).
 *
 * main() is linked here under the name tfw_boot_main_ut (mk/boot.mk renames
 * it at compile time) against stand-ins for every unit it calls, all in this
 * file and none of them shared: main() is a sequence, and what is pinned is
 * the sequence -- which calls, in which order, with which arguments -- not
 * what the callees do, which their own suites pin.
 *
 * main() has two exits and returns through neither: the jump to the
 * application and the recovery halt. Each stand-in records how it was reached
 * and leaves main() through longjmp back into run_main(). A main() that
 * returns is a failure of its own.
 */
#include <setjmp.h>
#include <string.h>

#include "unity.h"

#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "boot_cdi.h"
#include "boot_hal.h"
#include "boot_measure.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "boot_request.h"
#include "boot_stage.h"
#include "boot_swap.h"
#include "boot_store.h"
#include "boot_tick.h"
#include "boot_trial.h"
#include "core/secure_zero.h"

int tfw_boot_main_ut(void);

/* ---- the record ---------------------------------------------------------- */

typedef enum {
    EV_REQUEST_TAKE = 1,
    EV_UART_INIT,
    EV_CDI_INIT,
    EV_PLATFORM_LOAD,
    EV_RECOVERY,
    EV_TRIAL_AT_RESET,
    EV_SWAP_AT_RESET,
    EV_STAGE_AFTER_RESET,
    EV_TRIAL_FAIL,
    EV_PLATFORM_STORE,
    EV_FLOOR_LOAD,
    EV_EVALUATE,
    EV_SETTLE,
    EV_STAGE_TAKE,
    EV_SESSION_OPEN,
    EV_SWAP_TAKE,
    EV_STAGE_FORGET,
    EV_FALL_BACK,
    EV_TRIAL_ARM,
    EV_WATCHDOG_ARM,
    EV_CDI_DERIVE,
    EV_HANDOFF_BUILD,
    EV_HANDOFF_WRITE,
    EV_ZERO,
    EV_JUMP
} ev_t;

enum { EXIT_NONE = 0, EXIT_JUMP = 1, EXIT_RECOVERY = 2, EXIT_RETURNED = 3 };

#define MAX_EV 64
#define MAX_EVAL 4

static jmp_buf exit_env;

/* What the stand-ins answer. Reset to a healthy device by setUp. */
static struct {
    uint8_t                 update;
    tfw_reset_cause_t       cause;
    tfw_boot_platform_t     plat;
    tfw_status_t            load_st;
    tfw_boot_reset_action_t trial;
    uint8_t                 swap_overrides;
    tfw_boot_reset_action_t swap_act;
    tfw_status_t            fail_st;
    uint32_t                floor;
    tfw_status_t            floor_st;
    tfw_status_t            eval_st[MAX_EVAL];
    uint32_t                version;
    tfw_status_t            settle_st;
    uint32_t                settled_floor;
    uint8_t                 stage_sets_state;
    uint8_t                 stage_state;
    uint8_t                 session_sets_state;
    uint8_t                 session_state;
    uint8_t                 swap_take_sets_ota;
    uint8_t                 swap_take_ota;
    tfw_status_t            fall_back_st;
    tfw_status_t            arm_st;
    tfw_status_t            derive_st;
    uint8_t                 derive_writes;
    uint8_t                 tier;
} cfg;

/* What the stand-ins saw. Cleared by setUp. */
static struct {
    int                       ev[MAX_EV];
    unsigned                  n;
    int                       exit_how;
    tfw_reset_cause_t         take_cause;
    uint32_t                  baud;
    tfw_status_t              recovery_reason;
    tfw_boot_platform_t       recovery_plat;
    tfw_boot_platform_t       stored_plat;
    tfw_boot_reset_action_t   swap_trial;
    tfw_boot_reset_action_t   after_trial;
    tfw_boot_reset_action_t   after_act;
    uint32_t                  eval_floor[MAX_EVAL];
    unsigned                  n_eval;
    uint32_t                  settle_version;
    uint32_t                  settle_floor_in;
    uint32_t                  stage_floor;
    uint32_t                  swap_floor;
    tfw_boot_session_limits_t lim;
    uint8_t                   derive_meas[TFW_BOOT_MEASURE_LEN];
    uint8_t                   built_tier;
    uint8_t                   built_cdi[TFW_BOOT_MEASURE_LEN];
    uint8_t                   built_meas[TFW_BOOT_MEASURE_LEN];
    uint32_t                  built_version;
    tfw_reset_cause_t         built_cause;
    const uint8_t            *cdi_at;
    const uint8_t            *block_at;
    uint8_t                   written[TFW_BOOT_HANDOFF_LEN];
    unsigned                  written_len;
    int                       cdi_zero_at_jump;
    int                       block_zero_at_jump;
} seen;

static void ev(ev_t e)
{
    TEST_ASSERT_TRUE_MESSAGE(seen.n < MAX_EV, "event log full: main() looping");
    seen.ev[seen.n++] = (int)e;
}

static int all_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static void fill_measurement(uint8_t *m)
{
    size_t i;

    for (i = 0; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i] = (uint8_t)(0x40u + i);
    }
}

/* ---- the stand-ins ------------------------------------------------------- */

uint8_t tfw_boot_request_take(tfw_reset_cause_t cause)
{
    ev(EV_REQUEST_TAKE);
    seen.take_cause = cause;
    return cfg.update;
}

tfw_reset_cause_t tfw_boot_reset_cause(void)
{
    return cfg.cause;
}

void tfw_boot_uart_init(uint32_t baud)
{
    ev(EV_UART_INIT);
    seen.baud = baud;
}

tfw_status_t tfw_boot_cdi_init(void)
{
    ev(EV_CDI_INIT);
    return TFW_ERR_NOT_PROVISIONED;   /* ignored by main(), by design */
}

void tfw_boot_platform_load(tfw_boot_platform_t *p, tfw_status_t *st)
{
    ev(EV_PLATFORM_LOAD);
    *p  = cfg.plat;
    *st = cfg.load_st;
}

_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason)
{
    ev(EV_RECOVERY);
    seen.recovery_reason = reason;
    seen.recovery_plat   = *plat;
    seen.exit_how        = EXIT_RECOVERY;
    longjmp(exit_env, EXIT_RECOVERY);
}

tfw_boot_reset_action_t tfw_boot_trial_at_reset(const tfw_boot_platform_t *p)
{
    (void)p;
    ev(EV_TRIAL_AT_RESET);
    return cfg.trial;
}

tfw_boot_reset_action_t tfw_boot_swap_at_reset(tfw_boot_platform_t *p,
                                               tfw_boot_reset_action_t act)
{
    (void)p;
    ev(EV_SWAP_AT_RESET);
    seen.swap_trial = act;
    return cfg.swap_overrides ? cfg.swap_act : act;
}

void tfw_boot_stage_after_reset(tfw_boot_reset_action_t trial, tfw_boot_reset_action_t act)
{
    ev(EV_STAGE_AFTER_RESET);
    seen.after_trial = trial;
    seen.after_act   = act;
}

tfw_status_t tfw_boot_trial_fail(tfw_boot_platform_t *p)
{
    ev(EV_TRIAL_FAIL);
    p->boot_state = TFW_BOOT_STATE_TRIAL;
    p->ota_state  = TFW_BOOT_OTA_FAILED;
    return cfg.fail_st;
}

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    ev(EV_PLATFORM_STORE);
    seen.stored_plat = *p;
}

tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out)
{
    (void)p;
    ev(EV_FLOOR_LOAD);
    *out = cfg.floor;
    return cfg.floor_st;
}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    unsigned k = seen.n_eval;

    ev(EV_EVALUATE);
    TEST_ASSERT_TRUE(k < MAX_EVAL);
    seen.eval_floor[k] = floor;
    seen.n_eval = k + 1u;
    memset(out, 0, sizeof *out);
    out->trailer.image_version = cfg.version;
    fill_measurement(out->measurement);
    return cfg.eval_st[k];
}

tfw_status_t tfw_boot_settle(tfw_boot_platform_t *p, uint32_t version, uint32_t *floor)
{
    (void)p;
    ev(EV_SETTLE);
    seen.settle_version  = version;
    seen.settle_floor_in = *floor;
    if (cfg.settle_st == TFW_OK) {
        *floor = cfg.settled_floor;
    }
    return cfg.settle_st;
}

tfw_status_t tfw_boot_stage_take(tfw_boot_platform_t *p, uint32_t floor)
{
    ev(EV_STAGE_TAKE);
    seen.stage_floor = floor;
    if (cfg.stage_sets_state) {
        p->boot_state = cfg.stage_state;
    }
    return TFW_ERR_UNSUPPORTED;   /* ignored by main(), by design */
}

void tfw_boot_session_open(tfw_boot_platform_t *plat, const tfw_boot_session_limits_t *lim)
{
    ev(EV_SESSION_OPEN);
    seen.lim = *lim;
    if (cfg.session_sets_state) {
        plat->boot_state = cfg.session_state;
    }
}

tfw_status_t tfw_boot_swap_take(tfw_boot_platform_t *p, uint32_t floor)
{
    ev(EV_SWAP_TAKE);
    seen.swap_floor = floor;
    if (cfg.swap_take_sets_ota) {
        p->ota_state = cfg.swap_take_ota;
    }
    return TFW_ERR_BAD_STATE;     /* ignored by main(), by design */
}

tfw_status_t tfw_boot_stage_forget(void)
{
    ev(EV_STAGE_FORGET);
    return TFW_ERR_UNSUPPORTED;   /* ignored by main(), by design */
}

tfw_status_t tfw_boot_swap_fall_back(tfw_boot_platform_t *p)
{
    (void)p;
    ev(EV_FALL_BACK);
    return cfg.fall_back_st;
}

tfw_status_t tfw_boot_trial_arm(tfw_boot_platform_t *p)
{
    ev(EV_TRIAL_ARM);
    if (cfg.arm_st == TFW_OK) {
        p->boot_state = TFW_BOOT_STATE_TRIAL;
    }
    return cfg.arm_st;
}

void tfw_boot_watchdog_arm(void)
{
    ev(EV_WATCHDOG_ARM);
}

tfw_status_t tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                 uint8_t cdi[TFW_BOOT_MEASURE_LEN])
{
    ev(EV_CDI_DERIVE);
    memcpy(seen.derive_meas, measurement, TFW_BOOT_MEASURE_LEN);
    if (cfg.derive_writes) {
        memset(cdi, cfg.derive_st == TFW_OK ? 0xC5 : 0xEE, TFW_BOOT_MEASURE_LEN);
    }
    return cfg.derive_st;
}

uint8_t tfw_boot_cdi_tier(void)
{
    return cfg.tier;
}

void tfw_boot_handoff_build(uint8_t *block, uint8_t tier,
                            const uint8_t cdi[TFW_BOOT_MEASURE_LEN],
                            const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                            uint32_t image_version, tfw_reset_cause_t reset_cause)
{
    ev(EV_HANDOFF_BUILD);
    seen.built_tier    = tier;
    seen.built_version = image_version;
    seen.built_cause   = reset_cause;
    seen.cdi_at        = cdi;
    seen.block_at      = block;
    memcpy(seen.built_cdi, cdi, TFW_BOOT_MEASURE_LEN);
    memcpy(seen.built_meas, measurement, TFW_BOOT_MEASURE_LEN);
    memset(block, 0xB7, TFW_BOOT_HANDOFF_LEN);
}

/* #173's DeviceID seed: no tier here has one, so the block keeps the all-zero
 * seed tfw_boot_handoff_build wrote and set_seed is never reached. Neither
 * records an event: the sequences this suite pins are #163's. */
tfw_status_t tfw_boot_cdi_device_seed(uint8_t seed[TFW_BOOT_SEED_LEN])
{
    (void)seed;
    return TFW_ERR_NOT_PROVISIONED;
}

void tfw_boot_handoff_set_seed(uint8_t *block, const uint8_t seed[TFW_BOOT_SEED_LEN])
{
    (void)block;
    (void)seed;
    TEST_FAIL_MESSAGE("set_seed with no seed derived");
}

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    ev(EV_HANDOFF_WRITE);
    seen.written_len = len;
    memcpy(seen.written, block, len <= TFW_BOOT_HANDOFF_LEN ? len : TFW_BOOT_HANDOFF_LEN);
}

void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *q = (volatile uint8_t *)p;

    ev(EV_ZERO);
    while (n--) {
        *q++ = 0u;
    }
}

void tfw_boot_jump_application(void)
{
    ev(EV_JUMP);
    /* hand_over's frame is still live here: what it leaves behind is what
     * the application's stack would inherit. */
    seen.cdi_zero_at_jump   = seen.cdi_at != NULL && all_zero(seen.cdi_at, TFW_BOOT_MEASURE_LEN);
    seen.block_zero_at_jump = seen.block_at != NULL && all_zero(seen.block_at, TFW_BOOT_HANDOFF_LEN);
    seen.exit_how           = EXIT_JUMP;
    longjmp(exit_env, EXIT_JUMP);
}

/* ---- the harness --------------------------------------------------------- */

/* Leaves a recognisable pattern where main()'s frames are about to be, so a
 * local that is read before it is written reads 0xA5, not a lucky zero. */
static void __attribute__((noinline)) poison_stack(void)
{
    volatile uint8_t junk[4096];
    size_t i;

    for (i = 0; i < sizeof junk; i++) {
        junk[i] = 0xA5u;
    }
}

static int run_main(void)
{
    poison_stack();
    if (setjmp(exit_env) == 0) {
        (void)tfw_boot_main_ut();
        seen.exit_how = EXIT_RETURNED;
    }
    return seen.exit_how;
}

static void assert_events(const int *want, size_t n)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE((unsigned)n, seen.n, "number of calls main() made");
    TEST_ASSERT_EQUAL_INT_ARRAY(want, seen.ev, (uint32_t)n);
}

static unsigned count(ev_t e)
{
    unsigned i, c = 0;

    for (i = 0; i < seen.n; i++) {
        if (seen.ev[i] == (int)e) {
            c++;
        }
    }
    return c;
}

/* Index of the first e in the log, or MAX_EV when there is none. */
static unsigned first(ev_t e)
{
    unsigned i;

    for (i = 0; i < seen.n; i++) {
        if (seen.ev[i] == (int)e) {
            return i;
        }
    }
    return MAX_EV;
}

static void assert_recovery(tfw_status_t reason)
{
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, seen.exit_how);
    TEST_ASSERT_EQUAL_INT(reason, seen.recovery_reason);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_JUMP));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_TRIAL_ARM));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_WATCHDOG_ARM));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_CDI_DERIVE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_HANDOFF_WRITE));
}

void setUp(void)
{
    unsigned k;

    memset(&cfg, 0, sizeof cfg);
    memset(&seen, 0, sizeof seen);
    cfg.cause                = TFW_RESET_EXTERNAL;
    memset(&cfg.plat, 0x11, sizeof cfg.plat);
    cfg.plat.boot_state      = TFW_BOOT_STATE_GOOD;
    cfg.plat.ota_state       = TFW_BOOT_OTA_IDLE;
    cfg.plat.rollback_floor  = 0x0BADF100u;   /* never the floor main() uses */
    cfg.load_st              = TFW_OK;
    cfg.trial                = TFW_BOOT_RESET_BOOT;
    cfg.fail_st              = TFW_OK;
    cfg.floor                = 7u;
    cfg.floor_st             = TFW_OK;
    for (k = 0; k < MAX_EVAL; k++) {
        cfg.eval_st[k] = TFW_OK;
    }
    cfg.version              = 9u;
    cfg.settle_st            = TFW_OK;
    cfg.settled_floor        = 9u;
    cfg.fall_back_st         = TFW_ERR_BAD_STATE;
    cfg.arm_st               = TFW_ERR_BAD_STATE;   /* nothing pending */
    cfg.derive_st            = TFW_OK;
    cfg.derive_writes        = 1u;
    cfg.tier                 = 2u;
}

void tearDown(void)
{
}

/* ---- the jump ------------------------------------------------------------ */

void test_a_good_image_jumps_after_evaluate_and_the_cdi_in_that_order(void)
{
    static const int want[] = {
        EV_REQUEST_TAKE, EV_UART_INIT, EV_CDI_INIT, EV_PLATFORM_LOAD,
        EV_TRIAL_AT_RESET, EV_SWAP_AT_RESET, EV_STAGE_AFTER_RESET,
        EV_FLOOR_LOAD, EV_STAGE_TAKE, EV_SWAP_TAKE, EV_EVALUATE, EV_TRIAL_ARM,
        EV_ZERO, EV_CDI_DERIVE, EV_HANDOFF_BUILD, EV_HANDOFF_WRITE,
        EV_ZERO, EV_ZERO, EV_JUMP
    };
    uint8_t meas[TFW_BOOT_MEASURE_LEN];

    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    assert_events(want, sizeof want / sizeof want[0]);

    fill_measurement(meas);
    TEST_ASSERT_EQUAL_INT(TFW_RESET_EXTERNAL, seen.take_cause);
    TEST_ASSERT_EQUAL_UINT32(TFW_UART_BAUD, seen.baud);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, seen.derive_meas, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, seen.built_meas, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EACH_EQUAL_UINT8(0xC5u, seen.built_cdi, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8(2u, seen.built_tier);
    TEST_ASSERT_EQUAL_UINT32(9u, seen.built_version);
    TEST_ASSERT_EQUAL_INT(TFW_RESET_EXTERNAL, seen.built_cause);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_HANDOFF_LEN, seen.written_len);
    TEST_ASSERT_EACH_EQUAL_UINT8(0xB7u, seen.written, TFW_BOOT_HANDOFF_LEN);
}

void test_the_floor_loaded_is_the_one_every_check_is_made_against(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT32(7u, seen.stage_floor);
    TEST_ASSERT_EQUAL_UINT32(7u, seen.swap_floor);
    TEST_ASSERT_EQUAL_UINT(1u, seen.n_eval);
    TEST_ASSERT_EQUAL_UINT32(7u, seen.eval_floor[0]);
}

void test_the_cdi_and_the_block_are_zero_when_the_application_starts(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_TRUE(seen.cdi_zero_at_jump);
    TEST_ASSERT_TRUE(seen.block_zero_at_jump);
}

void test_a_cdi_the_derive_wrote_and_then_refused_is_handed_over_as_zero(void)
{
    cfg.derive_st = TFW_ERR_NOT_PROVISIONED;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, seen.built_cdi, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_TRUE(seen.cdi_zero_at_jump);
}

void test_a_cdi_the_derive_never_wrote_is_handed_over_as_zero(void)
{
    cfg.derive_st     = TFW_ERR_NOT_PROVISIONED;
    cfg.derive_writes = 0u;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, seen.built_cdi, TFW_BOOT_MEASURE_LEN);
}

void test_a_pending_image_is_armed_stored_and_watched_before_the_jump(void)
{
    static const int tail[] = {
        EV_EVALUATE, EV_TRIAL_ARM, EV_PLATFORM_STORE, EV_WATCHDOG_ARM, EV_ZERO
    };

    cfg.trial  = TFW_BOOT_RESET_TRY;
    cfg.arm_st = TFW_OK;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_INT_ARRAY(tail, &seen.ev[first(EV_EVALUATE)],
                                (uint32_t)(sizeof tail / sizeof tail[0]));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, seen.stored_plat.boot_state);
}

void test_nothing_armed_stores_nothing_and_arms_no_watchdog(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_PLATFORM_STORE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_WATCHDOG_ARM));
}

/* ---- the update request -------------------------------------------------- */

void test_the_request_is_taken_first_with_the_hals_reset_cause(void)
{
    cfg.cause = TFW_RESET_WATCHDOG;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_INT(EV_REQUEST_TAKE, seen.ev[0]);
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, seen.take_cause);
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, seen.built_cause);
}

void test_a_request_opens_one_session_after_the_stage_and_before_the_swap(void)
{
    cfg.update = 1u;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_SESSION_OPEN));
    TEST_ASSERT_EQUAL_UINT(first(EV_STAGE_TAKE) + 1u, first(EV_SESSION_OPEN));
    TEST_ASSERT_EQUAL_UINT(first(EV_SESSION_OPEN) + 1u, first(EV_SWAP_TAKE));
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_SESSION_IDLE_POLLS(TFW_BOOT_TICK_HZ), seen.lim.idle_polls);
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_SESSION_IDLE_BYTES(TFW_UART_BAUD), seen.lim.idle_bytes);
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_SESSION_MAX_POLLS(TFW_BOOT_TICK_HZ), seen.lim.max_polls);
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_SESSION_MAX_BYTES, seen.lim.max_bytes);
}

void test_no_request_opens_no_session(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SESSION_OPEN));
}

/* ---- the platform block -------------------------------------------------- */

void test_no_usable_region_halts_in_recovery_with_a_zeroed_block(void)
{
    static const int want[] = {
        EV_REQUEST_TAKE, EV_UART_INIT, EV_CDI_INIT, EV_PLATFORM_LOAD, EV_RECOVERY
    };

    cfg.load_st = TFW_ERR_NOT_PROVISIONED;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_events(want, sizeof want / sizeof want[0]);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, seen.recovery_reason);
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, (const uint8_t *)&seen.recovery_plat,
                                 (uint32_t)sizeof seen.recovery_plat);
}

/* ---- the one trial ------------------------------------------------------- */

void test_the_reset_action_passes_from_the_trial_to_the_swap_and_the_stage(void)
{
    cfg.trial          = TFW_BOOT_RESET_SETTLE;
    cfg.swap_overrides = 1u;
    cfg.swap_act       = TFW_BOOT_RESET_BOOT;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_SETTLE, seen.swap_trial);
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_SETTLE, seen.after_trial);
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_BOOT, seen.after_act);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SETTLE));   /* the swap's action rules */
}

void test_a_trial_still_running_at_reset_fails_stores_and_halts(void)
{
    static const int want[] = {
        EV_REQUEST_TAKE, EV_UART_INIT, EV_CDI_INIT, EV_PLATFORM_LOAD,
        EV_TRIAL_AT_RESET, EV_SWAP_AT_RESET, EV_STAGE_AFTER_RESET,
        EV_TRIAL_FAIL, EV_PLATFORM_STORE, EV_RECOVERY
    };

    cfg.trial = TFW_BOOT_RESET_FAIL;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_events(want, sizeof want / sizeof want[0]);
    assert_recovery(TFW_ERR_BAD_STATE);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, seen.stored_plat.ota_state);
}

void test_a_failed_trial_the_swap_falls_back_from_boots_the_previous_image(void)
{
    cfg.trial          = TFW_BOOT_RESET_FAIL;
    cfg.swap_overrides = 1u;
    cfg.swap_act       = TFW_BOOT_RESET_BOOT;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_TRIAL_FAIL));
}

void test_a_trial_fail_that_writes_nothing_stores_nothing(void)
{
    cfg.trial   = TFW_BOOT_RESET_FAIL;
    cfg.fail_st = TFW_ERR_BAD_STATE;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_PLATFORM_STORE));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, seen.recovery_reason);
}

/* ---- the floor ----------------------------------------------------------- */

void test_an_unreadable_floor_halts_before_anything_is_evaluated(void)
{
    cfg.floor_st = TFW_ERR_TIMEOUT;   /* the element did not answer */
    cfg.update   = 1u;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_EVALUATE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_STAGE_TAKE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SESSION_OPEN));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_PLATFORM_STORE));
}

void test_a_confirmed_trial_settles_its_version_and_checks_the_new_floor(void)
{
    cfg.trial  = TFW_BOOT_RESET_SETTLE;
    cfg.update = 1u;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(first(EV_FLOOR_LOAD) + 1u, first(EV_EVALUATE));
    TEST_ASSERT_EQUAL_UINT(first(EV_EVALUATE) + 1u, first(EV_SETTLE));
    TEST_ASSERT_EQUAL_UINT32(9u, seen.settle_version);
    TEST_ASSERT_EQUAL_UINT32(7u, seen.settle_floor_in);
    TEST_ASSERT_EQUAL_UINT32(7u, seen.eval_floor[0]);
    TEST_ASSERT_EQUAL_UINT(2u, seen.n_eval);
    TEST_ASSERT_EQUAL_UINT32(9u, seen.eval_floor[1]);
    TEST_ASSERT_EQUAL_UINT32(9u, seen.stage_floor);
    TEST_ASSERT_EQUAL_UINT32(9u, seen.swap_floor);
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_SESSION_OPEN));
}

void test_a_settle_the_authority_refuses_drops_the_request(void)
{
    cfg.trial     = TFW_BOOT_RESET_SETTLE;
    cfg.update    = 1u;
    cfg.settle_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SESSION_OPEN));
    TEST_ASSERT_EQUAL_UINT32(7u, seen.eval_floor[1]);
}

void test_a_confirmed_image_that_no_longer_verifies_halts_before_the_settle(void)
{
    cfg.trial      = TFW_BOOT_RESET_SETTLE;
    cfg.eval_st[0] = TFW_ERR_RANGE;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_RANGE);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SETTLE));
}

/* ---- a torn write -------------------------------------------------------- */

void test_a_staged_block_halts_without_an_evaluate(void)
{
    cfg.plat.boot_state = TFW_BOOT_STATE_STAGED;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_EVALUATE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_SWAP_TAKE));
}

void test_a_copy_the_stage_left_staged_halts(void)
{
    cfg.stage_sets_state = 1u;
    cfg.stage_state      = TFW_BOOT_STATE_STAGED;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_CRYPTO);
}

void test_a_session_that_left_staged_halts(void)
{
    cfg.update             = 1u;
    cfg.session_sets_state = 1u;
    cfg.session_state      = TFW_BOOT_STATE_STAGED;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_CRYPTO);
}

void test_a_stage_that_copied_an_image_goes_on_to_the_evaluate(void)
{
    cfg.plat.boot_state  = TFW_BOOT_STATE_STAGED;
    cfg.stage_sets_state = 1u;
    cfg.stage_state      = TFW_BOOT_STATE_GOOD;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
}

void test_an_erased_block_is_not_staged_and_boots(void)
{
    cfg.plat.boot_state = TFW_BOOT_STATE_EMPTY;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
}

/* ---- every refusal ------------------------------------------------------- */

static void refused(tfw_status_t st)
{
    setUp();
    cfg.eval_st[0] = st;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(st);
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_FALL_BACK));
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_TRIAL_FAIL));
    TEST_ASSERT_EQUAL_UINT(first(EV_FALL_BACK) + 1u, first(EV_TRIAL_FAIL));
    TEST_ASSERT_EQUAL_UINT(first(EV_TRIAL_FAIL) + 1u, first(EV_PLATFORM_STORE));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_STAGE_FORGET));
}

void test_every_refused_image_ends_its_trial_and_halts(void)
{
    refused(TFW_ERR_CRYPTO);          /* a bad signature */
    refused(TFW_ERR_RANGE);           /* under the floor */
    refused(TFW_ERR_INTERNAL);        /* the measure failed */
    refused(TFW_ERR_NOT_PROVISIONED); /* an erased slot, no trailer */
    refused(TFW_ERR_BAD_STATE);
}

void test_a_refused_pending_image_is_forgotten_from_the_store(void)
{
    cfg.plat.ota_state = TFW_BOOT_OTA_PENDING;
    cfg.eval_st[0]     = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_STAGE_FORGET));
    TEST_ASSERT_EQUAL_UINT(first(EV_EVALUATE) + 1u, first(EV_STAGE_FORGET));
}

void test_the_forget_follows_the_ota_state_the_swap_left(void)
{
    cfg.plat.ota_state      = TFW_BOOT_OTA_IDLE;
    cfg.swap_take_sets_ota  = 1u;
    cfg.swap_take_ota       = TFW_BOOT_OTA_PENDING;
    cfg.eval_st[0]          = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_STAGE_FORGET));
}

void test_a_verified_pending_image_is_not_forgotten(void)
{
    cfg.plat.ota_state = TFW_BOOT_OTA_PENDING;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_STAGE_FORGET));
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_FALL_BACK));
}

void test_a_refusal_the_swap_falls_back_from_boots_the_previous_image(void)
{
    cfg.eval_st[0]   = TFW_ERR_CRYPTO;
    cfg.fall_back_st = TFW_OK;
    TEST_ASSERT_EQUAL_INT(EXIT_JUMP, run_main());
    TEST_ASSERT_EQUAL_UINT(2u, seen.n_eval);
    TEST_ASSERT_EQUAL_UINT32(7u, seen.eval_floor[1]);
    TEST_ASSERT_EQUAL_UINT(first(EV_FALL_BACK) + 1u, first(EV_EVALUATE) + 2u);
    TEST_ASSERT_EQUAL_UINT(0u, count(EV_TRIAL_FAIL));
}

void test_a_previous_image_that_is_refused_too_halts_with_its_reason(void)
{
    cfg.eval_st[0]   = TFW_ERR_CRYPTO;
    cfg.eval_st[1]   = TFW_ERR_RANGE;
    cfg.fall_back_st = TFW_OK;
    TEST_ASSERT_EQUAL_INT(EXIT_RECOVERY, run_main());
    assert_recovery(TFW_ERR_RANGE);
    TEST_ASSERT_EQUAL_UINT(1u, count(EV_TRIAL_FAIL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_image_jumps_after_evaluate_and_the_cdi_in_that_order);
    RUN_TEST(test_the_floor_loaded_is_the_one_every_check_is_made_against);
    RUN_TEST(test_the_cdi_and_the_block_are_zero_when_the_application_starts);
    RUN_TEST(test_a_cdi_the_derive_wrote_and_then_refused_is_handed_over_as_zero);
    RUN_TEST(test_a_cdi_the_derive_never_wrote_is_handed_over_as_zero);
    RUN_TEST(test_a_pending_image_is_armed_stored_and_watched_before_the_jump);
    RUN_TEST(test_nothing_armed_stores_nothing_and_arms_no_watchdog);
    RUN_TEST(test_the_request_is_taken_first_with_the_hals_reset_cause);
    RUN_TEST(test_a_request_opens_one_session_after_the_stage_and_before_the_swap);
    RUN_TEST(test_no_request_opens_no_session);
    RUN_TEST(test_no_usable_region_halts_in_recovery_with_a_zeroed_block);
    RUN_TEST(test_the_reset_action_passes_from_the_trial_to_the_swap_and_the_stage);
    RUN_TEST(test_a_trial_still_running_at_reset_fails_stores_and_halts);
    RUN_TEST(test_a_failed_trial_the_swap_falls_back_from_boots_the_previous_image);
    RUN_TEST(test_a_trial_fail_that_writes_nothing_stores_nothing);
    RUN_TEST(test_an_unreadable_floor_halts_before_anything_is_evaluated);
    RUN_TEST(test_a_confirmed_trial_settles_its_version_and_checks_the_new_floor);
    RUN_TEST(test_a_settle_the_authority_refuses_drops_the_request);
    RUN_TEST(test_a_confirmed_image_that_no_longer_verifies_halts_before_the_settle);
    RUN_TEST(test_a_staged_block_halts_without_an_evaluate);
    RUN_TEST(test_a_copy_the_stage_left_staged_halts);
    RUN_TEST(test_a_session_that_left_staged_halts);
    RUN_TEST(test_a_stage_that_copied_an_image_goes_on_to_the_evaluate);
    RUN_TEST(test_an_erased_block_is_not_staged_and_boots);
    RUN_TEST(test_every_refused_image_ends_its_trial_and_halts);
    RUN_TEST(test_a_refused_pending_image_is_forgotten_from_the_store);
    RUN_TEST(test_the_forget_follows_the_ota_state_the_swap_left);
    RUN_TEST(test_a_verified_pending_image_is_not_forgotten);
    RUN_TEST(test_a_refusal_the_swap_falls_back_from_boots_the_previous_image);
    RUN_TEST(test_a_previous_image_that_is_refused_too_halts_with_its_reason);
    return UNITY_END();
}
