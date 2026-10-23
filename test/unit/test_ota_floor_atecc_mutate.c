/*
 * The element tier's anti-rollback floor (src/core/ota_floor_atecc.c, #163)
 * alone, over this suite's own element driver: tfw_atecc_wake, tfw_atecc_exec
 * and tfw_atecc_sleep record what they are asked and answer as told.
 *
 * The oracle is the floor's contract: one COUNTER read of the boot counter,
 * four response bytes, little-endian; the element is put to sleep after every
 * command it was woken for; a wake or a command that fails is a crypto error
 * and leaves the caller's floor as it was.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "ota_floor.h"
#include "../crypto/atecc608a/atecc.h"

static tfw_status_t wake_st, exec_st;
static unsigned     wakes, execs, sleeps;
static uint8_t      op, p1, dlen, rlen;
static uint16_t     p2, exec_ms;
static const uint8_t *data;
static uint8_t      answer[4];

tfw_status_t tfw_atecc_wake(void)
{
    wakes++;
    return wake_st;
}

tfw_status_t tfw_atecc_sleep(void)
{
    sleeps++;
    return TFW_ERR_CRYPTO; /* a failed sleep is not the floor's concern */
}

tfw_status_t tfw_atecc_exec(uint8_t opcode, uint8_t a1, uint16_t a2,
                            const uint8_t *d, uint8_t dl,
                            uint8_t *resp, uint8_t rl, uint16_t ms)
{
    execs++;
    op = opcode; p1 = a1; p2 = a2; data = d; dlen = dl; rlen = rl; exec_ms = ms;
    TEST_ASSERT_EQUAL_UINT(wakes, sleeps + 1u); /* awake while it runs */
    if (exec_st == TFW_OK) {
        memcpy(resp, answer, 4u);
    } else {
        memset(resp, 0x5a, rl);
    }
    return exec_st;
}

void setUp(void)
{
    wake_st = TFW_OK;
    exec_st = TFW_OK;
    wakes = execs = sleeps = 0u;
    op = p1 = dlen = rlen = 0xeeu;
    p2 = exec_ms = 0xeeeeu;
    data = (const uint8_t *)answer;
    answer[0] = 0x01u; answer[1] = 0x23u; answer[2] = 0x45u; answer[3] = 0x87u;
}

void tearDown(void) {}

static void test_the_floor_is_one_boot_counter_read(void)
{
    uint32_t floor = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(0, &floor));
    TEST_ASSERT_EQUAL_UINT(1u, wakes);
    TEST_ASSERT_EQUAL_UINT(1u, execs);
    TEST_ASSERT_EQUAL_UINT(1u, sleeps);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_COUNTER, op);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_COUNTER_READ, p1);
    TEST_ASSERT_EQUAL_HEX16(TFW_ATECC_COUNTER_BOOT, p2);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_UINT8(0u, dlen);
    TEST_ASSERT_EQUAL_UINT8(4u, rlen);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_T_COUNTER, exec_ms);
}

static void test_the_floor_is_the_counter_little_endian(void)
{
    uint32_t floor = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(0, &floor));
    TEST_ASSERT_EQUAL_HEX32(0x87452301u, floor);
}

static void test_every_single_byte_lands_in_its_place(void)
{
    for (unsigned i = 0; i < 4u; i++) {
        for (unsigned b = 0; b < 8u; b++) {
            uint32_t floor = 0xffffffffu;
            memset(answer, 0, sizeof answer);
            answer[i] = (uint8_t)(1u << b);
            TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(0, &floor));
            TEST_ASSERT_EQUAL_HEX32((uint32_t)1u << (8u * i + b), floor);
        }
    }
}

static void test_a_wake_that_fails_is_a_crypto_error_and_runs_nothing(void)
{
    static const tfw_status_t fails[] = { TFW_ERR_CRYPTO, TFW_ERR_INTERNAL, TFW_ERR_BAD_STATE };
    for (unsigned i = 0; i < sizeof fails / sizeof fails[0]; i++) {
        uint32_t floor = 0xa5a5a5a5u;
        setUp();
        wake_st = fails[i];
        TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor(0, &floor));
        TEST_ASSERT_EQUAL_UINT(0u, execs);
        TEST_ASSERT_EQUAL_HEX32(0xa5a5a5a5u, floor);
    }
}

static void test_a_refused_read_is_a_crypto_error_asleep_and_untouched(void)
{
    static const tfw_status_t fails[] = { TFW_ERR_CRYPTO, TFW_ERR_INTERNAL, TFW_ERR_BAD_STATE };
    for (unsigned i = 0; i < sizeof fails / sizeof fails[0]; i++) {
        uint32_t floor = 0xa5a5a5a5u;
        setUp();
        exec_st = fails[i];
        TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor(0, &floor));
        TEST_ASSERT_EQUAL_UINT(1u, sleeps);
        TEST_ASSERT_EQUAL_HEX32(0xa5a5a5a5u, floor);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_floor_is_one_boot_counter_read);
    RUN_TEST(test_the_floor_is_the_counter_little_endian);
    RUN_TEST(test_every_single_byte_lands_in_its_place);
    RUN_TEST(test_a_wake_that_fails_is_a_crypto_error_and_runs_nothing);
    RUN_TEST(test_a_refused_read_is_a_crypto_error_asleep_and_untouched);
    return UNITY_END();
}
