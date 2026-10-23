/*
 * Seeded mutation of the ATECC608A driver's I2C transactions
 * (src/crypto/atecc608a/atecc.c), with the bus faked in this file rather than
 * the software element: the element model only ever answers well, and the
 * driver's job is to refuse every answer that is not. Every status byte, every
 * count byte, every bit flip with and without a resealed CRC, random answers,
 * every out-of-range payload length, and the poll bound at its edge, through
 * tfw_atecc_exec, tfw_atecc_verify_external and tfw_atecc_wake.
 *
 * The oracle is the datasheet's framing as bytes: a command is word address
 * 0x03, a count of 7 + data, opcode, P1, P2 little-endian, data, then a CRC
 * over the count onward; an answer is a count, its bytes, and a CRC over all
 * but the last two. A count of 4 is a status, translated by the literal table
 * below; any other count must be exactly rlen + 3. An answer that is refused
 * leaves the caller's buffer untouched byte for byte; one that is accepted
 * copies exactly its data bytes.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "atecc.h"
#include "tfw/hal.h"

#define RANDOM_RUNS 3000u
#define SENTINEL    0xA5u

/* ---- the bus ------------------------------------------------------------ */

static uint8_t      wr_addr[8], wr_buf[8][160], wr_len[8];
static unsigned     writes;
static tfw_status_t cmd_status;          /* what a write to the element returns */
static unsigned     nacks;               /* reads to refuse before answering */
static unsigned     reads;
static uint8_t      read_len;
static uint8_t      answer[256];
static uint32_t     init_hz;
static unsigned     inits;
static uint32_t     delayed_us;

/* Where the last write was sent from: tfw_atecc_exec's file-static buf, which
 * outlives the call, so a test can read back what was left in it. */
static const uint8_t *wr_from;

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    init_hz = hz;
    inits++;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                              uint8_t *rd, uint8_t rlen)
{
    if (wlen) {
        TEST_ASSERT_TRUE(writes < 8u);
        wr_addr[writes] = addr7;
        wr_len[writes]  = wlen;
        memcpy(wr_buf[writes], wr, wlen);
        wr_from = wr;
        writes++;
        /* The wake pulse goes to 0x00 and is NACKed on a real bus. */
        return addr7 == 0x00u ? TFW_ERR_NACK : cmd_status;
    }
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_ADDR, addr7);
    reads++;
    read_len = rlen;
    if (nacks) {
        nacks--;
        memset(rd, 0xEE, rlen);     /* a NACKed read is garbage, never data */
        return TFW_ERR_NACK;
    }
    memcpy(rd, answer, rlen);
    return TFW_OK;
}

void tfw_hal_delay_us(uint16_t us) { delayed_us += us; }

/* ---- helpers ------------------------------------------------------------ */

static uint32_t rng = 0x6081A7C3u;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    writes = reads = nacks = inits = 0u;
    read_len   = 0u;
    cmd_status = TFW_OK;
    init_hz    = 0u;
    delayed_us = 0u;
    memset(answer, 0, sizeof answer);
}
void tearDown(void) { }

/* The datasheet's status table, as literals: 0x00 success, 0x03 parse error,
 * 0x0F execution error, 0xFF the part rejected our CRC, everything else -- a
 * miscompare, an ECC or self-test fault, the after-wake token, a code no
 * datasheet names -- a crypto failure. */
static tfw_status_t status_of(uint8_t code)
{
    if (code == 0x00u) return TFW_OK;
    if (code == 0x03u) return TFW_ERR_RANGE;
    if (code == 0x0Fu) return TFW_ERR_UNSUPPORTED;
    if (code == 0xFFu) return TFW_ERR_CRC;
    return TFW_ERR_CRYPTO;
}

static void seal(uint8_t *a, uint8_t n)
{
    uint16_t crc = tfw_atecc_crc16(a, (uint8_t)(n - 2u));

    a[n - 2u] = (uint8_t)crc;
    a[n - 1u] = (uint8_t)(crc >> 8);
}

static int crc_ok(const uint8_t *a, uint8_t n)
{
    uint16_t crc = tfw_atecc_crc16(a, (uint8_t)(n - 2u));

    return a[n - 2u] == (uint8_t)crc && a[n - 1u] == (uint8_t)(crc >> 8);
}

