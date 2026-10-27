/*
 * src/main.c's main() on the host (#163).
 *
 * The application image's entry point is built with -Dmain=tfw_app_main_ut
 * and linked against the stand-ins below, one test wide, in place of every
 * function it calls. Each stand-in records an event; the superloop never
 * returns, so tfw_ota_trial_poll, the last call of a pass, counts the passes
 * and leaves through longjmp after the number a test asks for. Nothing waits
 * on a clock.
 *
 * Pinned: the start-up order, interrupts enabled only after all of it and
 * before the boot event, the boot event on the transport every other call
 * gets, the order inside one pass, and the framer's two counters handed to
 * the SYS capability each pass, unswapped.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "proto/transport.h"
#include "caps/sys.h"
#include "caps/sched.h"
#include "caps/bus.h"
#include "core/handoff.h"
#include "core/platform_id.h"
#include "core/auth_session.h"
#include "core/app_runner.h"
#include "core/hsm_loop.h"
#include "core/ota_trial.h"
#include "core/ota_stage.h"

int tfw_app_main_ut(void);
tfw_status_t tfw_hal_tick_init(void);

enum ev {
    EV_HAL_INIT = 1, EV_TICK_INIT, EV_UART_INIT, EV_ADC_INIT, EV_BUS_INIT,
    EV_STAGE_INIT, EV_PLATFORM_ID_INIT, EV_HANDOFF_INIT, EV_CRYPTO_INIT,
    EV_SCHED_INIT, EV_AUTH_INIT, EV_TRANSPORT_INIT, EV_TRIAL_START,
    EV_APP_START, EV_TRIAL_AFTER_INIT, EV_IRQ_ENABLE, EV_SEND_EVENT,
    EV_TRANSPORT_POLL, EV_FRAMER_ERRORS, EV_SCHED_POLL, EV_SYS_POLL,
    EV_APP_POLL, EV_HSM_POLL, EV_TRIAL_POLL
};

#define MAX_EVENTS 128u
#define MAX_PASSES 8u

static struct {
    uint8_t ev[MAX_EVENTS];
    uint8_t n;
    uint32_t baud;
    uint8_t event_id;
    uint8_t sends;
    const tfw_transport_t *init_t;
    const tfw_transport_t *send_t;
    const tfw_transport_t *poll_t[MAX_PASSES];
    uint16_t crc[MAX_PASSES];
    uint16_t fmt[MAX_PASSES];
    uint8_t polls;
    uint8_t passes;
    uint8_t stop_after;
} seen;

static jmp_buf out;

static void log_ev(enum ev e)
{
    TEST_ASSERT_TRUE_MESSAGE(seen.n < MAX_EVENTS, "event log full");
    seen.ev[seen.n++] = (uint8_t)e;
}

/* ---------------------------------------------------------- stand-ins */

tfw_status_t tfw_hal_init(void) { log_ev(EV_HAL_INIT); return TFW_OK; }
tfw_status_t tfw_hal_tick_init(void) { log_ev(EV_TICK_INIT); return TFW_OK; }
tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    log_ev(EV_UART_INIT);
    seen.baud = baud;
    return TFW_OK;
}
tfw_status_t tfw_hal_adc_init(void) { log_ev(EV_ADC_INIT); return TFW_OK; }
void tfw_bus_init(void) { log_ev(EV_BUS_INIT); }
void tfw_stage_init(void) { log_ev(EV_STAGE_INIT); }
void tfw_platform_id_init(void) { log_ev(EV_PLATFORM_ID_INIT); }
void tfw_boot_handoff_init(void) { log_ev(EV_HANDOFF_INIT); }
tfw_status_t tfw_crypto_init(void) { log_ev(EV_CRYPTO_INIT); return TFW_OK; }
void tfw_sched_init(void) { log_ev(EV_SCHED_INIT); }
void tfw_auth_session_init(void) { log_ev(EV_AUTH_INIT); }
void tfw_transport_init(tfw_transport_t *t)
{
    log_ev(EV_TRANSPORT_INIT);
    seen.init_t = t;
}
void tfw_ota_trial_start(void) { log_ev(EV_TRIAL_START); }
void tfw_app_runner_start(void) { log_ev(EV_APP_START); }
void tfw_ota_trial_after_init(void) { log_ev(EV_TRIAL_AFTER_INIT); }
void tfw_hal_irq_enable(void) { log_ev(EV_IRQ_ENABLE); }

uint8_t tfw_transport_send_event(tfw_transport_t *t, uint8_t event_id)
{
    log_ev(EV_SEND_EVENT);
    seen.send_t = t;
    seen.event_id = event_id;
    seen.sends++;
    return 0u;
}

/* Each pass leaves distinct counters behind, crc and format apart, so a
 * swap or a stale read shows. */
