/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "frame.h"
#include "crc16.h"
#include "vectors.h"

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

/* Status 19 is pinned by a literal, not by the enum it came from: a renumber
 * would move the enum, and only a literal notices. */
static void test_bad_state_response_matches_the_vector(void)
{
    static const uint8_t want[] = TFWV_FRAME_OTA_BEGIN_BAD_STATE_RESP;
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_response(wire, sizeof wire, 0x03u,
                                          TFW_CMD_OTA_BEGIN, TFW_ERR_BAD_STATE, 0, 0u);
    TEST_ASSERT_EQUAL_UINT16(sizeof want - 1u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, n);
}

static void test_no_store_response_matches_the_vector(void)
{
    static const uint8_t want[] = TFWV_FRAME_OTA_BEGIN_NO_STORE_RESP;
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_response(wire, sizeof wire, 0x03u,
                                          TFW_CMD_OTA_BEGIN, TFW_ERR_NO_STORE, 0, 0u);
    TEST_ASSERT_EQUAL_UINT16(sizeof want - 1u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, n);
}

/* ADR decision 8's control commands (#77): 0x6C, 0x6D and 0x6E and the 44-byte
 * OTA_INFO reply, each against its literal. */
static void check(const uint8_t *want, uint16_t wlen, uint16_t n, const uint8_t *wire)
{
    TEST_ASSERT_EQUAL_UINT16(wlen, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, wire, n);
}

static void test_ota_control_frames_match_the_vectors(void)
{
    static const uint8_t info_req[] = TFWV_FRAME_OTA_INFO_REQ;
    static const uint8_t info_resp[] = TFWV_FRAME_OTA_INFO_RESP;
    static const uint8_t act_req[] = TFWV_FRAME_OTA_ACTIVATE_REQ;
    static const uint8_t act_resp[] = TFWV_FRAME_OTA_ACTIVATE_RESP;
    static const uint8_t act_bad[] = TFWV_FRAME_OTA_ACTIVATE_BAD_STATE_RESP;
    static const uint8_t conf_req[] = TFWV_FRAME_OTA_CONFIRM_REQ;
    static const uint8_t conf_resp[] = TFWV_FRAME_OTA_CONFIRM_RESP;
    uint8_t info[44] = { 2u, 0u, 0x05u, 7u, 0u, 0u, 0u, 4u, 0x2Au, 0u, 0u, 0u };
    uint8_t one = 1u;
    uint8_t wire[TFW_MAX_FRAME];

    memset(&info[12], 0xABu, 32u);
    check(info_req, sizeof info_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 4u, TFW_CMD_OTA_INFO, &one, 1u), wire);
    check(info_resp, sizeof info_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 4u, TFW_CMD_OTA_INFO, TFW_OK, info, 44u), wire);
    check(act_req, sizeof act_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 5u, TFW_CMD_OTA_ACTIVATE, &one, 1u), wire);
    check(act_resp, sizeof act_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 5u, TFW_CMD_OTA_ACTIVATE, TFW_OK, 0, 0u), wire);
    check(act_bad, sizeof act_bad - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 5u, TFW_CMD_OTA_ACTIVATE, TFW_ERR_BAD_STATE, 0, 0u), wire);
    check(conf_req, sizeof conf_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 6u, TFW_CMD_OTA_CONFIRM, 0, 0u), wire);
    check(conf_resp, sizeof conf_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 6u, TFW_CMD_OTA_CONFIRM, TFW_OK, 0, 0u), wire);
}

/* EEPROM_WRITE at the network block's old address and its new one (#125). */
static void test_eeprom_write_frames_match_the_vectors(void)
{
    static const uint8_t old_req[] = TFWV_FRAME_EEPROM_WRITE_OLD_NETCFG_REQ;
    static const uint8_t refused[] = TFWV_FRAME_EEPROM_WRITE_PROTECTED_RESP;
    static const uint8_t new_req[] = TFWV_FRAME_EEPROM_WRITE_NETCFG_REQ;
    static const uint8_t ok[]      = TFWV_FRAME_EEPROM_WRITE_NETCFG_RESP;
    const uint8_t at_old[6] = { 0xC0u, 0x00u, 'T', 'F', 'N', 'C' };
    const uint8_t at_new[6] = { 0x40u, 0x01u, 'T', 'F', 'N', 'C' };
    uint8_t wire[TFW_MAX_FRAME];

    check(old_req, sizeof old_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 7u, TFW_CMD_EEPROM_WRITE, at_old, 6u), wire);
    check(refused, sizeof refused - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 7u, TFW_CMD_EEPROM_WRITE, TFW_ERR_PROTECTED_REGION, 0, 0u), wire);
    check(new_req, sizeof new_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 8u, TFW_CMD_EEPROM_WRITE, at_new, 6u), wire);
    check(ok, sizeof ok - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 8u, TFW_CMD_EEPROM_WRITE, TFW_OK, 0, 0u), wire);
}

