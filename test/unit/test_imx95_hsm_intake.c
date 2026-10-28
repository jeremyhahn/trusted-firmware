/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's signal intake (src/hal/cortex-m/imx95/hsm_intake.c; #50
 * slice 10) through the superloop's pass (src/core/hsm_loop.c): each event is
 * injected as the hardware would bring it, S1 as the System Manager's
 * notification header, S5 as the pin's interrupt, S2 as an event the model's
 * enclave answers hsm_get_event with, and the loop is seen raising its signal
 * on the engine. The engine is stubbed to record the signals.
 *
 * Every id and the event layout below are test values: the board's are zero
 * until IMX95SRM, IMX95RM and imx-secure-enclave name them (board_profile.h).
 * S2 by polling is architecture question 6 (a), unconfirmed until the slice 10
 * bench test.
 */

#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "core/hsm.h"
#include "core/hsm_cmd.h"
#include "core/hsm_loop.h"
#include "core/hsm_port.h"
#include "tfw/hal.h"
#include "hsm_intake.h"
#include "scmi.h"

#define ELE      0x60200000u
#define ELE_MSGS 8u
#define EVT_CMD  0x9Au
#define S1_PROTO 0x80u
#define S1_MSG   0x05u
#define S2_EVENT 0x0000A5A5u

#define HDR(type, proto, msg) (((uint32_t)(proto) << 10) | ((uint32_t)(type) << 8) | (msg))

/* A two-word request; a six-word response, its count in word 2, events from
 * word 3, as test_imx95_ele_mu's evt6. */
static const struct tfw_ele_event_layout evt6 = { EVT_CMD, 2u, 6u, 1u, 2u, 3u };

static struct tfw_imx95_model           *m;
static struct tfw_ele_chan               ele;
static struct tfw_ele_event_layout       evt;
static struct tfw_imx95_hsm_intake_ids   ids;
static struct tfw_imx95_hsm_intake       in;

/* The engine, stubbed: the signals the loop raised. */
static tfw_hsm_t engine;
static uint8_t   raised[8];
static uint32_t  n_raised;

tfw_hsm_t *tfw_hsm_cmd_engine(uint32_t now)
{
    (void)now;
    return &engine;
}

void tfw_hsm_signal(tfw_hsm_t *h, uint8_t signal, uint32_t now)
{
    (void)now;
    TEST_ASSERT_EQUAL_PTR(&engine, h);
    if (n_raised < sizeof raised) {
        raised[n_raised] = signal;
    }
    n_raised++;
}

tfw_ms_t tfw_hal_millis(void)
{
    return 0u;
}

/* The port's sense on the test's state and ids, as hsm_port.c's on the
 * board's. */
uint8_t tfw_hsm_port_sense(void)
{
    return (uint8_t)(tfw_imx95_hsm_intake_sense(&in) |
                     tfw_imx95_hsm_poll(&in, &ele, &evt, &ids, 1u));
}

/* The enclave: SESSION_OPEN answered with a fresh handle, hsm_get_event with
 * `events`, and `fail_status` in place of success to request `fail_at`. */
static uint32_t ele_cmd[ELE_MSGS];
static uint32_t ele_w1[ELE_MSGS];
static uint32_t ele_n;
static uint32_t fail_at;
static uint32_t fail_status;
static uint32_t events[2];
static uint32_t n_events;

static void enclave(struct tfw_imx95_model *mm)
{
    uint32_t cmd = (mm->ele.req[0] >> 16) & 0xFFu;
    uint32_t n   = cmd == TFW_ELE_SESSION_OPEN ? TFW_ELE_SESSION_OPEN_RSP_WORDS : 6u;

    if (ele_n < ELE_MSGS) {
        ele_cmd[ele_n] = cmd;
        ele_w1[ele_n]  = mm->ele.req[1];
    }
    ele_n++;
    mm->ele.req_len = 0u;
    mm->ele.rsp_pos = 0u;
    mm->ele.rsp[0]  = 0xE1000006u | (cmd << 16) | (n << 8);
    mm->ele.rsp[1]  = (ele_n == fail_at) ? fail_status : 0xD6u;
    if (cmd == TFW_ELE_SESSION_OPEN) {
        mm->ele.rsp[2] = 0x100u + ele_n;   /* the session handle */
    } else {
        mm->ele.rsp[2] = n_events;
        mm->ele.rsp[3] = events[0];
        mm->ele.rsp[4] = events[1];
        mm->ele.rsp[5] = 0u;              /* the CRC word's place */
    }
    mm->ele.rsp_len = n;
}

void setUp(void)
{
    tfw_imx95_model_reset();
    m = tfw_imx95_model_get();
    m->ele.mu_base = ELE;
    m->ele.par     = 0x00000408u;   /* 8 transmit registers, 4 receive */
    m->ele.enclave = enclave;
    ele.mu = ELE;
    evt    = evt6;
    ids.s1_proto = S1_PROTO;
    ids.s1_msg   = S1_MSG;
    ids.s2_event = S2_EVENT;
    memset(&in, 0, sizeof in);
    memset(raised, 0, sizeof raised);
    n_raised = 0u;
    memset(ele_cmd, 0, sizeof ele_cmd);
    memset(ele_w1, 0, sizeof ele_w1);
    ele_n       = 0u;
    fail_at     = 0u;
    fail_status = 0u;
    n_events    = 0u;
    events[0]   = 0u;
    events[1]   = 0u;
    tfw_hsm_loop_reset();
}

