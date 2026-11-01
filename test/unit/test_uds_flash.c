/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/crypto/soft/uds_flash.c, the image's UDS source, linked in place of
 * test/mock/uds_mock.c, under repeated reads and the soft provider it feeds
 * under mutated requests (#163). The store has no write path in the image:
 * until tfw-boot hands a UDS over it reads back as unprovisioned, every time,
 * and the provider refuses rather than sign, MAC or hand out a key.
 *
 * "Refuses" is observed with sentinels: no output buffer a refused call is
 * given changes, so nothing derived from a key that is not there reaches a
 * reply.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/crypto.h"

const uint8_t *tfw_crypto_soft_uds(void);

void setUp(void)    { TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init()); }
void tearDown(void) {}

static void untouched(const uint8_t *buf, size_t n, const char *msg)
{
    for (size_t i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA5u, buf[i], msg);
    }
}

/* One SIGN of digest: refused, sig and alg untouched. */
static void sign_refused(const uint8_t digest[32], const char *msg)
{
    uint8_t sig[64], alg = 0xA5u;

    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg), msg);
    untouched(sig, sizeof sig, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA5u, alg, msg);
}

/* The image has no UDS of its own: the store reads back unprovisioned on
 * every read, across re-inits. */
static void test_the_store_reads_unprovisioned_every_time(void)
{
    char msg[32];

    for (unsigned i = 0u; i < 1000u; i++) {
        snprintf(msg, sizeof msg, "read %u", i);
        TEST_ASSERT_NULL_MESSAGE(tfw_crypto_soft_uds(), msg);
        if ((i & 0x3Fu) == 0u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_crypto_init(), msg);
        }
    }
}

/* Every byte of the digest through every value, one at a time: no digest
 * gets a signature out of an unprovisioned store. */
static void test_every_digest_byte_is_refused(void)
{
    uint8_t digest[32];
    char    msg[48];

    for (unsigned pos = 0u; pos < 32u; pos++) {
        for (unsigned v = 0u; v < 256u; v++) {
            memset(digest, 0x5A, sizeof digest);
            digest[pos] = (uint8_t)v;
            snprintf(msg, sizeof msg, "pos %u value %u", pos, v);
            sign_refused(digest, msg);
        }
    }
}

/* Every slot byte and every length to HMAC: slot 0 is the one key and is not
 * provisioned, every other slot is out of range, and out stays as it was. */
static void test_every_hmac_slot_and_length_is_refused(void)
{
    uint8_t data[256], out[32];
    char    msg[48];

    memset(data, 0x3C, sizeof data);
    for (unsigned slot = 0u; slot < 256u; slot++) {
        for (unsigned len = 0u; len <= sizeof data; len += 17u) {
            memset(out, 0xA5, sizeof out);
            snprintf(msg, sizeof msg, "slot %u len %u", slot, len);
            TEST_ASSERT_EQUAL_MESSAGE(slot ? TFW_ERR_RANGE : TFW_ERR_NOT_PROVISIONED,
                                      tfw_crypto_hmac((uint8_t)slot, data, (uint16_t)len, out),
                                      msg);
            untouched(out, sizeof out, msg);
        }
    }
}

/* No public half, no random and no counter on this tier, provisioned or not:
 * the buffers stay as given. */
static void test_no_key_material_leaves_by_another_door(void)
{
    uint8_t  pub[64], rnd_out[32];
    uint32_t ctr = 0xA5A5A5A5u;

    memset(pub, 0xA5, sizeof pub);
    memset(rnd_out, 0xA5, sizeof rnd_out);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(pub));
    untouched(pub, sizeof pub, "pubkey");
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(rnd_out, (uint8_t)sizeof rnd_out));
    untouched(rnd_out, sizeof rnd_out, "random");
    for (unsigned idx = 0u; idx < 256u; idx++) {
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter((uint8_t)idx, &ctr));
        TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, ctr);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_store_reads_unprovisioned_every_time);
    RUN_TEST(test_every_digest_byte_is_refused);
    RUN_TEST(test_every_hmac_slot_and_length_is_refused);
    RUN_TEST(test_no_key_material_leaves_by_another_door);
    return UNITY_END();
}