/* What exec must return for `answer` read as an rlen-byte response. */
static tfw_status_t expect_exec(uint8_t rlen)
{
    uint8_t n = rlen ? (uint8_t)(rlen + 3u) : 4u;

    if (answer[0] == 4u) {
        if (!crc_ok(answer, 4u)) {
            return TFW_ERR_CRC;
        }
        if (rlen && status_of(answer[1]) == TFW_OK) {
            return TFW_ERR_CRYPTO;
        }
        return status_of(answer[1]);
    }
    if (rlen == 0u || answer[0] != n) {
        return TFW_ERR_CRYPTO;
    }
    return crc_ok(answer, n) ? TFW_OK : TFW_ERR_CRC;
}

/* Runs exec against `answer` and checks the result and the caller's buffer. */
static tfw_status_t check_exec(uint8_t rlen)
{
    uint8_t      out[80];
    tfw_status_t want = expect_exec(rlen);
    tfw_status_t got;
    uint8_t      i;

    memset(out, SENTINEL, sizeof out);
    got = tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, rlen, 23u);
    TEST_ASSERT_EQUAL(want, got);
    TEST_ASSERT_EQUAL_UINT8(rlen ? rlen + 3u : 4u, read_len);
    if (got == TFW_OK && rlen) {
        TEST_ASSERT_EQUAL_MEMORY(&answer[1], out, rlen);
    }
    for (i = (got == TFW_OK) ? rlen : 0u; i < sizeof out; i++) {
        TEST_ASSERT_EQUAL_HEX8(SENTINEL, out[i]);
    }
    return got;
}

static void good_answer(uint8_t rlen)
{
    uint8_t i;

    answer[0] = (uint8_t)(rlen + 3u);
    for (i = 0u; i < rlen; i++) {
        answer[1u + i] = (uint8_t)(0x40u + i * 7u);
    }
    seal(answer, (uint8_t)(rlen + 3u));
}

static void status_answer(uint8_t code)
{
    answer[0] = 4u;
    answer[1] = code;
    seal(answer, 4u);
}

/* ---- exec: the command it frames ---------------------------------------- */

static void test_the_info_command_is_the_datasheets_bytes(void)
{
    static const uint8_t INFO[8] = { 0x03, 0x07, 0x30, 0x00, 0x00, 0x00, 0x03, 0x5D };
    uint8_t out[4];

    good_answer(4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_INFO, 0u, 0u, 0, 0u,
                                             out, 4u, TFW_ATECC_T_INFO));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_ADDR, wr_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(8u, wr_len[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(INFO, wr_buf[0], 8);
}

static void test_every_payload_length_is_framed(void)
{
    uint8_t  data[TFW_ATECC_MAX_DATA];
    uint8_t  dlen, i;
    unsigned runs = 0u;

    for (i = 0u; i < sizeof data; i++) {
        data[i] = (uint8_t)(i ^ 0x5Au);
    }
    for (dlen = 0u; dlen <= TFW_ATECC_MAX_DATA; dlen++) {
        uint8_t *f;
        uint16_t crc;

        setUp();
        status_answer(0x00u);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(0xC4u, 0x81u, 0xBEEFu, data, dlen,
                                                 0, 0u, 1u));
        TEST_ASSERT_EQUAL_UINT(1u, writes);
        f = wr_buf[0];
        TEST_ASSERT_EQUAL_UINT8(dlen + 8u, wr_len[0]);
        TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_WA_COMMAND, f[0]);
        TEST_ASSERT_EQUAL_UINT8(dlen + 7u, f[1]);
        TEST_ASSERT_EQUAL_HEX8(0xC4u, f[2]);
        TEST_ASSERT_EQUAL_HEX8(0x81u, f[3]);
        TEST_ASSERT_EQUAL_HEX8(0xEFu, f[4]);
        TEST_ASSERT_EQUAL_HEX8(0xBEu, f[5]);
        if (dlen) {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &f[6], dlen);
        }
        crc = tfw_atecc_crc16(&f[1], (uint8_t)(dlen + 5u));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, f[6u + dlen]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), f[7u + dlen]);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_ATECC_MAX_DATA + 1u, runs);
}

