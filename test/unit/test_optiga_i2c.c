/*
 * The OPTIGA Trust M transport (src/crypto/optiga/optiga_i2c.c) against a
 * fake bus that replays recorded frames: no element is attached.
 *
 * A script is the frames on the wire in order. 'W' is a frame the host must
 * write to DATA, byte for byte; 'R' is a frame the element has ready. The
 * fake answers I2C_STATE with RESP_RDY and the frame's length while the next
 * step is an 'R', and with nothing otherwise, which is how silence and
 * timeouts are made. 'S' is I2C_STATE alone announcing a frame of its length,
 * which no DATA read follows -- a length no frame may have is how it is used.
 * 'X' is the bus lost: every access from there on is refused, as an element
 * that is gone refuses it. Every read must follow a write of its register
 * address (Figure 2-3), and every access goes to 0x30.
 *
 * The frames are literals, computed outside this code with §8.1.1's bitwise
 * reference CRC, high byte first on the wire (see optiga_i2c.h on the FCS).
 * The specification's own pinned value is the acknowledge for frame 0,
 * 80 00 00 0C EC. They follow the host library's numbering: both counters are
 * 3 after a resync, so the first data frame each side sends is number 0.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "optiga_i2c.h"
#include "tfw/hal.h"

/* ---- frames -------------------------------------------------------------- */

#define RESYNC  0xC0, 0x00, 0x00, 0x0A, 0x9A
#define ACK0    0x80, 0x00, 0x00, 0x0C, 0xEC
#define ACK1    0x81, 0x00, 0x00, 0x56, 0x30
#define ACK2    0x82, 0x00, 0x00, 0xB9, 0x54
#define ACK3    0x83, 0x00, 0x00, 0xE3, 0x88
#define NAK0    0xA0, 0x00, 0x00, 0x0F, 0xD7

/* The request 01 00 00 02 E0 E0 as data frame 0 (ACK 3) and data frame 1 (ACK 0). */
#define Q0      0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0, 0x57, 0x63
#define Q1      0x04, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0, 0x93, 0x96
/* The response 00 00 00 02 CA FE: frame 0 (ACK 0); frame 1 acknowledging frame 1;
 * frame 3, a repeat of one already taken; a corrupted FCS. */
#define A0      0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x00, 0x9D
#define A1_ACK1 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x9C, 0x26
#define A3_OLD  0x0C, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0xD9, 0x28
#define A0_BAD  0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x00, 0x9C
/* The same response with PRESENCE set, and as the last packet of a chain never begun. */
#define A0_PRES 0x00, 0x00, 0x07, 0x08, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0xDE, 0x71
#define A0_LAST 0x00, 0x00, 0x07, 0x04, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x6F, 0xEB
/* A control frame that carries a byte, which a control frame may not. */
#define CTL_LEN 0x80, 0x00, 0x01, 0x00, 0x34, 0xB6
/* The host's chain error, PCTR 07 alone, as data frame 1. */
#define Q1_CERR 0x04, 0x00, 0x01, 0x07, 0x1F, 0x8B

/* At DATA_REG_LEN 16: the request 10..28 (25 bytes) as FIRST, MID, LAST ... */
#define CQ0 0x03, 0x00, 0x0B, 0x01, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xE1, 0x94
#define CQ1 0x07, 0x00, 0x0B, 0x02, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x68, 0x57
#define CQ2 0x0B, 0x00, 0x06, 0x04, 0x24, 0x25, 0x26, 0x27, 0x28, 0x5B, 0xBA
/* ... and the response 40..4D (14 bytes) as FIRST, LAST. */
#define CA0 0x02, 0x00, 0x0B, 0x01, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0xAB, 0x62
#define CA1 0x06, 0x00, 0x05, 0x04, 0x4A, 0x4B, 0x4C, 0x4D, 0xB7, 0xC8

