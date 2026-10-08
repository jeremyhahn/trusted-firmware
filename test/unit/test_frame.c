/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "frame.h"
#include "crc16.h"

static tfw_framer_t f;

void setUp(void)    { tfw_framer_init(&f); }
void tearDown(void) {}

/* Feeds every byte but the last and asserts nothing completed early, then
 * returns what the final byte produced. Most tests care only about that. */
static tfw_frame_event_t feed(const uint8_t *b, uint16_t n)
{
    tfw_frame_event_t e = TFW_FRAME_NONE;
    for (uint16_t i = 0; i < n; i++) {
        e = tfw_framer_push(&f, b[i]);
        if (i + 1u < n) {
            TEST_ASSERT_EQUAL(TFW_FRAME_NONE, e);
        }
    }
    return e;
}

static void test_request_round_trips(void)
{
    const uint8_t pay[3] = { 0x0Du, 0x01u, 0x7Fu };
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 0x42u, TFW_CMD_DIGITAL_WRITE, pay, 3u);

    TEST_ASSERT_NOT_EQUAL(0u, n);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
    TEST_ASSERT_EQUAL_HEX8(0x42u, tfw_frame_seq(&f));
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_DIGITAL_WRITE, tfw_frame_op(&f));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_frame_payload_len(&f));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, tfw_frame_payload(&f), 3u);
}

static void test_response_sets_the_response_bit_and_carries_status(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_response(wire, sizeof wire, 0x07u,
                                          TFW_CMD_PING, TFW_ERR_BAD_PIN, 0, 0u);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, tfw_frame_op(&f));
    TEST_ASSERT_EQUAL_HEX8(TFW_ERR_BAD_PIN, tfw_frame_status(&f));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&f));
}

static void test_event_uses_the_reserved_op_and_no_sequence(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_event(wire, sizeof wire, TFW_OK, 0, 0u);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, tfw_frame_op(&f));
    TEST_ASSERT_EQUAL_HEX8(TFW_SEQ_NONE, tfw_frame_seq(&f));
}

static void test_empty_payload_is_a_valid_frame(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_CAPS, 0, 0u);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&f));
}

static void test_maximum_payload_is_a_valid_frame(void)
{
    uint8_t pay[TFW_MAX_PAYLOAD];
    uint8_t wire[TFW_MAX_FRAME];
    memset(pay, 0x5Au, sizeof pay);
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_PING, pay, TFW_MAX_PAYLOAD);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
    TEST_ASSERT_EQUAL_UINT8(TFW_MAX_PAYLOAD, tfw_frame_payload_len(&f));
}

static void test_encoder_refuses_an_oversize_payload(void)
{
    uint8_t pay[TFW_MAX_PAYLOAD + 1u];
    uint8_t wire[TFW_MAX_FRAME + 8u];
    memset(pay, 0u, sizeof pay);
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_PING, pay, (uint8_t)sizeof pay));
}

static void test_encoder_refuses_a_buffer_it_would_overrun(void)
{
    uint8_t small[4];
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_request(small, sizeof small, 1u, TFW_CMD_CAPS, 0, 0u));
}

static void test_a_corrupted_payload_byte_is_caught_by_the_crc(void)
{
    const uint8_t pay[2] = { 0xAAu, 0xBBu };
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 9u, TFW_CMD_PING, pay, 2u);

    wire[5] ^= 0x01u;                       /* flip one bit in the body */
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_CRC, feed(wire, n));
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_crc);
}

/* The CRC spans LEN precisely so this case is caught. A corrupted length would
 * otherwise mis-frame everything after it, and the damage would surface as a
 * cascade of unrelated failures rather than one bad frame. */
static void test_a_corrupted_length_byte_is_caught(void)
{
    const uint8_t pay[4] = { 1u, 2u, 3u, 4u };
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 9u, TFW_CMD_PING, pay, 4u);
    tfw_frame_event_t e = TFW_FRAME_NONE;

    wire[1] = (uint8_t)(wire[1] - 1u);      /* still plausible, but wrong */
    for (uint16_t i = 0; i < n; i++) {
        tfw_frame_event_t r = tfw_framer_push(&f, wire[i]);
        if (r != TFW_FRAME_NONE) { e = r; }
    }
    TEST_ASSERT_NOT_EQUAL(TFW_FRAME_READY, e);
}

static void test_a_length_below_the_minimum_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_FRAME_NONE,       tfw_framer_push(&f, TFW_SOF));
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_format);
}

static void test_a_length_above_the_maximum_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_FRAME_NONE,       tfw_framer_push(&f, TFW_SOF));
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, (uint8_t)(TFW_MAX_LEN + 1u)));
}

/* THE RESYNC CASE.
 *
 * A payload byte happens to be 0xA5 and the byte after it is not a plausible
 * length -- but a genuine frame starts at that very byte. Because the failing
 * byte is reprocessed as a candidate SOF, the real frame survives. Without the
 * reprocess it would be consumed as the false frame's length and lost. */
