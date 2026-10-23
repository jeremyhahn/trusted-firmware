/*
 * Mutation tests for boot/measure/measure_soft.c (#163): the soft
 * measurement against the FIPS 180-4 SHA-256 vectors, through a reader that
 * records every read -- so a bound off by one, a dropped length check, a read
 * at the wrong address or of the wrong size, a reader error ignored or the
 * digest left unfinished each change something this file names.
 */
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_hal_mock.h"

#define CHUNK 32u
#define MAX_CALLS 16u

/* FIPS 180-4 example vectors: "abc" and the 448-bit two-block message. */
static const uint8_t ABC_DIGEST[32] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE,
    0x5D, 0xAE, 0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
    0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
};
static const char MSG56[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const uint8_t MSG56_DIGEST[32] = {
    0x24, 0x8D, 0x6A, 0x61, 0xD2, 0x06, 0x38, 0xB8, 0xE5, 0xC0, 0x26, 0x93,
    0x0C, 0x3E, 0x60, 0x39, 0xA3, 0x3C, 0xE4, 0x59, 0x64, 0xFF, 0x21, 0x67,
    0xF6, 0xEC, 0xED, 0xD4, 0x19, 0xDB, 0x06, 0xC1,
};

/* The reader: serves src at base, else a byte of the address; records the
 * first MAX_CALLS reads; fails the fail_at'th read (1-based) with fail_st. */
static const uint8_t *src;
static uint32_t       src_base;
static uint32_t       calls;
static uint32_t       call_addr[MAX_CALLS];
static uint8_t        call_len[MAX_CALLS];
static uint32_t       last_addr;
static uint8_t        last_len;
static uint32_t       total;
static uint32_t       fail_at;
static tfw_status_t   fail_st;

static tfw_status_t reader(uint32_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    if (calls < MAX_CALLS) {
        call_addr[calls] = addr;
        call_len[calls]  = len;
    }
    calls++;
    last_addr = addr;
    last_len  = len;
    total += len;
    if (calls == fail_at) {
        return fail_st;
    }
    for (i = 0u; i < len; i++) {
        buf[i] = (src != NULL) ? src[addr - src_base + i] : (uint8_t)(addr + i);
    }
    return TFW_OK;
}

static void serve(const void *data, uint32_t base)
{
    src      = (const uint8_t *)data;
    src_base = base;
}

void setUp(void)
{
    tfw_boot_mock_reset();
    src = NULL;
    src_base = 0u;
    calls = 0u;
    total = 0u;
    fail_at = 0u;
    fail_st = TFW_OK;
    memset(call_addr, 0, sizeof call_addr);
    memset(call_len, 0, sizeof call_len);
}

void tearDown(void) {}

static void test_abc_measures_to_the_fips_vector_in_one_read_at_base(void)
{
    uint8_t out[32];
    serve("abc", 0x1000u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0x1000u, 3u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, 32);
    TEST_ASSERT_EQUAL_UINT32(1u, calls);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, call_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(3u, call_len[0]);
}

static void test_a_two_block_message_reads_a_whole_chunk_then_the_rest(void)
{
    uint8_t out[32];
    serve(MSG56, 0x200u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0x200u, 56u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG56_DIGEST, out, 32);
    TEST_ASSERT_EQUAL_UINT32(2u, calls);
    TEST_ASSERT_EQUAL_HEX32(0x200u, call_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(CHUNK, call_len[0]);
    TEST_ASSERT_EQUAL_HEX32(0x220u, call_addr[1]);
    TEST_ASSERT_EQUAL_UINT8(56u - CHUNK, call_len[1]);
}

static void test_one_chunk_exactly_is_one_read(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0u, CHUNK, out));
    TEST_ASSERT_EQUAL_UINT32(1u, calls);
    TEST_ASSERT_EQUAL_UINT8(CHUNK, call_len[0]);
}

static void test_one_byte_under_a_chunk_is_one_short_read(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0u, CHUNK - 1u, out));
    TEST_ASSERT_EQUAL_UINT32(1u, calls);
    TEST_ASSERT_EQUAL_UINT8(CHUNK - 1u, call_len[0]);
}