/* At DATA_REG_LEN 16, the response 50..65 (22 bytes) as FIRST, MID, LAST. */
#define MA0 0x00, 0x00, 0x0B, 0x01, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0xA0, 0x15
#define MA1 0x04, 0x00, 0x0B, 0x02, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x29, 0xD6
#define MA2 0x08, 0x00, 0x03, 0x04, 0x64, 0x65, 0x51, 0x6A
/* Chains the host must refuse: FIRST again, or NONE, inside one begun by MA0 ... */
#define MA1_FIRST 0x04, 0x00, 0x0B, 0x01, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x53, 0xAC
#define MA1_NONE  0x04, 0x00, 0x03, 0x00, 0xCA, 0xFE, 0x1C, 0x4A
/* ... the host's chain error for those, as data frame 1 acknowledging frame 1 ... */
#define Q1_CERR1  0x05, 0x00, 0x01, 0x07, 0x03, 0x30
/* ... and as frame 0, a FIRST short of full, a MID with no chain, the reserved
 * chain value 3, and the element's own chain error. */
#define A0_FIRST  0x00, 0x00, 0x03, 0x01, 0xCA, 0xFE, 0x56, 0x3A
#define A0_MID    0x00, 0x00, 0x03, 0x02, 0xCA, 0xFE, 0xB9, 0x5E
#define A0_CHAIN3 0x00, 0x00, 0x03, 0x03, 0xCA, 0xFE, 0xE3, 0x82
#define A0_CERR   0x00, 0x00, 0x01, 0x07, 0x6D, 0x67
/* The response as frame 0 still acknowledging frame 3: an answer to no question. */
#define A0_ACK3   0x03, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x74, 0xF4
/* Frames whose FCS is right and whose header is not: LEN 1 on a frame of none,
 * the RFU bit, SEQCTR 11, a data frame with no PCTR, and a data frame with the
 * resync sequence. */
#define CTL_FLEN  0x80, 0x00, 0x01, 0x1D, 0x65
#define CTL_RFU   0x90, 0x00, 0x00, 0x89, 0x79
#define CTL_SEQ3  0xE0, 0x00, 0x00, 0x09, 0xA1
#define D_EMPTY   0x00, 0x00, 0x00, 0x00, 0x00
#define D_RESYNC  0x40, 0x00, 0x01, 0x00, 0x0F, 0x6F

static const uint8_t apdu[] = { 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0 };
static const uint8_t reply[] = { 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE };

/* ---- the bus ------------------------------------------------------------- */

typedef struct {
    char    dir;
    uint8_t len;
    uint8_t b[16];
} step_t;

#define W(...) { 'W', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define R(...) { 'R', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define S(n)    { 'S', (uint8_t)(n), { 0 } }
#define X       { 'X', 0u, { 0 } }
#define PLAY(s) play((s), (unsigned)(sizeof (s) / sizeof (s)[0]))