static void test_every_oversized_payload_is_refused_before_the_bus(void)
{
    static uint8_t data[256];
    unsigned       dlen, runs = 0u;

    for (dlen = TFW_ATECC_MAX_DATA + 1u; dlen <= 255u; dlen++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_atecc_exec(TFW_ATECC_OP_WRITE, 0u, 0u,
                                                        data, (uint8_t)dlen, 0, 0u, 45u));
        TEST_ASSERT_EQUAL_UINT(0u, writes);
        TEST_ASSERT_EQUAL_UINT(0u, reads);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(255u - TFW_ATECC_MAX_DATA, runs);
}

static void test_a_refused_command_write_is_returned_and_never_polled(void)
{
    static const tfw_status_t FAULTS[] = { TFW_ERR_NACK, TFW_ERR_TIMEOUT, TFW_ERR_BUSY };
    uint8_t  out[32];
    unsigned i;

    for (i = 0u; i < sizeof FAULTS / sizeof FAULTS[0]; i++) {
        setUp();
        cmd_status = FAULTS[i];
        good_answer(32u);
        memset(out, SENTINEL, sizeof out);
        TEST_ASSERT_EQUAL(FAULTS[i], tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u,
                                                    out, 32u, 23u));
        TEST_ASSERT_EQUAL_UINT(0u, reads);
        TEST_ASSERT_EACH_EQUAL_HEX8(SENTINEL, out, sizeof out);
    }
}

static void test_a_response_longer_than_the_buffer_is_refused_and_nothing_is_sent(void)
{
    static uint8_t out[256];
    const uint8_t *b;
    unsigned       rlen, runs = 0u;

    /* The edge: the largest response fills the buffer and is the data. */
    setUp();
    good_answer(TFW_ATECC_MAX_RESP);
    memset(out, SENTINEL, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_READ, 0u, 0u, 0, 0u,
                                             out, TFW_ATECC_MAX_RESP, 5u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_MAX_RESP + 3u, read_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&answer[1], out, TFW_ATECC_MAX_RESP);
    TEST_ASSERT_EACH_EQUAL_HEX8(SENTINEL, &out[TFW_ATECC_MAX_RESP],
                                sizeof out - TFW_ATECC_MAX_RESP);
    b = wr_from;

    /* One past it and beyond: refused before the packet is built. buf was
     * cleared by the call above, and a build would put the word address in
     * its first byte. */
    for (rlen = TFW_ATECC_MAX_RESP + 1u; rlen <= 255u; rlen++) {
        setUp();
        memset(out, SENTINEL, sizeof out);
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_atecc_exec(TFW_ATECC_OP_READ, 0u, 0u, 0, 0u,
                                                        out, (uint8_t)rlen, 5u));
        TEST_ASSERT_EQUAL_UINT(0u, writes);
        TEST_ASSERT_EQUAL_UINT(0u, reads);
        TEST_ASSERT_EACH_EQUAL_HEX8(SENTINEL, out, sizeof out);
        TEST_ASSERT_EACH_EQUAL_HEX8(0x00u, b, 8u);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(255u - TFW_ATECC_MAX_RESP, runs);
}

static void test_a_failed_command_write_leaves_the_shared_buffer_cleared(void)
{
    static const tfw_status_t FAULTS[] = { TFW_ERR_NACK, TFW_ERR_TIMEOUT, TFW_ERR_BUSY };
    uint8_t  data[32];
    unsigned i;

    for (i = 0u; i < sizeof data; i++) {
        data[i] = (uint8_t)(0xA5u ^ i);
    }
    for (i = 0u; i < sizeof FAULTS / sizeof FAULTS[0]; i++) {
        setUp();
        cmd_status = FAULTS[i];
        TEST_ASSERT_EQUAL(FAULTS[i], tfw_atecc_exec(TFW_ATECC_OP_WRITE, 0u, 0u,
                                                    data, sizeof data, 0, 0u, 45u));
        TEST_ASSERT_EQUAL_UINT(1u, writes);
        TEST_ASSERT_EQUAL_UINT(0u, reads);
        /* The packet carried the payload, and nothing of it is left behind. */
        TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &wr_buf[0][6], sizeof data);
        TEST_ASSERT_EACH_EQUAL_HEX8(0x00u, wr_from, wr_len[0]);
    }
}