static void test_a_false_sof_does_not_swallow_the_frame_that_follows_it(void)
{
    uint8_t  good[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(good, sizeof good, 0x33u, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_EQUAL(TFW_FRAME_NONE,       tfw_framer_push(&f, TFW_SOF));  /* false SOF */
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, TFW_SOF));  /* implausible LEN,
                                                                          * and itself a SOF */
    /* the real frame's remaining bytes, its SOF having just been reprocessed */
    tfw_frame_event_t e = TFW_FRAME_NONE;
    for (uint16_t i = 1; i < n; i++) {
        e = tfw_framer_push(&f, good[i]);
    }
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, e);
    TEST_ASSERT_EQUAL_HEX8(0x33u, tfw_frame_seq(&f));
}

static void test_leading_garbage_before_a_frame_is_discarded(void)
{
    const uint8_t junk[5] = { 0x00u, 0xFFu, 0x13u, 0x7Eu, 0x01u };
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 5u, TFW_CMD_PING, 0, 0u);

    for (unsigned i = 0; i < sizeof junk; i++) {
        TEST_ASSERT_EQUAL(TFW_FRAME_NONE, tfw_framer_push(&f, junk[i]));
    }
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
}

/* A truncated frame must not hold the machine hostage until the next byte
 * arrives -- that byte may be the start of a good frame, which would then be
 * eaten as the dead frame's tail. */
static void test_abort_recovers_from_a_truncated_frame(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_PING, (const uint8_t *)"ab", 2u);

    for (uint16_t i = 0; i < n - 2u; i++) { (void)tfw_framer_push(&f, wire[i]); }
    tfw_framer_abort(&f);

    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
}

/* A sender can be internally consistent and still wrong: this frame's CRC is
 * correct, but its OP claims a STATUS byte that its LEN leaves no room for.
 * Rejected structurally, because the alternative is a payload length that
 * wraps to 255 and a dispatcher reading past the body. */
static void test_a_response_too_short_for_its_status_byte_is_malformed(void)
{
    uint8_t  wire[8];
    uint16_t crc;

    wire[0] = TFW_SOF;
    wire[1] = 2u;                                   /* SEQ + OP only */
    wire[2] = 0x01u;                                /* SEQ */
    wire[3] = TFW_CMD_PING | TFW_OP_RESPONSE_BIT;     /* claims a STATUS */
    crc     = tfw_crc16(&wire[1], 3u);
    wire[4] = (uint8_t)(crc & 0xFFu);
    wire[5] = (uint8_t)(crc >> 8);

    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, feed(wire, 6u));
}

/* THE SAFETY PROPERTY, stated directly. An arbitrary byte stream may waste
 * cycles but must never overrun the body buffer, wedge the state machine, or
 * report a frame that was not there. Ten million bytes, and the suite runs
 * under ASan in its own CI job, so an overrun is a crash rather than a
 * silently-passing test. */
static void test_random_bytes_never_overrun_or_wedge_the_framer(void)
{
    unsigned long ready = 0u;

    srand(20260922u);
    for (unsigned long i = 0; i < 10000000uL; i++) {
        tfw_frame_event_t e = tfw_framer_push(&f, (uint8_t)(rand() & 0xFF));
        TEST_ASSERT_TRUE(f.state <= (uint8_t)TFW_FRAME_CRC_HI);
        TEST_ASSERT_TRUE(f.got <= f.want);
        TEST_ASSERT_TRUE(f.want <= TFW_MAX_LEN);
        if (e == TFW_FRAME_READY) {
            ready++;
            TEST_ASSERT_TRUE(tfw_frame_payload_len(&f) <= TFW_MAX_PAYLOAD);
        }
    }
    /* Random bytes DO occasionally form a valid frame -- 1 in 65536 CRCs
     * match by chance. The assertion is that nothing broke, not that nothing
     * parsed; a run that produced zero would mean the generator, not the
     * framer, was being tested. */
    TEST_ASSERT_TRUE(ready < 10000uL);

    /* PROOF THE LOOP RAN. Every assertion above holds trivially over zero
     * iterations, so a fuzz test that silently stopped looping would still
     * report PASS -- the failure that looks most like success. Ten million
     * random bytes cannot avoid producing both malformed lengths and failed
     * CRCs, so these two counters moving is evidence the work happened. */
    TEST_ASSERT_GREATER_THAN_UINT16(0u, f.err_format);
    TEST_ASSERT_GREATER_THAN_UINT16(0u, f.err_crc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request_round_trips);
    RUN_TEST(test_response_sets_the_response_bit_and_carries_status);
    RUN_TEST(test_event_uses_the_reserved_op_and_no_sequence);
    RUN_TEST(test_empty_payload_is_a_valid_frame);
    RUN_TEST(test_maximum_payload_is_a_valid_frame);
    RUN_TEST(test_encoder_refuses_an_oversize_payload);
    RUN_TEST(test_encoder_refuses_a_buffer_it_would_overrun);
    RUN_TEST(test_a_corrupted_payload_byte_is_caught_by_the_crc);
    RUN_TEST(test_a_corrupted_length_byte_is_caught);
    RUN_TEST(test_a_length_below_the_minimum_is_refused);
    RUN_TEST(test_a_length_above_the_maximum_is_refused);
    RUN_TEST(test_a_false_sof_does_not_swallow_the_frame_that_follows_it);
    RUN_TEST(test_leading_garbage_before_a_frame_is_discarded);
    RUN_TEST(test_abort_recovers_from_a_truncated_frame);
    RUN_TEST(test_a_response_too_short_for_its_status_byte_is_malformed);
    RUN_TEST(test_random_bytes_never_overrun_or_wedge_the_framer);
    return UNITY_END();
}