static const step_t *script;
static unsigned      steps, at;
static uint8_t       reg;          /* the register address last written; 0 after a read */
static uint16_t      reg_len;      /* what the element reads back from DATA_REG_LEN */
static uint16_t      asked;        /* what the host wrote to DATA_REG_LEN */
static unsigned      nacks;        /* accesses to refuse before answering */
static unsigned      xfers;
static unsigned      idle_polls;   /* I2C_STATE reads with nothing ready */
static uint32_t      init_hz;
static uint32_t      delayed_us;

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    init_hz = hz;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                              uint8_t *rd, uint8_t rlen)
{
    TEST_ASSERT_EQUAL_HEX8(0x30u, addr7);
    TEST_ASSERT_TRUE_MESSAGE(wlen == 0u || rlen == 0u, "a repeated start");
    xfers++;
    if (at < steps && script[at].dir == 'X') {
        return TFW_ERR_NACK;
    }
    if (nacks) {
        nacks--;
        return TFW_ERR_NACK;
    }
    if (wlen == 1u) {
        reg = wr[0];
        return TFW_OK;
    }
    if (wlen) {
        if (wr[0] == 0x81u) {
            TEST_ASSERT_EQUAL_UINT8(3u, wlen);
            asked = (uint16_t)((wr[1] << 8) | wr[2]);
            return TFW_OK;
        }
        TEST_ASSERT_EQUAL_HEX8(0x80u, wr[0]);
        TEST_ASSERT_TRUE_MESSAGE(at < steps && script[at].dir == 'W',
                                 "the host wrote a frame the script does not hold");
        TEST_ASSERT_EQUAL_UINT8(script[at].len, wlen - 1u);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(script[at].b, &wr[1], script[at].len);
        at++;
        return TFW_OK;
    }

    switch (reg) {
    case 0x81u:
        TEST_ASSERT_EQUAL_UINT8(2u, rlen);
        rd[0] = (uint8_t)(reg_len >> 8);
        rd[1] = (uint8_t)(reg_len & 0xFFu);
        break;
    case 0x82u:
        TEST_ASSERT_EQUAL_UINT8(4u, rlen);
        memset(rd, 0, 4u);
        if (at < steps && (script[at].dir == 'R' || script[at].dir == 'S')) {
            rd[0] = 0x40u;  /* RESP_RDY */
            rd[3] = script[at].len;
            if (script[at].dir == 'S') {
                at++;
            }
        } else {
            idle_polls++;
        }
        break;
    case 0x80u:
        TEST_ASSERT_TRUE_MESSAGE(at < steps && script[at].dir == 'R',
                                 "the host read DATA with no frame ready");
        TEST_ASSERT_EQUAL_UINT8(script[at].len, rlen);
        memcpy(rd, script[at].b, rlen);
        at++;
        break;
    default:
        TEST_FAIL_MESSAGE("a read with no register address written before it");
    }
    reg = 0u;
    return TFW_OK;
}

void tfw_hal_delay_us(uint16_t us) { delayed_us += us; }

/* ---- helpers ------------------------------------------------------------- */

static void play(const step_t *s, unsigned n)
{
    script = s;
    steps  = n;
    at     = 0u;
}

static void played(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(steps, at, "frames of the script never went over the wire");
}

static void open_at(uint16_t len)
{
    static const step_t s[] = { W(RESYNC) };

    reg_len = len;
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_open());
    played();
}

static uint8_t  rsp[64];
static uint16_t rlen;

static tfw_status_t exchange(uint16_t rcap)
{
    return tfw_optiga_transceive(apdu, (uint16_t)sizeof apdu, rsp, rcap, &rlen, 5u);
}

static void got_reply(void)
{
    TEST_ASSERT_EQUAL_UINT16(sizeof reply, rlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reply, rsp, sizeof reply);
}

static uint32_t rng = 0x0971CA30u;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* §8.1.1, the reference implementation, seed 0. */
static uint16_t fcs_reference(const uint8_t *d, unsigned n)
{
    uint16_t seed = 0u;

    while (n--) {
        uint8_t c = *d++;
        for (int i = 0; i < 8; i++) {
            seed = (uint16_t)(((seed ^ c) & 0x01u) ? (seed >> 1) ^ 0x8408u : seed >> 1);
            c    = (uint8_t)(c >> 1);
        }
    }
    return seed;
}

void setUp(void)
{
    play(0, 0u);
    reg        = 0u;
    reg_len    = 64u;
    asked      = 0u;
    nacks      = 0u;
    xfers      = 0u;
    idle_polls = 0u;
    init_hz    = 0u;
    delayed_us = 0u;
    rlen       = 0xFFFFu;
    memset(rsp, 0xA5, sizeof rsp);
}
void tearDown(void) { }

/* ---- tests --------------------------------------------------------------- */

