/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bounded update session -- tfw_boot_session_open.
 *
 * The idle timeout is the only thing between an unauthenticated peer and a
 * healthy device parked in the bootloader forever, so it is tested as a
 * number: the receive loop's empty polls are counted by the mock, and each
 * test asserts how many there were before the loop gave up.
 *
 * tfw_boot_evaluate and tfw_boot_hand_over are replaced here by controllable
 * doubles. Neither is under test -- the policy has its own suite, and the real
 * hand-over jumps -- what is under test is what the session does with their
 * answers.
 */

#include <setjmp.h>
#include <string.h>
#include "unity.h"
#include "boot_recovery.h"
#include "boot_policy.h"
#include "boot_hal_mock.h"
#include "boot_hal.h"
#include "boot_cdi.h"
#include "frame.h"

#define IDLE 100u
#define BYTES 500u
#define PAGE_SZ 128u

static tfw_status_t evaluate_result;
static unsigned    hand_overs;
static unsigned    evaluates;

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)floor;
    memset(out, 0, sizeof *out);
    evaluates++;
    return evaluate_result;
}

void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    (void)v;
    hand_overs++;
}

/* A tier that keeps no floor of its own, which is a legitimate answer
 * (boot_cdi.h): boot_store.c then falls back to the EEPROM copy. It keeps the
 * tier drivers, and their HAL, out of this link. */
tfw_status_t tfw_boot_floor_read(uint32_t *out)
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_boot_floor_write(uint32_t value)
{
    (void)value;
    return TFW_ERR_UNSUPPORTED;
}

static tfw_boot_platform_t plat;

void setUp(void)
{
    tfw_boot_mock_reset();
    memset(&plat, 0, sizeof plat);
    evaluate_result = TFW_OK;
    hand_overs = 0u;
    evaluates = 0u;
}
void tearDown(void) {}

/* --------------------------------------------------------------- helpers */

static void feed_frame(uint8_t seq, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    uint8_t  buf[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(buf, sizeof buf, seq, cmd, p, n);

    TEST_ASSERT_TRUE(len > 0u);
    tfw_boot_mock_feed_rx(buf, len);
}

static void feed_ota_begin(uint32_t total)
{
    uint8_t p[4] = { (uint8_t)total, (uint8_t)(total >> 8),
                     (uint8_t)(total >> 16), (uint8_t)(total >> 24) };
    feed_frame(1u, TFW_CMD_OTA_BEGIN, p, 4u);
}

/* A whole image of `total` bytes, in frames that fit a payload. */
static void feed_ota_image(uint32_t total)
{
    uint8_t  p[4u + 32u];
    uint32_t off;

    memset(&p[4], 0xA5, 32u);
    for (off = 0u; off < total; off += 32u) {
        p[0] = (uint8_t)off;         p[1] = (uint8_t)(off >> 8);
        p[2] = (uint8_t)(off >> 16); p[3] = (uint8_t)(off >> 24);
        feed_frame(2u, TFW_CMD_OTA_DATA, p, sizeof p);
    }
}

/* Replays what the device transmitted through the real framer. */
typedef struct {
    unsigned events;
    unsigned responses;
    tfw_status_t first_event_status;
    tfw_status_t last_status;
} tx_summary_t;

static tx_summary_t summarise_tx(void)
{
    uint8_t      raw[2048];
    size_t       n = tfw_boot_mock_drain_tx(raw, sizeof raw);
    tfw_framer_t  f;
    tx_summary_t s;
    size_t       i;

    memset(&s, 0, sizeof s);
    tfw_framer_init(&f);
    for (i = 0u; i < n; i++) {
        if (tfw_framer_push(&f, raw[i]) != TFW_FRAME_READY) { continue; }
        if (tfw_frame_op(&f) == TFW_OP_EVENT) {
            if (s.events++ == 0u) { s.first_event_status = tfw_frame_status(&f); }
        } else {
            s.responses++;
        }
        s.last_status = tfw_frame_status(&f);
    }
    return s;
}

static void open_session(uint32_t idle_polls, uint32_t idle_bytes,
                         uint32_t max_polls, uint32_t max_bytes)
{
    tfw_boot_session_limits_t lim;

    lim.idle_polls = idle_polls;
    lim.idle_bytes = idle_bytes;
    lim.max_polls  = max_polls;
    lim.max_bytes  = max_bytes;
    tfw_boot_session_open(&plat, &lim);
}

static unsigned hook_at;
static int      hook_fired;
static void (*hook_feed)(void);

static void idle_hook(unsigned empty_polls)
{
    if (!hook_fired && empty_polls == hook_at) {
        hook_fired = 1;
        hook_feed();
    }
}

static void speak_after(unsigned polls, void (*feed)(void))
{
    hook_at = polls;
    hook_fired = 0;
    hook_feed = feed;
    tfw_boot_mock_on_rx_idle(idle_hook);
}

static void feed_ping(void)  { feed_frame(7u, TFW_CMD_PING, 0, 0u); }
static void feed_begin(void) { feed_ota_begin(256u); }
static void feed_stray_data(void)
{
    static const uint8_t p[5] = { 0, 0, 0, 0, 0xEE };
    feed_frame(8u, TFW_CMD_OTA_DATA, p, sizeof p);      /* no BEGIN: refused */
}

/* ------------------------------------------------------------- the bound */

/* The fall-through the whole design rests on: with nobody talking, the call
 * RETURNS, after exactly the configured number of empty polls, and nothing was
 * jumped to, reset or committed on the way. */
static void test_an_idle_session_returns_after_the_bound(void)
{
    open_session(IDLE, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_did_reset());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_jumped());
}

