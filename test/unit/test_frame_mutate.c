/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A seeded mutation test for the frame decoder: the first code every byte
 * from the host or the network reaches.
 *
 * The inputs are the literal wire vectors, mutated -- bit flips, truncation,
 * LEN set to 0, the maximum and one past it, appended bytes, a CRC recomputed
 * over a mutated body so the frame gets past the check -- plus pure random
 * buffers. Each input is copied into a heap buffer of exactly its length, so
 * under `make test-asan` a read one byte past it is a fault, not a pass.
 *
 * The seed and the counts are literals: a failure reproduces bit for bit.
 */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "frame.h"
#include "crc16.h"
#include "vectors.h"

#define ITERATIONS 4000u
#define INPUT_CAP  160u

static tfw_framer_t f;
static uint32_t     rng;

void setUp(void)
{
    rng = 0x2026A5C3u;
    tfw_framer_init(&f);
}

void tearDown(void) {}

/* xorshift32: small, and the same sequence on every host. */
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

typedef struct {
    const char *bytes;
    uint16_t    len;
} seed_t;

#define SEED(v) { v, (uint16_t)(sizeof v - 1u) }

static const seed_t seeds[] = {
    SEED(TFWV_FRAME_I2C_CONFIG_REQ),
    SEED(TFWV_FRAME_SPI_CONFIG_REQ),
    SEED(TFWV_FRAME_OTA_BEGIN_BAD_STATE_RESP),
    SEED(TFWV_FRAME_OTA_BEGIN_NO_STORE_RESP),
    SEED(TFWV_FRAME_OTA_BEGIN_REQ),
    SEED(TFWV_FRAME_OTA_BEGIN_OVER_BUDGET_RESP),
    SEED(TFWV_FRAME_EEPROM_WRITE_NETCFG_AUTH_REQUIRED_RESP),
    SEED(TFWV_FRAME_OTA_INFO_REQ),
    SEED(TFWV_FRAME_OTA_INFO_RESP),
    SEED(TFWV_FRAME_OTA_ACTIVATE_REQ),
    SEED(TFWV_FRAME_OTA_ACTIVATE_RESP),
    SEED(TFWV_FRAME_OTA_ACTIVATE_BAD_STATE_RESP),
    SEED(TFWV_FRAME_OTA_CONFIRM_REQ),
    SEED(TFWV_FRAME_OTA_CONFIRM_RESP),
    SEED(TFWV_FRAME_HSM_HEARTBEAT_REQ),
    SEED(TFWV_FRAME_HSM_HEARTBEAT_RESP),
    SEED(TFWV_FRAME_HSM_REPORT_REQ),
    SEED(TFWV_FRAME_HSM_REPORT_RESP),
    SEED(TFWV_FRAME_HSM_REPORT_RANGE_RESP),
    SEED(TFWV_FRAME_HSM_VERDICT_REQ),
    SEED(TFWV_FRAME_HSM_SELECT_REQ),
    SEED(TFWV_FRAME_HSM_SELECT_RESP),
    SEED(TFWV_FRAME_HSM_LOAD_REQ),
    SEED(TFWV_FRAME_HSM_LOAD_RANGE_RESP),
};
#define SEED_COUNT ((uint32_t)(sizeof seeds / sizeof seeds[0]))

/* Rewrites the CRC after LEN's promised body, if the buffer still holds it,
 * so a mutated body is judged by what follows the check, not by the check. */
static void recrc(uint8_t *b, uint16_t n)
{
    uint16_t body = (n > 1u) ? b[1] : 0u;
    uint16_t crc;

    if (n < (uint16_t)(body + 4u)) {
        return;
    }
    crc              = tfw_crc16(&b[1], (uint16_t)(body + 1u));
    b[body + 2u]     = (uint8_t)(crc & 0xFFu);
    b[body + 3u]     = (uint8_t)(crc >> 8);
}