static void test_fcs_is_the_reference_crc(void)
{
    static const uint8_t ack0[] = { 0x80u, 0x00u, 0x00u };
    uint8_t  d[64];
    unsigned runs;

    TEST_ASSERT_EQUAL_HEX16(0x0CECu, tfw_optiga_fcs(ack0, 3u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, tfw_optiga_fcs(ack0, 0u));
    for (runs = 0u; runs < 2000u; runs++) {
        unsigned n = xorshift32() % (sizeof d + 1u);
        for (unsigned i = 0u; i < n; i++) {
            d[i] = (uint8_t)xorshift32();
        }
        TEST_ASSERT_EQUAL_HEX16(fcs_reference(d, n), tfw_optiga_fcs(d, (uint16_t)n));
    }
    TEST_ASSERT_EQUAL_UINT(2000u, runs);
}

static void test_open_asks_64_and_resyncs(void)
{
    open_at(64u);
    TEST_ASSERT_EQUAL_UINT16(64u, asked);
    TEST_ASSERT_EQUAL_UINT32(TFW_ELEMENT_I2C_HZ, init_hz);
    /* The guard time before every access. */
    TEST_ASSERT_EQUAL_UINT32(xfers * 500u, delayed_us);
}

static void test_open_refuses_a_length_out_of_range_and_stays_closed(void)
{
    static const uint16_t bad[] = { 0x000Fu, 0x0041u, 0x0100u, 0xFFFFu };

    for (unsigned i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        setUp();
        reg_len = bad[i];
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_open());
        TEST_ASSERT_EQUAL_UINT(0u, at);  /* no resync was sent */
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, exchange(sizeof rsp));
}

static void test_open_repeats_a_late_acknowledge_then_gives_up(void)
{
    static const step_t s[] = { W(RESYNC) };

    nacks = 3u;
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_open());
    played();

    setUp();
    nacks = 1000u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, tfw_optiga_open());
    TEST_ASSERT_EQUAL_UINT(200u, xfers);
}

static void test_an_empty_apdu_is_refused(void)
{
    unsigned before;

    open_at(64u);
    before = xfers;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_optiga_transceive(apdu, 0u, rsp, sizeof rsp, &rlen, 5u));
    TEST_ASSERT_EQUAL_UINT(before, xfers);
}

static void test_two_exchanges_count_frames_and_take_a_piggybacked_ack(void)
{
    static const step_t s[] = {
        W(Q0), R(ACK0), R(A0), W(ACK0),
        W(Q1), R(A1_ACK1), W(ACK1),
    };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    got_reply();
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    got_reply();
    played();
}