static void test_one_byte_over_a_chunk_is_a_chunk_then_one_byte(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0x40u, CHUNK + 1u, out));
    TEST_ASSERT_EQUAL_UINT32(2u, calls);
    TEST_ASSERT_EQUAL_UINT8(CHUNK, call_len[0]);
    TEST_ASSERT_EQUAL_HEX32(0x40u + CHUNK, call_addr[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, call_len[1]);
}

static void test_a_zero_length_is_refused_before_any_read(void)
{
    uint8_t out[32];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(reader, 0u, 0u, out));
    TEST_ASSERT_EQUAL_UINT32(0u, calls);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, out, 32);
}

static void test_one_past_the_largest_image_is_refused_before_any_read(void)
{
    uint8_t out[32];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_boot_measure_from(reader, 0u, TFW_BOOT_MAX_IMAGE_LEN + 1u, out));
    TEST_ASSERT_EQUAL_UINT32(0u, calls);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, out, 32);
}

static void test_the_largest_image_is_read_to_its_last_byte_and_no_further(void)
{
    uint8_t  out[32];
    uint32_t tail = TFW_BOOT_MAX_IMAGE_LEN % CHUNK;
    if (tail == 0u) {
        tail = CHUNK;
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, 0x100u, TFW_BOOT_MAX_IMAGE_LEN, out));
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_MAX_IMAGE_LEN, total);
    TEST_ASSERT_EQUAL_UINT32((TFW_BOOT_MAX_IMAGE_LEN + CHUNK - 1u) / CHUNK, calls);
    TEST_ASSERT_EQUAL_HEX32(0x100u + TFW_BOOT_MAX_IMAGE_LEN - tail, last_addr);
    TEST_ASSERT_EQUAL_UINT8(tail, last_len);
}

static void test_a_reader_error_is_returned_and_stops_the_measurement(void)
{
    uint8_t out[32];
    memset(out, 0xA5, sizeof out);
    fail_at = 2u;
    fail_st = TFW_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_boot_measure_from(reader, 0u, 3u * CHUNK, out));
    TEST_ASSERT_EQUAL_UINT32(2u, calls);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, out, 32);
}

static void test_a_reader_error_on_the_last_read_is_returned(void)
{
    uint8_t out[32];
    fail_at = 2u;
    fail_st = TFW_ERR_INTERNAL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_boot_measure_from(reader, 0u, CHUNK + 5u, out));
}

static void test_the_application_is_measured_from_its_base_in_flash(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  out[32];
    TEST_ASSERT_TRUE(tfw_boot_mock_flash_size() >= TFW_BOOT_APP_BASE + sizeof MSG56);
    memcpy(&flash[TFW_BOOT_APP_BASE], MSG56, 56u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(56u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG56_DIGEST, out, 32);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(3u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, 32);
}

static void test_the_hash_is_sha256(void)
{
    uint8_t out[32];
    tfw_boot_hash((const uint8_t *)"abc", 3u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, 32);
    tfw_boot_hash((const uint8_t *)MSG56, 56u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG56_DIGEST, out, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_abc_measures_to_the_fips_vector_in_one_read_at_base);
    RUN_TEST(test_a_two_block_message_reads_a_whole_chunk_then_the_rest);
    RUN_TEST(test_one_chunk_exactly_is_one_read);
    RUN_TEST(test_one_byte_under_a_chunk_is_one_short_read);
    RUN_TEST(test_one_byte_over_a_chunk_is_a_chunk_then_one_byte);
    RUN_TEST(test_a_zero_length_is_refused_before_any_read);
    RUN_TEST(test_one_past_the_largest_image_is_refused_before_any_read);
    RUN_TEST(test_the_largest_image_is_read_to_its_last_byte_and_no_further);
    RUN_TEST(test_a_reader_error_is_returned_and_stops_the_measurement);
    RUN_TEST(test_a_reader_error_on_the_last_read_is_returned);
    RUN_TEST(test_the_application_is_measured_from_its_base_in_flash);
    RUN_TEST(test_the_hash_is_sha256);
    return UNITY_END();
}