/* One mutated input in b[0..INPUT_CAP); returns its length. */
static uint16_t make_input(uint8_t *b)
{
    const seed_t *s   = &seeds[rnd() % SEED_COUNT];
    uint16_t      n   = s->len;
    uint32_t      ops = 1u + rnd() % 3u;

    if (rnd() % 8u == 0u) {
        n = (uint16_t)(1u + rnd() % INPUT_CAP);
        for (uint16_t i = 0; i < n; i++) {
            b[i] = (uint8_t)rnd();
        }
        return n;
    }
    memcpy(b, s->bytes, n);
    while (ops-- > 0u) {
        switch (rnd() % 8u) {
        case 0:
            for (uint32_t k = 1u + rnd() % 8u; k > 0u; k--) {
                b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            break;
        case 1:
            n = (uint16_t)(1u + rnd() % n);
            break;
        case 2:
            b[1] = 0u;
            break;
        case 3:
            b[1] = TFW_MAX_LEN;
            break;
        case 4:
            b[1] = (uint8_t)(TFW_MAX_LEN + 1u);
            break;
        case 5:
            for (uint32_t k = 1u + rnd() % 16u; k > 0u && n < INPUT_CAP; k--) {
                b[n++] = (uint8_t)rnd();
            }
            break;
        case 6:
            b[2u + rnd() % (n > 2u ? n - 2u : 1u)] = (uint8_t)rnd();
            recrc(b, n);
            break;
        default:
            recrc(b, n);
            break;
        }
        if (n < 2u) {
            n = 2u;
        }
    }
    return n;
}

static void test_mutated_frames_never_overrun_or_misreport(void)
{
    static uint8_t scratch[INPUT_CAP];
    unsigned long  ready = 0u, bad = 0u;

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        uint16_t n  = make_input(scratch);
        uint8_t *in = malloc(n);

        TEST_ASSERT_NOT_NULL(in);
        memcpy(in, scratch, n);
        tfw_framer_abort(&f);
        for (uint16_t i = 0; i < n; i++) {
            tfw_frame_event_t e = tfw_framer_push(&f, in[i]);

            TEST_ASSERT_TRUE(e <= TFW_FRAME_BAD_FORMAT);
            TEST_ASSERT_TRUE(f.state <= (uint8_t)TFW_FRAME_CRC_HI);
            TEST_ASSERT_TRUE(f.got <= f.want);
            TEST_ASSERT_TRUE(f.want <= TFW_MAX_LEN);
            if (e == TFW_FRAME_BAD_CRC || e == TFW_FRAME_BAD_FORMAT) {
                bad++;
            }
            if (e == TFW_FRAME_READY) {
                const uint8_t *p   = tfw_frame_payload(&f);
                uint8_t        len = tfw_frame_payload_len(&f);

                ready++;
                /* SOF, LEN, the body and two CRC bytes all came from this input. */
                TEST_ASSERT_TRUE((uint32_t)f.want + 4u <= (uint32_t)i + 1u);
                TEST_ASSERT_TRUE(len <= TFW_MAX_PAYLOAD);
                TEST_ASSERT_TRUE(p >= f.body);
                TEST_ASSERT_TRUE(p + len <= f.body + f.want);
                (void)tfw_frame_seq(&f);
                (void)tfw_frame_op(&f);
                (void)tfw_frame_status(&f);
            }
        }
        free(in);
    }

    /* The framer is not wedged: after an abort a clean frame still reads. */
    tfw_framer_abort(&f);
    {
        tfw_frame_event_t e = TFW_FRAME_NONE;
        for (uint16_t i = 0; i < seeds[0].len; i++) {
            e = tfw_framer_push(&f, (uint8_t)seeds[0].bytes[i]);
        }
        TEST_ASSERT_EQUAL(TFW_FRAME_READY, e);
    }

    /* Proof the loop ran and reached both sides of the CRC. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, ready);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, bad);
}

/* ---- each refusal on its own: the event, the counter, nothing past it ---- */

static tfw_frame_event_t push_all(const uint8_t *b, uint16_t n)
{
    tfw_frame_event_t e = TFW_FRAME_NONE;
    for (uint16_t i = 0; i < n; i++) {
        e = tfw_framer_push(&f, b[i]);
    }
    return e;
}

static void test_a_length_above_the_maximum_counts_one_format_error_and_waits_for_sof(void)
{
    (void)tfw_framer_push(&f, TFW_SOF);
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, (uint8_t)(TFW_MAX_LEN + 1u)));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT8(0u, f.want);
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_format);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_crc);
}

static void test_a_length_below_the_minimum_takes_no_length_and_waits_for_sof(void)
{
    (void)tfw_framer_push(&f, TFW_SOF);
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, (uint8_t)(TFW_LEN_MIN - 1u)));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT8(0u, f.want);
    TEST_ASSERT_EQUAL_UINT8(0u, f.got);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_crc);
}

static void test_a_crc_mismatch_counts_one_crc_error_and_ends_the_frame(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_PING, 0, 0u);

    wire[n - 2u] ^= 0x01u;                     /* CRC low byte */
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_CRC, push_all(wire, n));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_crc);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_format);
    wire[n - 2u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, push_all(wire, n));
}

