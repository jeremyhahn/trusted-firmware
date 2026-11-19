/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * #584: on the STM32H5 ETH_MAC image a TCP peer may ask for ATTEST, whose
 * software P-256 sign runs for tens of seconds at 64 MHz. An output armed over
 * serial before it must still be released on time: the signer calls
 * tfw_sign_yield (hal_eth_sign_yield.c) after every ladder step, and that runs
 * the scheduler's deadline sweep and nothing else.
 *
 * The clock is faked: every tfw_hal_millis() read during the sign advances it
 * by STEP_MS, about one ladder step at 64 MHz, so the deadline falls inside
 * the sign. The real scheduler, signer and micro-ecc are linked.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/board.h"
#include "../../src/caps/sched.h"
#include "../../src/core/reader.h"
#include "../../src/core/writer.h"
#include "../../src/crypto/deviceid_p256.h"

#define PIN      3u
#define ARM_MS   1000u
#define STEP_MS  100u

static tfw_ms_t now_ms;
static int      signing;
static int      released;          /* the scheduled write happened */
static int      released_in_sign;  /* ... while the sign was in progress */
static tfw_ms_t released_ms;
static uint8_t  released_level;

tfw_ms_t tfw_hal_millis(void)
{
    tfw_ms_t t = now_ms;

    if (signing) {
        now_ms += STEP_MS;
    }
    return t;
}

/* The netif's deadline service (hal_eth_netif.c), faked: counted per sweep.
 * test_h5_netif.c runs it on the real lwIP timers across a lease expiry. */
static unsigned deadline_sweeps;

void tfw_h5_eth_service_deadlines(void)
{
    deadline_sweeps++;
}

tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out)
{
    (void)out;
    return pin == PIN ? TFW_OK : TFW_ERR_RANGE;
}

uint8_t tfw_board_pin_flags(tfw_pin_t pin)
{
    (void)pin;
    return 0u;
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    (void)p;
    (void)mode;
    return TFW_OK;
}

static int arming;

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    (void)p;
    if (!arming) {
        released = 1;
        released_in_sign = signing;
        released_ms = now_ms;
        released_level = level;
    }
    return TFW_OK;
}

static const uint8_t seed[32] = {
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
};
static const uint8_t digest[32] = { 0xA5 };

static void arm_high_for(uint32_t ms)
{
    uint8_t      in[6] = { PIN, 1u,
                           (uint8_t)ms, (uint8_t)(ms >> 8),
                           (uint8_t)(ms >> 16), (uint8_t)(ms >> 24) };
    uint8_t      out[4];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, (uint8_t)sizeof in);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    arming = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sched_cmd_set(&r, &w));
    arming = 0;
}

void setUp(void)
{
    tfw_sched_init();
    now_ms = 0u;
    signing = 0;
    released = 0;
    released_in_sign = 0;
    released_ms = 0u;
    released_level = 1u;
}

void tearDown(void) { }

/* Armed over serial, then a TCP ATTEST's sign starts: the pin drops at the
 * first ladder step past its deadline, inside the sign, not after it. */
static void test_a_scheduled_output_is_released_on_time_during_a_sign(void)
{
    uint8_t sig[64];
    uint8_t quiet[64];

    arm_high_for(ARM_MS);
    TEST_ASSERT_TRUE(tfw_sched_active());

    signing = 1;
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(seed, digest, sig));
    signing = 0;

    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_TRUE(released_in_sign);
    TEST_ASSERT_EQUAL_UINT8(0u, released_level);
    TEST_ASSERT_TRUE(released_ms >= ARM_MS);
    TEST_ASSERT_TRUE(released_ms - ARM_MS <= STEP_MS);
    TEST_ASSERT_TRUE(now_ms > released_ms + STEP_MS); /* the sign went on */
    TEST_ASSERT_FALSE(tfw_sched_active());

    /* The yields change nothing in the signature (RFC 6979: deterministic). */
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(seed, digest, quiet));
    TEST_ASSERT_EQUAL_MEMORY(quiet, sig, sizeof sig);
}

/* Codex round 2 P1: a sign first derives d from the seed, and that runs a full
 * multiplication (the public key) before the signing one. With nothing cached,
 * a deadline late in that derivation is released on time, in the first half of
 * the sign's ladder steps, not only once signing starts. */
#define DERIVE_ARM_MS (200u * STEP_MS)

static void test_a_scheduled_output_is_released_on_time_during_derivation(void)
{
    uint8_t sig[64];

    arm_high_for(DERIVE_ARM_MS);
    TEST_ASSERT_TRUE(tfw_sched_active());

    signing = 1;
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(seed, digest, sig));
    signing = 0;

    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_TRUE(released_in_sign);
    TEST_ASSERT_EQUAL_UINT8(0u, released_level);
    TEST_ASSERT_TRUE(released_ms >= DERIVE_ARM_MS);
    TEST_ASSERT_TRUE(released_ms - DERIVE_ARM_MS <= STEP_MS);
    /* Both multiplications yield, and the release fell in the first. */
    TEST_ASSERT_TRUE(now_ms >= 500u * STEP_MS);
    TEST_ASSERT_TRUE(2u * released_ms < now_ms);
    TEST_ASSERT_FALSE(tfw_sched_active());
}

/* Nothing armed: the sweep runs on every step and writes no pin. */
static void test_a_sign_with_nothing_armed_writes_no_pin(void)
{
    uint8_t sig[64];

    deadline_sweeps = 0u;
    signing = 1;
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(seed, digest, sig));
    signing = 0;
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_TRUE(now_ms >= 250u * STEP_MS); /* one sweep per ladder step */
    TEST_ASSERT_TRUE(deadline_sweeps >= 250u);  /* the network's deadlines too */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_scheduled_output_is_released_on_time_during_a_sign);
    RUN_TEST(test_a_scheduled_output_is_released_on_time_during_derivation);
    RUN_TEST(test_a_sign_with_nothing_armed_writes_no_pin);
    return UNITY_END();
}