void tearDown(void)
{
}

/* S2 off, so only the interrupts' events are in play. */
static void no_s2(void)
{
    ids.s2_event = 0u;
}

static void test_s1_notification_raises_the_tamper_signal_once(void)
{
    no_s2();
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, S1_PROTO, S1_MSG));
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_TAMPER_PIN, raised[0]);
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);   /* one notification, one edge */
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, S1_PROTO, S1_MSG));
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_TAMPER_PIN, raised[1]);
}

static void test_s1_takes_only_its_own_notification(void)
{
    no_s2();
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_DELAYED_RESP, S1_PROTO, S1_MSG));
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, S1_PROTO + 1u, S1_MSG));
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, S1_PROTO, S1_MSG + 1u));
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
}

static void test_s1_with_its_protocol_unknown_takes_nothing(void)
{
    no_s2();
    ids.s1_proto = 0u;
    ids.s1_msg   = 0u;
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, 0u, 0u));
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
}

static void test_s5_pin_raises_the_hotplug_signal_once(void)
{
    no_s2();
    tfw_imx95_hsm_pin(&in);
    tfw_imx95_hsm_pin(&in);   /* two before the pass are one edge */
    tfw_hsm_loop_poll();
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_HOTPLUG, raised[0]);
}

static void test_s1_and_s5_in_one_pass_raise_both(void)
{
    no_s2();
    tfw_imx95_hsm_pin(&in);
    tfw_imx95_hsm_notified(&in, &ids, HDR(TFW_SCMI_TYPE_NOTIFICATION, S1_PROTO, S1_MSG));
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_TAMPER_PIN, raised[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_HOTPLUG, raised[1]);
}

static void test_s2_event_raises_the_ahab_signal_on_one_session(void)
{
    events[0] = 0x11u;
    events[1] = S2_EVENT;
    n_events  = 2u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_AHAB_FAILURE, raised[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, ele_n);
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_SESSION_OPEN, ele_cmd[0]);
    TEST_ASSERT_EQUAL_HEX32(EVT_CMD, ele_cmd[1]);
    TEST_ASSERT_EQUAL_HEX32(0x101u, ele_w1[1]);   /* the session opened */
    n_events = 0u;
    tfw_hsm_loop_poll();                          /* the event drained */
    events[0] = S2_EVENT;
    n_events  = 1u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, n_raised);
    TEST_ASSERT_EQUAL_UINT32(4u, ele_n);          /* no second SESSION_OPEN */
    TEST_ASSERT_EQUAL_HEX32(0x101u, ele_w1[3]);
}

static void test_s2_other_events_raise_nothing(void)
{
    events[0] = 0x11u;
    events[1] = 0x22u;
    n_events  = 2u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
    TEST_ASSERT_EQUAL_UINT32(2u, ele_n);
}

static void test_s2_session_the_enclave_lost_is_opened_again(void)
{
    fail_at     = 2u;            /* the first hsm_get_event */
    fail_status = 0x00000729u;   /* failure, unknown handle */
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
    TEST_ASSERT_EQUAL_UINT32(0u, in.session);
    events[0] = S2_EVENT;
    n_events  = 1u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(4u, ele_n);
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_SESSION_OPEN, ele_cmd[2]);
    TEST_ASSERT_EQUAL_HEX32(0x103u, ele_w1[3]);
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_AHAB_FAILURE, raised[0]);
}

static void test_s2_refused_session_is_retried_and_nothing_polled(void)
{
    fail_at     = 1u;
    fail_status = 0x00000029u;
    events[0]   = S2_EVENT;
    n_events    = 1u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
    TEST_ASSERT_EQUAL_UINT32(1u, ele_n);
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_SESSION_OPEN, ele_cmd[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, n_raised);
}

static void test_s2_other_refusal_keeps_the_session(void)
{
    fail_at     = 2u;
    fail_status = 0x00000029u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_HEX32(0x101u, in.session);
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(3u, ele_n);
    TEST_ASSERT_EQUAL_HEX32(EVT_CMD, ele_cmd[2]);
}

static void test_s2_with_anything_unknown_sends_nothing(void)
{
    events[0] = S2_EVENT;
    n_events  = 1u;
    ele.mu = 0u;
    tfw_hsm_loop_poll();
    ele.mu      = ELE;
    evt.command = 0u;
    tfw_hsm_loop_poll();
    evt          = evt6;
    ids.s2_event = 0u;
    tfw_hsm_loop_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, ele_n);
    TEST_ASSERT_EQUAL_UINT32(0u, n_raised);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_s1_notification_raises_the_tamper_signal_once);
    RUN_TEST(test_s1_takes_only_its_own_notification);
    RUN_TEST(test_s1_with_its_protocol_unknown_takes_nothing);
    RUN_TEST(test_s5_pin_raises_the_hotplug_signal_once);
    RUN_TEST(test_s1_and_s5_in_one_pass_raise_both);
    RUN_TEST(test_s2_event_raises_the_ahab_signal_on_one_session);
    RUN_TEST(test_s2_other_events_raise_nothing);
    RUN_TEST(test_s2_session_the_enclave_lost_is_opened_again);
    RUN_TEST(test_s2_refused_session_is_retried_and_nothing_polled);
    RUN_TEST(test_s2_other_refusal_keeps_the_session);
    RUN_TEST(test_s2_with_anything_unknown_sends_nothing);
    return UNITY_END();
}