static void test_peer_binding_refusals_match_the_vectors(void)
{
    static const uint8_t auth[]  = TFWV_FRAME_EEPROM_WRITE_NETCFG_AUTH_REQUIRED_RESP;
    static const uint8_t begin[] = TFWV_FRAME_OTA_BEGIN_REQ;
    static const uint8_t over[]  = TFWV_FRAME_OTA_BEGIN_OVER_BUDGET_RESP;
    const uint8_t total[4] = { 0x00u, 0x04u, 0x00u, 0x00u };
    uint8_t wire[TFW_MAX_FRAME];

    check(auth, sizeof auth - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 8u, TFW_CMD_EEPROM_WRITE, TFW_ERR_AUTH_REQUIRED, 0, 0u), wire);
    check(begin, sizeof begin - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 9u, TFW_CMD_OTA_BEGIN, total, 4u), wire);
    check(over, sizeof over - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 9u, TFW_CMD_OTA_BEGIN, TFW_ERR_BAD_STATE, 0, 0u), wire);
}

/* The HSM channel's frames. The state payloads are written here from the
 * design, not taken from hsm_cmd.c. */
static void test_hsm_channel_frames_match_the_vectors(void)
{
    static const uint8_t hb_req[]       = TFWV_FRAME_HSM_HEARTBEAT_REQ;
    static const uint8_t hb_resp[]      = TFWV_FRAME_HSM_HEARTBEAT_RESP;
    static const uint8_t rep_req[]      = TFWV_FRAME_HSM_REPORT_REQ;
    static const uint8_t rep_resp[]     = TFWV_FRAME_HSM_REPORT_RESP;
    static const uint8_t rep_range[]    = TFWV_FRAME_HSM_REPORT_RANGE_RESP;
    static const uint8_t ver_req[]      = TFWV_FRAME_HSM_VERDICT_REQ;
    static const uint8_t sel_req[]      = TFWV_FRAME_HSM_SELECT_REQ;
    static const uint8_t sel_resp[]     = TFWV_FRAME_HSM_SELECT_RESP;
    static const uint8_t load_req[]     = TFWV_FRAME_HSM_LOAD_REQ;
    static const uint8_t load_range[]   = TFWV_FRAME_HSM_LOAD_RANGE_RESP;
    static const uint8_t rec_req[]      = TFWV_FRAME_HSM_RECORD_READ_REQ;
    static const uint8_t rec_resp[]     = TFWV_FRAME_HSM_RECORD_READ_RESP;
    static const uint8_t rec_range[]    = TFWV_FRAME_HSM_RECORD_READ_RANGE_RESP;
    static const uint8_t rec_index[1]   = { 0x1fu };
    static const uint8_t rec_entry[10]  = { 0x20u, 0x02u, 0x01u, 0x04u, 0x03u,
                                            0x02u, 0x01u, 0x03u, 0x05u, 0x0bu };
    static const uint8_t state_hb[6]    = { 0x01u, 0u, 0x02u, 0x01u, 0u, 0u };
    static const uint8_t state_lock[6]  = { 0x03u, 0u, 0u, 0u, 0u, 0u };
    static const uint8_t state_set[6]   = { 0x01u, 0u, 0u, 0u, 0u, 0u };
    static const uint8_t chunk[8]       = { 0x0Eu, 0x01u, 0u, 0u, 0xD8u, 0x6Bu, 0xA3u, 0x02u };
    uint8_t              s4             = 4u;
    uint8_t              compromised    = 1u;
    uint8_t              policy         = 0u;
    uint8_t              wire[TFW_MAX_FRAME];

    check(hb_req, sizeof hb_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_HSM_HEARTBEAT, 0, 0u), wire);
    check(hb_resp, sizeof hb_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 1u, TFW_CMD_HSM_HEARTBEAT, TFW_OK, state_hb, 6u), wire);
    check(rep_req, sizeof rep_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_HSM_REPORT, &s4, 1u), wire);
    check(rep_resp, sizeof rep_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 2u, TFW_CMD_HSM_REPORT, TFW_OK, state_lock, 6u), wire);
    check(rep_range, sizeof rep_range - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 2u, TFW_CMD_HSM_REPORT, TFW_ERR_RANGE, 0, 0u), wire);
    check(ver_req, sizeof ver_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 3u, TFW_CMD_HSM_VERDICT, &compromised, 1u), wire);
    check(sel_req, sizeof sel_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 4u, TFW_CMD_HSM_SELECT, &policy, 1u), wire);
    check(sel_resp, sizeof sel_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 4u, TFW_CMD_HSM_SELECT, TFW_OK, state_set, 6u), wire);
    check(load_req, sizeof load_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 5u, TFW_CMD_HSM_LOAD, chunk, sizeof chunk), wire);
    check(load_range, sizeof load_range - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 5u, TFW_CMD_HSM_LOAD, TFW_ERR_RANGE, 0, 0u), wire);
    check(rec_req, sizeof rec_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 6u, TFW_CMD_HSM_RECORD_READ, rec_index, sizeof rec_index), wire);
    check(rec_resp, sizeof rec_resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 6u, TFW_CMD_HSM_RECORD_READ, TFW_OK, rec_entry, sizeof rec_entry), wire);
    check(rec_range, sizeof rec_range - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 7u, TFW_CMD_HSM_RECORD_READ, TFW_ERR_RANGE, 0, 0u), wire);
}