static void test_a_chain_each_way_at_the_smallest_register(void)
{
    static const step_t s[] = {
        W(CQ0), R(ACK0), W(CQ1), R(ACK1), W(CQ2), R(ACK2),
        R(CA0), W(ACK0), R(CA1), W(ACK1),
    };
    uint8_t q[25];

    for (unsigned i = 0u; i < sizeof q; i++) {
        q[i] = (uint8_t)(0x10u + i);
    }
    open_at(16u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_transceive(q, sizeof q, rsp, sizeof rsp, &rlen, 5u));
    played();
    TEST_ASSERT_EQUAL_UINT16(14u, rlen);
    for (unsigned i = 0u; i < 14u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x40u + i, rsp[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, rsp[14]);
}

static void test_a_nak_repeats_the_frame_unchanged(void)
{
    static const step_t s[] = { W(Q0), R(NAK0), W(Q0), R(ACK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_a_bad_fcs_is_naked_and_the_repeat_taken(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A0_BAD), W(NAK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_a_frame_already_taken_is_acked_again_and_dropped(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A3_OLD), W(ACK3), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_a_control_frame_with_a_packet_is_discarded(void)
{
    static const step_t s[] = { W(Q0), R(CTL_LEN), R(ACK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_silence_repeats_three_times_then_resyncs(void)
{
    static const step_t s[] = { W(Q0), W(Q0), W(Q0), W(Q0), W(RESYNC) };
    static const step_t again[] = { W(Q0), R(ACK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, exchange(sizeof rsp));
    played();
    TEST_ASSERT_EQUAL_UINT(4u * 6u, idle_polls);  /* four waits of 5 ms, 6 polls each */

    /* The resync put both counters back: the next request is frame 0 again. */
    PLAY(again);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
}

static void test_no_response_after_the_ack_times_out(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, exchange(sizeof rsp));
    played();
    TEST_ASSERT_EQUAL_UINT(4u * 6u, idle_polls);
}

static void test_a_resync_from_the_element_ends_the_exchange(void)
{
    static const step_t s[] = { W(Q0), R(RESYNC), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();
}

static void test_a_response_longer_than_the_buffer(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A0), W(ACK0), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TOO_LONG, exchange(sizeof reply - 1u));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, rsp[0]);
}

static void test_the_presentation_layer_is_unsupported(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A0_PRES), W(ACK0), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, exchange(sizeof rsp));
    played();
}

static void test_a_broken_chain_is_refused_to_the_element(void)
{
    static const step_t s[] = {
        W(Q0), R(ACK0), R(A0_LAST), W(ACK0), W(Q1_CERR), R(ACK1), W(RESYNC),
    };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();
}

static void test_a_response_chain_with_a_middle_packet(void)
{
    static const step_t s[] = {
        W(Q0), R(ACK0), R(MA0), W(ACK0), R(MA1), W(ACK1), R(MA2), W(ACK2),
    };

    open_at(16u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    TEST_ASSERT_EQUAL_UINT16(22u, rlen);
    for (unsigned i = 0u; i < 22u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x50u + i, rsp[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, rsp[22]);
}

/* §5.2: a chain the element breaks is refused to it with a chain error. */
static void test_a_bad_first_packet_is_refused_to_the_element(void)
{
    static const step_t first[] = {
        W(Q0), R(ACK0), R(A0_FIRST), W(ACK0), W(Q1_CERR), R(ACK1), W(RESYNC),
    };
    static const step_t mid[] = {
        W(Q0), R(ACK0), R(A0_MID), W(ACK0), W(Q1_CERR), R(ACK1), W(RESYNC),
    };
    static const step_t chain3[] = {
        W(Q0), R(ACK0), R(A0_CHAIN3), W(ACK0), W(Q1_CERR), R(ACK1), W(RESYNC),
    };
    const step_t *const scripts[] = { first, mid, chain3 };

    for (unsigned i = 0u; i < 3u; i++) {
        setUp();
        open_at(64u);
        play(scripts[i], 7u);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
        played();
    }
}

static void test_a_chain_begun_twice_or_left_unended_is_refused(void)
{
    static const step_t again[] = {
        W(Q0), R(ACK0), R(MA0), W(ACK0), R(MA1_FIRST), W(ACK1),
        W(Q1_CERR1), R(ACK1), W(RESYNC),
    };
    static const step_t none[] = {
        W(Q0), R(ACK0), R(MA0), W(ACK0), R(MA1_NONE), W(ACK1),
        W(Q1_CERR1), R(ACK1), W(RESYNC),
    };

    open_at(16u);
    PLAY(again);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();

    setUp();
    open_at(16u);
    PLAY(none);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();
}

/* The element's own chain error is not answered with ours: a resync ends it. */
static void test_a_chain_error_from_the_element_ends_the_exchange(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A0_CERR), W(ACK0), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();
}

/* Table 3-3: a length no frame has, or one past the register, is NAKed unread;
 * both while the request waits for its acknowledge and after. */
static void test_a_length_out_of_range_is_naked_unread(void)
{
    static const step_t s[] = {
        W(Q0), S(4u), W(NAK0), R(ACK0), S(65u), W(NAK0), R(A0), W(ACK0),
    };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_a_header_the_fcs_passes_but_the_layout_does_not_is_naked(void)
{
    static const step_t s[] = {
        W(Q0), R(CTL_FLEN), W(NAK0), R(D_EMPTY), W(NAK0), R(ACK0),
        R(CTL_RFU), W(NAK0), R(CTL_SEQ3), W(NAK0), R(D_RESYNC), W(NAK0),
        R(A0), W(ACK0),
    };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
}

static void test_an_answer_that_acknowledges_no_question_is_refused(void)
{
    static const step_t s[] = { W(Q0), R(A0_ACK3), W(ACK0), W(RESYNC) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, exchange(sizeof rsp));
    played();
}

/* A0 acknowledges CQ0 and answers it while two packets of the request are
 * still to go. */
static void test_an_answer_before_the_request_is_whole_is_refused(void)
{
    static const step_t s[] = { W(CQ0), R(A0), W(ACK0), W(RESYNC) };
    uint8_t q[25];

    for (unsigned i = 0u; i < sizeof q; i++) {
        q[i] = (uint8_t)(0x10u + i);
    }
    open_at(16u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING,
                          tfw_optiga_transceive(q, sizeof q, rsp, sizeof rsp, &rlen, 5u));
    played();
}

/* The bus lost at each access the exchange makes: sending, polling I2C_STATE,
 * reading DATA, and polling for the response. Each returns the bus's own
 * status, and the resync after it is refused the same. */
static void test_a_lost_bus_returns_its_status_at_every_access(void)
{
    static const step_t on_send[]  = { X };
    static const step_t on_state[] = { W(Q0), X };
    static const step_t on_data[]  = { W(Q0), S(12u), X };
    static const step_t on_reply[] = { W(Q0), R(ACK0), X };
    const step_t *const scripts[]  = { on_send, on_state, on_data, on_reply };
    static const unsigned n[]      = { 1u, 2u, 3u, 3u };

    for (unsigned i = 0u; i < 4u; i++) {
        setUp();
        open_at(64u);
        play(scripts[i], n[i]);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, exchange(sizeof rsp));
        TEST_ASSERT_EQUAL_UINT(n[i] - 1u, at);  /* everything before the loss */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fcs_is_the_reference_crc);
    RUN_TEST(test_open_asks_64_and_resyncs);
    RUN_TEST(test_open_refuses_a_length_out_of_range_and_stays_closed);
    RUN_TEST(test_open_repeats_a_late_acknowledge_then_gives_up);
    RUN_TEST(test_an_empty_apdu_is_refused);
    RUN_TEST(test_two_exchanges_count_frames_and_take_a_piggybacked_ack);
    RUN_TEST(test_a_chain_each_way_at_the_smallest_register);
    RUN_TEST(test_a_nak_repeats_the_frame_unchanged);
    RUN_TEST(test_a_bad_fcs_is_naked_and_the_repeat_taken);
    RUN_TEST(test_a_frame_already_taken_is_acked_again_and_dropped);
    RUN_TEST(test_a_control_frame_with_a_packet_is_discarded);
    RUN_TEST(test_silence_repeats_three_times_then_resyncs);
    RUN_TEST(test_no_response_after_the_ack_times_out);
    RUN_TEST(test_a_resync_from_the_element_ends_the_exchange);
    RUN_TEST(test_a_response_longer_than_the_buffer);
    RUN_TEST(test_the_presentation_layer_is_unsupported);
    RUN_TEST(test_a_broken_chain_is_refused_to_the_element);
    RUN_TEST(test_a_response_chain_with_a_middle_packet);
    RUN_TEST(test_a_bad_first_packet_is_refused_to_the_element);
    RUN_TEST(test_a_chain_begun_twice_or_left_unended_is_refused);
    RUN_TEST(test_a_chain_error_from_the_element_ends_the_exchange);
    RUN_TEST(test_a_length_out_of_range_is_naked_unread);
    RUN_TEST(test_a_header_the_fcs_passes_but_the_layout_does_not_is_naked);
    RUN_TEST(test_an_answer_that_acknowledges_no_question_is_refused);
    RUN_TEST(test_an_answer_before_the_request_is_whole_is_refused);
    RUN_TEST(test_a_lost_bus_returns_its_status_at_every_access);
    return UNITY_END();
}
