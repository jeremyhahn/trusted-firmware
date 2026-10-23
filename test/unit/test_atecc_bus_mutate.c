/*
 * Mutation tests for src/crypto/atecc608a/atecc.c (#163), beside
 * test_atecc_mutate, whose bus fake is its own. test_atecc drives
 * the driver through the software element, which answers like the part but
 * cannot misbehave on cue and does not keep time. This link has no model: the
 * I2C HAL is a scripted bus defined below, so a test chooses exactly what each
 * read returns and counts every init, write, read and delay the driver makes.
 *
 * No device, no key: the frames carry no secret.
 */
#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "atecc.h"

#define MAX_FRAME (TFW_ATECC_MAX_DATA + 8u)

static unsigned     inits, writes, reads, delays;
static uint32_t     init_hz;
static uint8_t      wrote[MAX_FRAME];
static uint8_t      wrote_len;
static tfw_status_t write_st;           /* what a command write answers */
static unsigned     busy;               /* reads to NACK before answering */
static uint8_t      reply[MAX_FRAME];
static uint8_t      reply_len;
static uint8_t      nack_with_token;    /* NACK a read but leave the wake token in it */

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    inits++;
    init_hz = hz;
    return TFW_OK;
}

void tfw_hal_delay_us(uint16_t us)
{
    (void)us;
    delays++;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    if (addr7 == 0x00u) {
        return TFW_ERR_NACK;            /* the wake pulse */
    }
    if (wlen) {
        writes++;
        wrote_len = wlen;
        memcpy(wrote, wr, wlen);
        return wr[0] == TFW_ATECC_WA_COMMAND ? write_st : TFW_OK;
    }
    reads++;
    if (nack_with_token) {
        static const uint8_t token[4] = { 0x04u, 0x11u, 0x33u, 0x43u };
        memcpy(rd, token, rlen < 4u ? rlen : 4u);
        return TFW_ERR_NACK;
    }
    if (busy) {
        busy--;
        return TFW_ERR_NACK;
    }
    memset(rd, 0, rlen);
    memcpy(rd, reply, rlen < reply_len ? rlen : reply_len);
    return TFW_OK;
}

/* A well-formed response: count, body, CRC. */
static void answer(const uint8_t *body, uint8_t len)
{
    uint16_t crc;

    reply[0] = (uint8_t)(len + 3u);
    memcpy(&reply[1], body, len);
    crc = tfw_atecc_crc16(reply, (uint8_t)(len + 1u));
    reply[len + 1u] = (uint8_t)(crc & 0xFFu);
    reply[len + 2u] = (uint8_t)(crc >> 8);
    reply_len = (uint8_t)(len + 3u);
}

static void answer_status(uint8_t status)
{
    answer(&status, 1u);
}

void setUp(void)
{
    inits = writes = reads = delays = 0u;
    init_hz = 0u;
    wrote_len = 0u;
    write_st = TFW_OK;
    busy = 0u;
    reply_len = 0u;
    nack_with_token = 0u;
}

void tearDown(void) {}

/* Every wake reasserts the slow bus; the part never wakes at 400 kHz. */
void test_every_wake_reasserts_the_slow_bus(void)
{
    answer_status(0x11u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL_UINT(2u, inits);
    TEST_ASSERT_EQUAL_UINT32(TFW_ATECC_I2C_HZ, init_hz);
    TEST_ASSERT_TRUE(init_hz <= 100000uL);
}

/* A wake read that NACKs is no element, whatever landed in the buffer. */
void test_a_nacked_wake_read_is_not_a_wake(void)
{
    nack_with_token = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_atecc_wake());
}

/* Status-only replies to a no-data command: each fault code is a crypto
 * failure, including the wake status and a code the datasheet never lists. */
void test_fault_statuses_are_crypto_failures(void)
{
    static const uint8_t codes[] = { 0x01u, 0x05u, 0x07u, 0x08u, 0x11u, 0x42u };
    uint8_t i;

    for (i = 0u; i < sizeof codes; i++) {
        answer_status(codes[i]);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_CRYPTO,
                                  tfw_atecc_exec(0x12u, 0u, 0u, 0, 0u, 0, 0u, 1u),
                                  "status code");
    }
    answer_status(0x00u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(0x12u, 0u, 0u, 0, 0u, 0, 0u, 1u));
}

/* The largest payload is sent whole; one byte more is refused unsent. */
void test_data_bound_is_inclusive(void)
{
    uint8_t data[TFW_ATECC_MAX_DATA + 1u];

    memset(data, 0xA5, sizeof data);
    answer_status(0x00u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(0x12u, 0u, 0u, data, TFW_ATECC_MAX_DATA,
                                             0, 0u, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_MAX_DATA + 8u, wrote_len);

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_atecc_exec(0x12u, 0u, 0u, data,
                                                    TFW_ATECC_MAX_DATA + 1u, 0, 0u, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
}

/* Param2 goes out little-endian, both bytes. */
void test_param2_is_framed_little_endian(void)
{
    answer_status(0x00u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(0x12u, 0x5Au, 0x1234u, 0, 0u, 0, 0u, 1u));
    TEST_ASSERT_EQUAL_UINT8(0x5Au, wrote[3]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, wrote[4]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, wrote[5]);
}

/* A refused command write is reported as the bus reported it, and nothing is
 * read after it. */
void test_a_refused_write_returns_the_bus_status(void)
{
    answer_status(0x11u);               /* a stale reply waiting to be misread */
    write_st = TFW_ERR_BUSY;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_atecc_exec(0x12u, 0u, 0u, 0, 0u, 0, 0u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, reads);
}

/* The poll stops on the first answer: three NACKs, one good read, four waits. */
void test_the_poll_stops_at_the_first_answer(void)
{
    answer_status(0x00u);
    busy = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(0x12u, 0u, 0u, 0, 0u, 0, 0u, 50u));
    TEST_ASSERT_EQUAL_UINT(4u, reads);
    TEST_ASSERT_EQUAL_UINT(4u, delays);
}

/* A success status where data was asked for is still a failure. */
void test_status_zero_in_place_of_data_fails(void)
{
    uint8_t resp[4];

    answer_status(0x00u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_atecc_exec(0x02u, 0u, 0u, 0, 0u,
                                                     resp, sizeof resp, 1u));
}

/* A data reply of the wrong length is the wrong reply, not a bad CRC. */
void test_a_reply_of_the_wrong_length_is_refused(void)
{
    uint8_t body[32], resp[8];

    memset(body, 0x3C, sizeof body);
    answer(body, sizeof body);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_atecc_exec(0x02u, 0u, 0u, 0, 0u,
                                                     resp, sizeof resp, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_wake_reasserts_the_slow_bus);
    RUN_TEST(test_a_nacked_wake_read_is_not_a_wake);
    RUN_TEST(test_fault_statuses_are_crypto_failures);
    RUN_TEST(test_data_bound_is_inclusive);
    RUN_TEST(test_param2_is_framed_little_endian);
    RUN_TEST(test_a_refused_write_returns_the_bus_status);
    RUN_TEST(test_the_poll_stops_at_the_first_answer);
    RUN_TEST(test_status_zero_in_place_of_data_fails);
    RUN_TEST(test_a_reply_of_the_wrong_length_is_refused);
    return UNITY_END();
}