/* ---- exec: the answer it reads ------------------------------------------ */

static void test_every_status_byte_with_no_data_expected(void)
{
    unsigned code, runs = 0u;

    for (code = 0u; code <= 0xFFu; code++) {
        setUp();
        status_answer((uint8_t)code);
        TEST_ASSERT_EQUAL(status_of((uint8_t)code), check_exec(0u));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(256u, runs);
}

static void test_every_status_byte_where_data_was_expected_is_a_refusal(void)
{
    unsigned code, runs = 0u;

    for (code = 0u; code <= 0xFFu; code++) {
        setUp();
        status_answer((uint8_t)code);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, check_exec(32u));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(256u, runs);
}

static void test_every_count_byte_under_a_resealed_crc(void)
{
    unsigned count, ok = 0u;

    for (count = 0u; count <= 0xFFu; count++) {
        setUp();
        good_answer(32u);
        answer[0] = (uint8_t)count;
        seal(answer, 35u);
        if (check_exec(32u) == TFW_OK) {
            ok++;
        }
    }
    /* 35 is the one count a 32-byte answer can carry. */
    TEST_ASSERT_EQUAL_UINT(1u, ok);
}

static void test_every_bit_flip_is_refused(void)
{
    static const uint8_t RLENS[] = { 0u, 1u, 4u, 32u, 64u };
    unsigned r, bit, runs = 0u;

    for (r = 0u; r < sizeof RLENS; r++) {
        uint8_t n = RLENS[r] ? (uint8_t)(RLENS[r] + 3u) : 4u;

        for (bit = 0u; bit < n * 8u; bit++) {
            setUp();
            if (RLENS[r]) {
                good_answer(RLENS[r]);
            } else {
                status_answer(0x00u);
            }
            answer[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            /* CRC-16 catches every single-bit error. */
            TEST_ASSERT_NOT_EQUAL(TFW_OK, check_exec(RLENS[r]));
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((4u + 4u + 7u + 35u + 67u) * 8u, runs);
}

static void test_every_data_bit_flip_under_a_resealed_crc_is_the_data(void)
{
    unsigned bit, runs = 0u;

    for (bit = 8u; bit < 33u * 8u; bit++) {
        setUp();
        good_answer(32u);
        answer[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        seal(answer, 35u);
        TEST_ASSERT_EQUAL(TFW_OK, check_exec(32u));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(32u * 8u, runs);
}

static void test_random_answers(void)
{
    unsigned i, ok = 0u, crc = 0u, other = 0u;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint8_t      rlen = (uint8_t)(xorshift32() % 65u);
        uint8_t      j;
        tfw_status_t st;

        setUp();
        for (j = 0u; j < 68u; j++) {
            answer[j] = (uint8_t)xorshift32();
        }
        /* A third keep a plausible count and a third a plausible CRC too, so
         * the paths past the count check are reached and not only refused. */
        switch (i % 3u) {
        case 1u:
            answer[0] = rlen ? (uint8_t)(rlen + 3u) : 4u;
            break;
        case 2u:
            answer[0] = rlen ? (uint8_t)(rlen + 3u) : 4u;
            seal(answer, answer[0]);
            break;
        default:
            break;
        }
        st = check_exec(rlen);
        if (st == TFW_OK) {
            ok++;
        } else if (st == TFW_ERR_CRC) {
            crc++;
        } else {
            other++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, ok + crc + other);
    TEST_ASSERT_TRUE(ok > 0u);
    TEST_ASSERT_TRUE(crc > 0u);
    TEST_ASSERT_TRUE(other > 0u);
}

/* ---- exec: the poll ------------------------------------------------------ */

static void test_the_poll_bound_at_its_edge(void)
{
    static const uint16_t EXEC_MS[] = { 0u, 1u, TFW_ATECC_T_READ, TFW_ATECC_T_RANDOM,
                                        TFW_ATECC_T_SIGN };
    unsigned i;

    for (i = 0u; i < sizeof EXEC_MS / sizeof EXEC_MS[0]; i++) {
        unsigned tries = EXEC_MS[i] * 2u + 4u;
        uint8_t  out[32];

        /* The last try answers. */
        setUp();
        good_answer(32u);
        nacks = tries - 1u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u,
                                                 out, 32u, EXEC_MS[i]));
        TEST_ASSERT_EQUAL_UINT(tries, reads);
        TEST_ASSERT_EQUAL_UINT32(tries * 500u, delayed_us);

        /* One NACK more and the part is broken, not busy. */
        setUp();
        good_answer(32u);
        nacks = tries;
        memset(out, SENTINEL, sizeof out);
        TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u,
                                                          out, 32u, EXEC_MS[i]));
        TEST_ASSERT_EQUAL_UINT(tries, reads);
        TEST_ASSERT_EACH_EQUAL_HEX8(SENTINEL, out, sizeof out);

        /* An early answer is read early. */
        setUp();
        good_answer(32u);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u,
                                                 out, 32u, EXEC_MS[i]));
        TEST_ASSERT_EQUAL_UINT(1u, reads);
    }
}