static void test_a_response_with_no_room_for_its_status_counts_a_format_error_not_a_crc_error(void)
{
    uint8_t  wire[6];
    uint16_t crc;

    wire[0] = TFW_SOF;
    wire[1] = 2u;
    wire[2] = 0x01u;
    wire[3] = TFW_CMD_PING | TFW_OP_RESPONSE_BIT;
    crc     = tfw_crc16(&wire[1], 3u);
    wire[4] = (uint8_t)(crc & 0xFFu);
    wire[5] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, push_all(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_format);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_crc);
}

static void test_the_crc_error_counter_saturates_rather_than_wrapping(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_PING, 0, 0u);

    wire[n - 1u] ^= 0x80u;
    f.err_crc = 0xFFFFu;
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_CRC, push_all(wire, n));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, f.err_crc);
}

static void test_the_format_error_counter_saturates_rather_than_wrapping(void)
{
    f.err_format = 0xFFFFu;
    (void)tfw_framer_push(&f, TFW_SOF);
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, 0u));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, f.err_format);
}

static void test_a_corrupted_state_counts_no_error(void)
{
    f.state = 0xFFu;
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, TFW_SOF));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_format);
    TEST_ASSERT_EQUAL_UINT16(0u, f.err_crc);
}

static void test_a_request_buffer_one_byte_short_is_refused_and_untouched(void)
{
    uint8_t wire[TFW_FRAME_OVERHEAD + 2u + 1u];
    uint8_t want[sizeof wire];
    uint8_t pay = 0x5Au;

    memset(wire, 0xEEu, sizeof wire);
    memcpy(want, wire, sizeof want);
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_request(wire, (uint16_t)(sizeof wire - 1u), 1u, TFW_CMD_PING, &pay, 1u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, sizeof wire);
}

/* A response's STATUS byte counts against the buffer: room for a request of
 * the same payload is one byte short of room for the response. */
static void test_a_response_buffer_with_no_room_for_the_status_is_refused_and_untouched(void)
{
    uint8_t wire[TFW_FRAME_OVERHEAD + 3u];
    uint8_t want[sizeof wire];

    memset(wire, 0xEEu, sizeof wire);
    memcpy(want, wire, sizeof want);
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_response(wire, (uint16_t)(sizeof wire - 1u), 1u, TFW_CMD_PING, TFW_OK, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT16(sizeof wire,
        tfw_frame_encode_response(wire, (uint16_t)sizeof wire, 1u, TFW_CMD_PING, TFW_OK, 0, 0u));
}

static void test_an_oversize_response_payload_is_refused_and_untouched(void)
{
    uint8_t pay[TFW_MAX_PAYLOAD + 1u];
    uint8_t wire[TFW_MAX_FRAME + 8u];
    uint8_t want[sizeof wire];

    memset(pay, 0x11u, sizeof pay);
    memset(wire, 0xEEu, sizeof wire);
    memcpy(want, wire, sizeof want);
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_response(wire, sizeof wire, 1u, TFW_CMD_PING, TFW_OK, pay, (uint8_t)sizeof pay));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, sizeof wire);
}

static void test_an_oversize_event_payload_is_refused_and_untouched(void)
{
    uint8_t pay[TFW_MAX_PAYLOAD + 1u];
    uint8_t wire[TFW_MAX_FRAME + 8u];
    uint8_t want[sizeof wire];

    memset(pay, 0x22u, sizeof pay);
    memset(wire, 0xEEu, sizeof wire);
    memcpy(want, wire, sizeof want);
    TEST_ASSERT_EQUAL_UINT16(0u,
        tfw_frame_encode_event(wire, sizeof wire, TFW_OK, pay, (uint8_t)sizeof pay));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, sizeof wire);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_frames_never_overrun_or_misreport);
    RUN_TEST(test_a_length_above_the_maximum_counts_one_format_error_and_waits_for_sof);
    RUN_TEST(test_a_length_below_the_minimum_takes_no_length_and_waits_for_sof);
    RUN_TEST(test_a_crc_mismatch_counts_one_crc_error_and_ends_the_frame);
    RUN_TEST(test_a_response_with_no_room_for_its_status_counts_a_format_error_not_a_crc_error);
    RUN_TEST(test_the_crc_error_counter_saturates_rather_than_wrapping);
    RUN_TEST(test_the_format_error_counter_saturates_rather_than_wrapping);
    RUN_TEST(test_a_corrupted_state_counts_no_error);
    RUN_TEST(test_a_request_buffer_one_byte_short_is_refused_and_untouched);
    RUN_TEST(test_a_response_buffer_with_no_room_for_the_status_is_refused_and_untouched);
    RUN_TEST(test_an_oversize_response_payload_is_refused_and_untouched);
    RUN_TEST(test_an_oversize_event_payload_is_refused_and_untouched);
    return UNITY_END();
}