void tfw_transport_poll(tfw_transport_t *t)
{
    log_ev(EV_TRANSPORT_POLL);
    TEST_ASSERT_TRUE(seen.polls < MAX_PASSES);
    seen.poll_t[seen.polls] = t;
    t->framer.err_crc = (uint16_t)(0x1100u + seen.polls);
    t->framer.err_format = (uint16_t)(0x2200u + seen.polls);
    seen.polls++;
}

void tfw_sys_set_framer_errors(uint16_t err_crc, uint16_t err_format)
{
    log_ev(EV_FRAMER_ERRORS);
    TEST_ASSERT_TRUE(seen.passes < MAX_PASSES);
    seen.crc[seen.passes] = err_crc;
    seen.fmt[seen.passes] = err_format;
}

void tfw_sched_poll(void) { log_ev(EV_SCHED_POLL); }
void tfw_sys_poll(void) { log_ev(EV_SYS_POLL); }
void tfw_app_runner_poll(void) { log_ev(EV_APP_POLL); }

void tfw_hsm_loop_poll(void) { log_ev(EV_HSM_POLL); }

void tfw_ota_trial_poll(void)
{
    log_ev(EV_TRIAL_POLL);
    seen.passes++;
    if (seen.passes >= seen.stop_after) {
        longjmp(out, 1);
    }
}

/* ---------------------------------------------------------- helpers */

static void run_main(uint8_t passes)
{
    memset(&seen, 0, sizeof seen);
    seen.stop_after = passes;
    if (setjmp(out) == 0) {
        (void)tfw_app_main_ut();
        TEST_FAIL_MESSAGE("main() returned");
    }
}

static const uint8_t startup_order[] = {
    EV_HAL_INIT, EV_TICK_INIT, EV_UART_INIT, EV_ADC_INIT, EV_BUS_INIT,
    EV_STAGE_INIT, EV_PLATFORM_ID_INIT, EV_HANDOFF_INIT, EV_CRYPTO_INIT,
    EV_SCHED_INIT, EV_AUTH_INIT, EV_TRANSPORT_INIT, EV_TRIAL_START,
    EV_APP_START, EV_TRIAL_AFTER_INIT, EV_IRQ_ENABLE, EV_SEND_EVENT
};

static const uint8_t pass_order[] = {
    EV_TRANSPORT_POLL, EV_FRAMER_ERRORS, EV_SCHED_POLL, EV_SYS_POLL,
    EV_APP_POLL, EV_HSM_POLL, EV_TRIAL_POLL
};

#define N_STARTUP ((uint8_t)sizeof startup_order)
#define N_PASS ((uint8_t)sizeof pass_order)

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------- tests */

static void test_startup_runs_in_order_once(void)
{
    run_main(1u);
    TEST_ASSERT_EQUAL_UINT8(N_STARTUP + N_PASS, seen.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(startup_order, seen.ev, (uint32_t)N_STARTUP);
}

static void test_uart_opens_at_the_configured_baud(void)
{
    run_main(1u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)TFW_UART_BAUD, seen.baud);
}

static void test_boot_event_sent_once_on_the_transport(void)
{
    run_main(3u);
    TEST_ASSERT_EQUAL_UINT8(1u, seen.sends);
    TEST_ASSERT_EQUAL_UINT8(TFW_EVENT_BOOT, seen.event_id);
    TEST_ASSERT_NOT_NULL(seen.init_t);
    TEST_ASSERT_EQUAL_PTR(seen.init_t, seen.send_t);
}

static void test_each_pass_runs_in_order(void)
{
    uint8_t p;

    run_main(4u);
    TEST_ASSERT_EQUAL_UINT8(4u, seen.passes);
    TEST_ASSERT_EQUAL_UINT8(N_STARTUP + 4u * N_PASS, seen.n);
    for (p = 0u; p < 4u; p++) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(pass_order,
                                      &seen.ev[N_STARTUP + p * N_PASS],
                                      (uint32_t)N_PASS);
    }
}

static void test_every_pass_polls_the_same_transport(void)
{
    uint8_t p;

    run_main(3u);
    TEST_ASSERT_EQUAL_UINT8(3u, seen.polls);
    for (p = 0u; p < 3u; p++) {
        TEST_ASSERT_EQUAL_PTR(seen.init_t, seen.poll_t[p]);
    }
}

static void test_framer_counters_reach_sys_each_pass_unswapped(void)
{
    uint8_t p;

    run_main(3u);
    for (p = 0u; p < 3u; p++) {
        TEST_ASSERT_EQUAL_HEX16(0x1100u + p, seen.crc[p]);
        TEST_ASSERT_EQUAL_HEX16(0x2200u + p, seen.fmt[p]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_startup_runs_in_order_once);
    RUN_TEST(test_uart_opens_at_the_configured_baud);
    RUN_TEST(test_boot_event_sent_once_on_the_transport);
    RUN_TEST(test_each_pass_runs_in_order);
    RUN_TEST(test_every_pass_polls_the_same_transport);
    RUN_TEST(test_framer_counters_reach_sys_each_pass_unswapped);
    return UNITY_END();
}