/* ---- verify_external ---------------------------------------------------- */

static uint8_t SIG[64], PUB[64];

static tfw_status_t run_verify(uint16_t exec_ms)
{
    uint8_t i;

    for (i = 0u; i < 64u; i++) {
        SIG[i] = (uint8_t)(i + 1u);
        PUB[i] = (uint8_t)(0xFFu - i);
    }
    return tfw_atecc_verify_external(SIG, PUB, exec_ms);
}

static void test_verify_external_frames_its_command(void)
{
    uint8_t *f;
    uint16_t crc;

    status_answer(0x00u);
    TEST_ASSERT_EQUAL(TFW_OK, run_verify(TFW_ATECC_T_VERIFY));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    f = wr_buf[0];
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_ADDR, wr_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(136u, wr_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, f[0]);
    TEST_ASSERT_EQUAL_UINT8(135u, f[1]);
    TEST_ASSERT_EQUAL_HEX8(0x45u, f[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, f[3]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, f[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, f[5]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SIG, &f[6], 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PUB, &f[70], 64);
    crc = tfw_atecc_crc16(&f[1], 133u);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, f[134]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), f[135]);
    TEST_ASSERT_EQUAL_UINT8(4u, read_len);
}

static void test_verify_external_every_status_byte(void)
{
    unsigned code, runs = 0u;

    for (code = 0u; code <= 0xFFu; code++) {
        setUp();
        status_answer((uint8_t)code);
        TEST_ASSERT_EQUAL(status_of((uint8_t)code), run_verify(TFW_ATECC_T_VERIFY));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(256u, runs);
}

static void test_verify_external_every_bit_flip_is_refused(void)
{
    unsigned bit;

    for (bit = 0u; bit < 32u; bit++) {
        setUp();
        status_answer(0x00u);
        answer[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        /* A flip in the count is a malformed answer; elsewhere, a bad CRC. */
        TEST_ASSERT_EQUAL(bit < 8u ? TFW_ERR_CRYPTO : TFW_ERR_CRC,
                          run_verify(TFW_ATECC_T_VERIFY));
    }
}

static void test_verify_external_every_count_byte_under_a_resealed_crc(void)
{
    unsigned count;

    for (count = 0u; count <= 0xFFu; count++) {
        setUp();
        answer[0] = (uint8_t)count;
        answer[1] = 0x00u;
        seal(answer, 4u);
        TEST_ASSERT_EQUAL(count == 4u ? TFW_OK : TFW_ERR_CRYPTO,
                          run_verify(TFW_ATECC_T_VERIFY));
    }
}

static void test_verify_external_refuses_a_refused_write_and_never_polls(void)
{
    cmd_status = TFW_ERR_NACK;
    status_answer(0x00u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, run_verify(TFW_ATECC_T_VERIFY));
    TEST_ASSERT_EQUAL_UINT(0u, reads);
}

static void test_verify_external_poll_bound_at_its_edge(void)
{
    unsigned tries = TFW_ATECC_T_VERIFY * 2u + 4u;

    status_answer(0x00u);
    nacks = tries - 1u;
    TEST_ASSERT_EQUAL(TFW_OK, run_verify(TFW_ATECC_T_VERIFY));
    TEST_ASSERT_EQUAL_UINT(tries, reads);
    TEST_ASSERT_EQUAL_UINT32(tries * 500u, delayed_us);

    setUp();
    status_answer(0x00u);
    nacks = tries;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run_verify(TFW_ATECC_T_VERIFY));
    TEST_ASSERT_EQUAL_UINT(tries, reads);
}

/* ---- wake and sleep ------------------------------------------------------ */

static const uint8_t WAKE_TOKEN[4] = { 0x04, 0x11, 0x33, 0x43 };

static void test_wake_pulses_the_bus_at_the_elements_clock(void)
{
    memcpy(answer, WAKE_TOKEN, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL_UINT(1u, inits);
    TEST_ASSERT_EQUAL_UINT32(TFW_ATECC_I2C_HZ, init_hz);
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_HEX8(0x00u, wr_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, wr_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, wr_buf[0][0]);
    TEST_ASSERT_EQUAL_UINT32(1500u, delayed_us);
    TEST_ASSERT_EQUAL_UINT(1u, reads);
    TEST_ASSERT_EQUAL_UINT8(4u, read_len);
}

static void test_wake_every_bit_flip_of_the_token_is_refused(void)
{
    unsigned bit;

    for (bit = 0u; bit < 32u; bit++) {
        setUp();
        memcpy(answer, WAKE_TOKEN, 4u);
        answer[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        TEST_ASSERT_EQUAL(bit < 16u ? TFW_ERR_CRYPTO : TFW_ERR_CRC, tfw_atecc_wake());
    }
}

static void test_wake_every_status_under_a_resealed_crc(void)
{
    unsigned code;

    for (code = 0u; code <= 0xFFu; code++) {
        setUp();
        status_answer((uint8_t)code);
        TEST_ASSERT_EQUAL(code == 0x11u ? TFW_OK : TFW_ERR_CRYPTO, tfw_atecc_wake());
    }
}

static void test_wake_with_no_answer_is_not_an_element(void)
{
    memcpy(answer, WAKE_TOKEN, 4u);
    nacks = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_atecc_wake());
    TEST_ASSERT_EQUAL_UINT(1u, reads);
}

static void test_sleep_writes_the_sleep_word_and_returns_the_bus(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_sleep());
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_ADDR, wr_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, wr_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, wr_buf[0][0]);

    setUp();
    cmd_status = TFW_ERR_NACK;
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_atecc_sleep());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_info_command_is_the_datasheets_bytes);
    RUN_TEST(test_every_payload_length_is_framed);
    RUN_TEST(test_every_oversized_payload_is_refused_before_the_bus);
    RUN_TEST(test_a_refused_command_write_is_returned_and_never_polled);
    RUN_TEST(test_a_response_longer_than_the_buffer_is_refused_and_nothing_is_sent);
    RUN_TEST(test_a_failed_command_write_leaves_the_shared_buffer_cleared);
    RUN_TEST(test_every_status_byte_with_no_data_expected);
    RUN_TEST(test_every_status_byte_where_data_was_expected_is_a_refusal);
    RUN_TEST(test_every_count_byte_under_a_resealed_crc);
    RUN_TEST(test_every_bit_flip_is_refused);
    RUN_TEST(test_every_data_bit_flip_under_a_resealed_crc_is_the_data);
    RUN_TEST(test_random_answers);
    RUN_TEST(test_the_poll_bound_at_its_edge);
    RUN_TEST(test_verify_external_frames_its_command);
    RUN_TEST(test_verify_external_every_status_byte);
    RUN_TEST(test_verify_external_every_bit_flip_is_refused);
    RUN_TEST(test_verify_external_every_count_byte_under_a_resealed_crc);
    RUN_TEST(test_verify_external_refuses_a_refused_write_and_never_polls);
    RUN_TEST(test_verify_external_poll_bound_at_its_edge);
    RUN_TEST(test_wake_pulses_the_bus_at_the_elements_clock);
    RUN_TEST(test_wake_every_bit_flip_of_the_token_is_refused);
    RUN_TEST(test_wake_every_status_under_a_resealed_crc);
    RUN_TEST(test_wake_with_no_answer_is_not_an_element);
    RUN_TEST(test_sleep_writes_the_sleep_word_and_returns_the_bus);
    return UNITY_END();
}