static void test_prov_write_pubkey_frames_match_the_vectors(void)
{
    static const uint8_t x_req[]     = TFWV_FRAME_PROV_WRITE_PUBKEY_X_REQ;
    static const uint8_t y_req[]     = TFWV_FRAME_PROV_WRITE_PUBKEY_Y_REQ;
    static const uint8_t resp[]      = TFWV_FRAME_PROV_WRITE_PUBKEY_RESP;
    static const uint8_t range[]     = TFWV_FRAME_PROV_WRITE_PUBKEY_RANGE_RESP;
    static const uint8_t protected[] = TFWV_FRAME_PROV_WRITE_PUBKEY_PROTECTED_RESP;
    uint8_t              req[34];
    uint8_t              key[64];
    uint8_t              wire[TFW_MAX_FRAME];
    uint8_t              i;

    for (i = 0u; i < 32u; i++) {
        key[i]       = (uint8_t)(0x10u + i);
        key[32u + i] = (uint8_t)(0x80u + i);
    }
    req[0] = 15u;
    req[1] = 0u;
    memcpy(&req[2], key, 32u);
    check(x_req, sizeof x_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 7u, TFW_CMD_PROV_WRITE_PUBKEY, req, 34u), wire);
    req[1] = 1u;
    memcpy(&req[2], &key[32], 32u);
    check(y_req, sizeof y_req - 1u,
          tfw_frame_encode_request(wire, sizeof wire, 8u, TFW_CMD_PROV_WRITE_PUBKEY, req, 34u), wire);
    check(resp, sizeof resp - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 8u, TFW_CMD_PROV_WRITE_PUBKEY, TFW_OK, key, 64u), wire);
    check(range, sizeof range - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 7u, TFW_CMD_PROV_WRITE_PUBKEY, TFW_ERR_RANGE, 0, 0u), wire);
    check(protected, sizeof protected - 1u,
          tfw_frame_encode_response(wire, sizeof wire, 7u, TFW_CMD_PROV_WRITE_PUBKEY,
                                    TFW_ERR_PROTECTED_REGION, 0, 0u), wire);
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
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_CRC, e);
    TEST_ASSERT_EQUAL_UINT16(1u, f.err_crc);
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

static void test_a_corrupted_state_is_recovered_not_wedged(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_PING, (const uint8_t *)"ab", 2u);

    f.state = 0xFFu;
    f.want  = 9u;
    f.got   = 4u;
    TEST_ASSERT_EQUAL(TFW_FRAME_BAD_FORMAT, tfw_framer_push(&f, 0x00u));
    TEST_ASSERT_EQUAL_UINT8(TFW_FRAME_WAIT_SOF, f.state);
    TEST_ASSERT_EQUAL_UINT8(0u, f.want);
    TEST_ASSERT_EQUAL_UINT8(0u, f.got);
    TEST_ASSERT_EQUAL(TFW_FRAME_READY, feed(wire, n));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request_round_trips);
    RUN_TEST(test_response_sets_the_response_bit_and_carries_status);
    RUN_TEST(test_bad_state_response_matches_the_vector);
    RUN_TEST(test_no_store_response_matches_the_vector);
    RUN_TEST(test_ota_control_frames_match_the_vectors);
    RUN_TEST(test_eeprom_write_frames_match_the_vectors);
    RUN_TEST(test_peer_binding_refusals_match_the_vectors);
    RUN_TEST(test_hsm_channel_frames_match_the_vectors);
    RUN_TEST(test_prov_write_pubkey_frames_match_the_vectors);
    RUN_TEST(test_event_uses_the_reserved_op_and_no_sequence);
    RUN_TEST(test_empty_payload_is_a_valid_frame);
    RUN_TEST(test_maximum_payload_is_a_valid_frame);
    RUN_TEST(test_encoder_refuses_an_oversize_payload);
    RUN_TEST(test_encoder_refuses_a_buffer_it_would_overrun);
    RUN_TEST(test_a_corrupted_payload_byte_is_caught_by_the_crc);
    RUN_TEST(test_a_corrupted_length_byte_is_caught);
    RUN_TEST(test_a_false_sof_does_not_swallow_the_frame_that_follows_it);
    RUN_TEST(test_leading_garbage_before_a_frame_is_discarded);
    RUN_TEST(test_abort_recovers_from_a_truncated_frame);
    RUN_TEST(test_random_bytes_never_overrun_or_wedge_the_framer);
    RUN_TEST(test_a_corrupted_state_is_recovered_not_wedged);
    return UNITY_END();
}