/* A host that opens a session must be able to tell it from a refusal: the
 * announcement is an EVENT with status OK, where recovery's carries the reason
 * the image was refused. */
static void test_the_session_announces_itself_as_requested_not_refused(void)
{
    tx_summary_t s;

    open_session(IDLE, 0u, 0u, 0u);
    s = summarise_tx();
    TEST_ASSERT_EQUAL_UINT(1u, s.events);
    TEST_ASSERT_EQUAL_INT(TFW_OK, s.first_event_status);
}

static void test_ping_is_answered_during_the_session(void)
{
    tx_summary_t s;

    feed_frame(5u, TFW_CMD_PING, 0, 0u);
    open_session(IDLE, 0u, 0u, 0u);
    s = summarise_tx();
    TEST_ASSERT_EQUAL_UINT(1u, s.responses);
    TEST_ASSERT_EQUAL_INT(TFW_OK, s.last_status);
}

/* PING is what a probe sends. If it restarted the clock, anyone on the wire
 * could keep a healthy device in the bootloader by pinging it every few
 * seconds -- the exact thing the timeout is for. */
static void test_ping_does_not_extend_the_session(void)
{
    speak_after(60u, feed_ping);
    open_session(IDLE, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
}

/* A refused update frame is not progress either. */
static void test_a_rejected_update_frame_does_not_extend_the_session(void)
{
    speak_after(60u, feed_stray_data);
    open_session(IDLE, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
}

/* A transfer that is moving must be allowed to finish: an accepted OTA_BEGIN
 * restarts the full bound. */
static void test_an_accepted_update_frame_restarts_the_bound(void)
{
    speak_after(60u, feed_begin);
    open_session(IDLE, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(60u + IDLE, tfw_boot_mock_rx_empty_polls());
}

/* --------------------------------------------------- a busy line is bounded */

/* A peer that never pauses produces no empty poll, so the poll budget never
 * advances. Junk that never forms a frame must still end the session, after
 * exactly the byte budget. */
static void test_a_continuous_junk_stream_ends_the_session_after_the_byte_budget(void)
{
    uint8_t junk[BYTES + 100u];

    memset(junk, 0xEE, sizeof junk);        /* never SOF: no frame ever forms */
    tfw_boot_mock_feed_rx(junk, sizeof junk);

    open_session(IDLE, BYTES, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(BYTES, tfw_boot_mock_rx_consumed());
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
}

/* Back-to-back PINGs are valid frames and answered, but are not progress. */
static void test_a_ping_flood_ends_the_session_after_the_byte_budget(void)
{
    unsigned i;

    for (i = 0u; i < 100u; i++) {
        feed_frame((uint8_t)i, TFW_CMD_PING, 0, 0u);      /* 6 bytes each */
    }
    open_session(IDLE, BYTES, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(BYTES, tfw_boot_mock_rx_consumed());
}

static void feed_junk_burst(void)
{
    uint8_t junk[BYTES - 50u];
    memset(junk, 0xEE, sizeof junk);
    tfw_boot_mock_feed_rx(junk, sizeof junk);
}

static void feed_begin_then_junk(void)
{
    feed_begin();
    feed_junk_burst();
}

/* Progress resets the byte budget as it does the poll budget: a burst just
 * under the limit, then an accepted frame, then another burst under the limit
 * does not end the session -- it only ends on the quiet that follows. */
static void test_progress_restarts_the_byte_budget(void)
{
    speak_after(0u, feed_junk_burst);
    open_session(IDLE, BYTES, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(BYTES - 50u, tfw_boot_mock_rx_consumed());
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());

    tfw_boot_mock_reset();
    memset(&plat, 0, sizeof plat);
    feed_junk_burst();
    speak_after(0u, feed_begin_then_junk);
    open_session(IDLE, BYTES, 0u, 0u);
    /* 450 + begin frame + 450 more junk bytes: over BYTES in total, so only a
     * reset by the accepted OTA_BEGIN lets it reach the poll bound. */
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
}

/* ---------------------------------------- the ceiling progress cannot reset */

static void feed_data(uint32_t offset, uint8_t n)
{
    uint8_t p[4u + 60u];

    memset(p, 0xA5, sizeof p);
    p[0] = (uint8_t)offset;         p[1] = (uint8_t)(offset >> 8);
    p[2] = (uint8_t)(offset >> 16); p[3] = (uint8_t)(offset >> 24);
    feed_frame(2u, TFW_CMD_OTA_DATA, p, (uint8_t)(4u + n));
}

static uint32_t drip_offset;
static unsigned drip_last;

/* One valid OTA_DATA every 60 empty polls: inside the 100-poll idle window, so
 * the idle budget is restarted every time and never expires. */
static void drip_hook(unsigned empty_polls)
{
    if (empty_polls == 0u && drip_offset == 0u) {
        feed_ota_begin(16384u);
        drip_offset = 1u;               /* BEGIN sent; DATA follows at offset 0 */
        drip_last = 0u;
        return;
    }
    if (empty_polls >= 60u && empty_polls - drip_last >= 60u) {
        drip_last = empty_polls;
        feed_data(drip_offset - 1u, 32u);
        drip_offset += 32u;
    }
}

/* Accepted frames are well-formed, not authenticated, so they cannot be allowed
 * to restart the clock forever: a drip meets the poll ceiling. */
static void test_a_drip_of_valid_frames_ends_at_the_poll_ceiling(void)
{
    const uint32_t ceiling = 1000u;

    drip_offset = 0u;
    tfw_boot_mock_on_rx_idle(drip_hook);
    open_session(IDLE, 0u, ceiling, 0u);

    /* Every iteration counts, received byte or not; the one that trips the
     * ceiling returns before it polls. */
    TEST_ASSERT_EQUAL_UINT(ceiling - 1u,
                           tfw_boot_mock_rx_empty_polls() + tfw_boot_mock_rx_consumed());
    TEST_ASSERT_TRUE(tfw_boot_mock_rx_consumed() > 20u * 6u);  /* really was accepting frames */
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
}

/* A flood of valid OTA_DATA leaves no empty poll and restarts both idle
 * budgets on every frame; only the byte ceiling stops it. */
static void test_a_flood_of_valid_frames_ends_at_the_byte_ceiling(void)
{
    const uint32_t ceiling = 2000u;
    uint32_t off;

    feed_ota_begin(16384u);
    for (off = 0u; off < 90u * 32u; off += 32u) {
        feed_data(off, 32u);
    }
    open_session(IDLE, BYTES, 0u, ceiling);

    TEST_ASSERT_EQUAL_UINT(ceiling, tfw_boot_mock_rx_consumed());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
}

/* ----------------------------------------------------- one attempt only */

/* A refused OTA_END (short transfer) is the failed attempt. The BEGIN queued
 * behind it must never be read. */
static void test_a_refused_end_ends_the_requested_session(void)
{
    feed_ota_begin(256u);
    feed_data(0u, 32u);
    feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);      /* 32 of 256 bytes: refused */
    feed_ota_begin(256u);
    open_session(IDLE, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(0u, evaluates);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_TRUE(tfw_boot_mock_rx_consumed() < 10u + 42u + 6u + 10u);
}

/* The same for an END whose image fails verification, and a second BEGIN
 * queued behind it is again never read. */
static void test_a_failed_verification_ends_the_requested_session(void)
{
    feed_ota_begin(256u);
    feed_ota_image(256u);
    feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);
    feed_ota_begin(256u);
    evaluate_result = TFW_ERR_CRYPTO;

    open_session(IDLE, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_rx_empty_polls());
    TEST_ASSERT_EQUAL_UINT(10u + 8u * 42u + 6u, tfw_boot_mock_rx_consumed());
}

/* ------------------------------------------- a full legitimate update fits */

static uint32_t full_offset;
static int      full_end_sent;

static void full_image_hook(unsigned empty_polls)
{
    (void)empty_polls;
    if (full_offset < TFW_BOOT_APP_SIZE) {
        feed_data(full_offset, 32u);        /* the smallest chunk the ceiling assumes */
        full_offset += 32u;
    } else if (!full_end_sent) {
        full_end_sent = 1;
        feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);
    }
}

/* The whole application region, 32 data bytes a frame, stop-and-wait. It must
 * complete and hand over with the REAL byte ceiling for this board in force and
 * a good deal to spare; the poll ceiling cannot be exceeded on the host mock,
 * where an iteration costs nothing, so its margin is argued in config.h. */
static void test_a_full_size_update_completes_inside_the_ceilings(void)
{
    const uint32_t max_bytes = TFW_BOOT_SESSION_MAX_BYTES;

    full_offset = 0u;
    full_end_sent = 0;
    feed_ota_begin(TFW_BOOT_APP_SIZE);
    tfw_boot_mock_on_rx_idle(full_image_hook);

    open_session(IDLE, TFW_BOOT_SESSION_IDLE_BYTES(TFW_UART_BAUD), 10000000u, max_bytes);

    TEST_ASSERT_EQUAL_UINT(1u, hand_overs);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_APP_SIZE / PAGE_SZ, tfw_boot_mock_page_writes());
    TEST_ASSERT_TRUE(tfw_boot_mock_rx_consumed() < max_bytes);
    /* At the smallest chunk the ceiling assumes, the transfer itself fits with
     * the whole TFW_BOOT_SESSION_SLACK still unspent for retransmits and probes. */
    TEST_ASSERT_TRUE(tfw_boot_mock_rx_consumed() <= max_bytes - TFW_BOOT_SESSION_SLACK);
}

/* ------------------------------------------------- recovery is unbounded */

static jmp_buf escape;

static void escape_on_idle(unsigned empty_polls)
{
    (void)empty_polls;
    longjmp(escape, 1);
}

/* Recovery has nothing to fall back to, so it takes neither a ceiling nor a
 * one-attempt rule. Real tfw_boot_recovery, a flood far past every byte bound
 * and a failed END followed by a fresh BEGIN: it consumes all of it and is
 * still running when the line goes quiet. */
static void test_recovery_is_unbounded_and_takes_more_than_one_attempt(void)
{
    uint8_t junk[3000];
    volatile int returned = 0;

    memset(junk, 0xEE, sizeof junk);
    tfw_boot_mock_feed_rx(junk, sizeof junk);
    feed_ota_begin(256u);
    feed_data(0u, 32u);
    feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);      /* refused: a failed attempt */
    feed_ota_begin(256u);                       /* ... and a second one */
    tfw_boot_mock_on_rx_idle(escape_on_idle);

    if (setjmp(escape) == 0) {
        tfw_boot_recovery(&plat, TFW_ERR_CRYPTO);
        returned = 1;
    }

    TEST_ASSERT_FALSE(returned);
    TEST_ASSERT_EQUAL_UINT(sizeof junk + 10u + 42u + 6u + 10u,
                           tfw_boot_mock_rx_consumed());
}

/* ------------------------------------------------------- how it can end */

/* A completed, verified update hands over: this is the session doing its job. */
static void test_a_verified_update_hands_over(void)
{
    feed_ota_begin(256u);
    feed_ota_image(256u);
    feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);
    evaluate_result = TFW_OK;

    open_session(IDLE, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT(1u, hand_overs);
    TEST_ASSERT_EQUAL_UINT(2u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
}

/* The session grants nothing: an image that does not verify is not handed
 * over, is recorded as failed, and the session carries on until it times out. */
static void test_an_unverified_update_is_never_handed_over(void)
{
    tx_summary_t s;

    feed_ota_begin(256u);
    feed_ota_image(256u);
    feed_frame(3u, TFW_CMD_OTA_END, 0, 0u);
    evaluate_result = TFW_ERR_CRYPTO;

    open_session(IDLE, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    s = summarise_tx();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, s.last_status);
}

static void test_reset_ends_the_session_by_resetting(void)
{
    const uint8_t mode[1] = { 0u };

    feed_frame(4u, TFW_CMD_RESET, mode, 1u);
    open_session(IDLE, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(1u, tfw_boot_mock_did_reset());
}

/* An application command is not this loop's business and must not move it. */
static void test_application_commands_are_refused_in_the_session(void)
{
    tx_summary_t s;

    feed_frame(6u, TFW_CMD_CAPS, 0, 0u);
    open_session(IDLE, 0u, 0u, 0u);
    s = summarise_tx();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, s.last_status);
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_idle_session_returns_after_the_bound);
    RUN_TEST(test_the_session_announces_itself_as_requested_not_refused);
    RUN_TEST(test_a_continuous_junk_stream_ends_the_session_after_the_byte_budget);
    RUN_TEST(test_a_ping_flood_ends_the_session_after_the_byte_budget);
    RUN_TEST(test_progress_restarts_the_byte_budget);
    RUN_TEST(test_a_drip_of_valid_frames_ends_at_the_poll_ceiling);
    RUN_TEST(test_a_flood_of_valid_frames_ends_at_the_byte_ceiling);
    RUN_TEST(test_a_refused_end_ends_the_requested_session);
    RUN_TEST(test_a_failed_verification_ends_the_requested_session);
    RUN_TEST(test_a_full_size_update_completes_inside_the_ceilings);
    RUN_TEST(test_recovery_is_unbounded_and_takes_more_than_one_attempt);
    RUN_TEST(test_ping_is_answered_during_the_session);
    RUN_TEST(test_ping_does_not_extend_the_session);
    RUN_TEST(test_a_rejected_update_frame_does_not_extend_the_session);
    RUN_TEST(test_an_accepted_update_frame_restarts_the_bound);
    RUN_TEST(test_a_verified_update_hands_over);
    RUN_TEST(test_an_unverified_update_is_never_handed_over);
    RUN_TEST(test_reset_ends_the_session_by_resetting);
    RUN_TEST(test_application_commands_are_refused_in_the_session);
    return UNITY_END();
}
