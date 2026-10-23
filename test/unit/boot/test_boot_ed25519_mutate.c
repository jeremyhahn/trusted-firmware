/*
 * The ed25519-donna backend (boot/verify/verify_ed25519_donna.c) and its shim
 * (boot/verify/donna_shim.c) under seeded faults (#163), linked exactly as
 * test_boot_ed25519 is. The shipped rows are ES256 (owner, item 79); this
 * binding is still built, measured and tested as it stands.
 *
 * Public RFC 8032 vectors only: TEST 1024 (§7.1), whose 1023-byte message is
 * the one vector longer than 255 bytes, so a length narrowed anywhere on the
 * way to donna is seen. No private key: only the public key, the message and
 * the signature are here.
 *
 * Deterministic: xorshift32 from a literal seed, a literal iteration count.
 *
 * Mutants this file kills (each tried by hand against the two units):
 *   verify: the length narrowed to 8 bits, shortened by one, or dropped; the
 *     verdict inverted, or any non-zero donna answer read as good; the
 *     refusal reported as another status; pub and sig swapped
 *   pubkey: the erased test inverted; the clear skipped, one byte short or
 *     one byte wide; the loop one past the slot; the unprovisioned answer OK
 *   cose_alg: any value but 0x27 (EdDSA, RFC 9053 §2.2)
 *   shim: the clear skipped, one byte short, one byte wide, or not zero
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "boot_verify.h"
#include "donna_shim.h"

/* RFC 8032 §7.1, -----TEST 1024: PUBLIC KEY, SIGNATURE, MESSAGE. */
#define RFC8032_T1024_PUB \
    "278117fc144c72340f67d0f2316e8386" \
    "ceffbf2b2428c9c51fef7c597f1d426e"

#define RFC8032_T1024_SIG \
    "0aab4c900501b3e24d7cdf4663326a3a" \
    "87df5e4843b2cbdb67cbf6e460fec350" \
    "aa5371b1508f9f4528ecea23c436d94b" \
    "5e8fcd4f681e30a6ac00a9704a188a03"

#define RFC8032_T1024_MSG \
    "08b8b2b733424243760fe426a4b54908" \
    "632110a66c2f6591eabd3345e3e4eb98" \
    "fa6e264bf09efe12ee50f8f54e9f77b1" \
    "e355f6c50544e23fb1433ddf73be84d8" \
    "79de7c0046dc4996d9e773f4bc9efe57" \
    "38829adb26c81b37c93a1b270b20329d" \
    "658675fc6ea534e0810a4432826bf58c" \
    "941efb65d57a338bbd2e26640f89ffbc" \
    "1a858efcb8550ee3a5e1998bd177e93a" \
    "7363c344fe6b199ee5d02e82d522c4fe" \
    "ba15452f80288a821a579116ec6dad2b" \
    "3b310da903401aa62100ab5d1a36553e" \
    "06203b33890cc9b832f79ef80560ccb9" \
    "a39ce767967ed628c6ad573cb116dbef" \
    "efd75499da96bd68a8a97b928a8bbc10" \
    "3b6621fcde2beca1231d206be6cd9ec7" \
    "aff6f6c94fcd7204ed3455c68c83f4a4" \
    "1da4af2b74ef5c53f1d8ac70bdcb7ed1" \
    "85ce81bd84359d44254d95629e9855a9" \
    "4a7c1958d1f8ada5d0532ed8a5aa3fb2" \
    "d17ba70eb6248e594e1a2297acbbb39d" \
    "502f1a8c6eb6f1ce22b3de1a1f40cc24" \
    "554119a831a9aad6079cad88425de6bd" \
    "e1a9187ebb6092cf67bf2b13fd65f270" \
    "88d78b7e883c8759d2c4f5c65adb7553" \
    "878ad575f9fad878e80a0c9ba63bcbcc" \
    "2732e69485bbc9c90bfbd62481d9089b" \
    "eccf80cfe2df16a2cf65bd92dd597b07" \
    "07e0917af48bbb75fed413d238f5555a" \
    "7a569d80c3414a8d0859dc65a46128ba" \
    "b27af87a71314f318c782b23ebfe808b" \
    "82b0ce26401d2e22f04d83d1255dc51a" \
    "ddd3b75a2b1ae0784504df543af8969b" \
    "e3ea7082ff7fc9888c144da2af58429e" \
    "c96031dbcad3dad9af0dcbaaaf268cb8" \
    "fcffead94f3c7ca495e056a9b47acdb7" \
    "51fb73e666c6c655ade8297297d07ad1" \
    "ba5e43f1bca32301651339e22904cc8c" \
    "42f58c30c04aafdb038dda0847dd988d" \
    "cda6f3bfd15c4b4c4525004aa06eeff8" \
    "ca61783aacec57fb3d1f92b0fe2fd1a8" \
    "5f6724517b65e614ad6808d6f6ee34df" \
    "f7310fdc82aebfd904b01e1dc54b2927" \
    "094b2db68d6f903b68401adebf5a7e08" \
    "d78ff4ef5d63653a65040cf9bfd4aca7" \
    "984a74d37145986780fc0b16ac451649" \
    "de6188a7dbdf191f64b5fc5e2ab47b57" \
    "f7f7276cd419c17a3ca8e1b939ae49e4" \
    "88acba6b965610b5480109c8b17b80e1" \
    "b7b750dfc7598d5d5011fd2dcc5600a3" \
    "2ef5b52a1ecc820e308aa342721aac09" \
    "43bf6686b64b2579376504ccc493d97e" \
    "6aed3fb0f9cd71a43dd497f01f17c0e2" \
    "cb3797aa2a2f256656168e6c496afc5f" \
    "b93246f6b1116398a346f1a641f3b041" \
    "e989f7914f90cc2c7fff357876e506b5" \
    "0d334ba77c225bc307ba537152f3f161" \
    "0e4eafe595f6d9d90d11faa933a15ef1" \
    "369546868a7f3a45a96768d40fd9d034" \
    "12c091c6315cf4fde7cb68606937380d" \
    "b2eaaa707b4c4185c32eddcdd306705e" \
    "4dc1ffc872eeee475a64dfac86aba41c" \
    "0618983f8741c5ef68d3a101e8a3b8ca" \
    "c60c905c15fc910840b94c00a0b9d0"

#define T1024_MSG_LEN 1023u
#define FLIPS         48u
#define GUARD         8u

static uint8_t pub[TFW_BOOT_PUBKEY_LEN];
static uint8_t sig[TFW_BOOT_SIG_LEN];
static uint8_t msg[T1024_MSG_LEN];
static uint32_t rng;

static uint8_t nib(char c)
{
    return (uint8_t)((c <= '9') ? (c - '0') : (c - 'a' + 10));
}

static void unhex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0u; i < n; i++) {
        out[i] = (uint8_t)((nib(h[i * 2u]) << 4) | nib(h[i * 2u + 1u]));
    }
}

static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    rng = 0x8032D044u;
    unhex(RFC8032_T1024_PUB, pub, sizeof pub);
    unhex(RFC8032_T1024_SIG, sig, sizeof sig);
    unhex(RFC8032_T1024_MSG, msg, sizeof msg);
}

void tearDown(void) {}

static void test_rfc8032_test1024_verifies(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, (uint16_t)T1024_MSG_LEN, sig, pub));
}

/* The verifier reads what it is given and writes none of it. */
static void test_the_inputs_are_left_as_they_were(void)
{
    uint8_t p[sizeof pub], s[sizeof sig], m[sizeof msg];

    memcpy(p, pub, sizeof p);
    memcpy(s, sig, sizeof s);
    memcpy(m, msg, sizeof m);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, (uint16_t)T1024_MSG_LEN, sig, pub));
    TEST_ASSERT_EQUAL_MEMORY(p, pub, sizeof p);
    TEST_ASSERT_EQUAL_MEMORY(s, sig, sizeof s);
    TEST_ASSERT_EQUAL_MEMORY(m, msg, sizeof m);
}

/* A prefix of a signed message is not signed: every length but the whole one
 * is refused, including 1023 narrowed to its low byte (0xFF) and the lengths
 * one short and one bit short of it. */
static void test_a_shorter_length_is_refused(void)
{
    static const uint16_t lens[] = { 0u, 1u, 255u, 256u, 511u, 767u, 1022u };
    size_t i;

    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, lens[i], sig, pub));
    }
}

/* Any one bit of the message, the signature or the key, at random: refused,
 * and refused as TFW_ERR_CRYPTO, not some other status. The vector verifies
 * again once the bit is put back. */
static void test_a_flipped_bit_anywhere_is_refused(void)
{
    uint32_t i;

    for (i = 0; i < FLIPS; i++) {
        uint32_t r = next_rand();
        uint8_t bit = (uint8_t)(1u << (r & 7u));
        uint8_t *b;
        size_t at;

        switch ((r >> 3) % 3u) {
        case 0:  b = msg; at = (r >> 5) % sizeof msg; break;
        case 1:  b = sig; at = (r >> 5) % sizeof sig; break;
        default: b = pub; at = (r >> 5) % sizeof pub; break;
        }
        b[at] ^= bit;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, (uint16_t)T1024_MSG_LEN, sig, pub));
        b[at] ^= bit;
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, (uint16_t)T1024_MSG_LEN, sig, pub));
}

/* The key in the signature's place and the signature's first half in the
 * key's: arguments taken in the wrong order are refused. */
static void test_the_key_and_signature_are_not_interchangeable(void)
{
    uint8_t s2[TFW_BOOT_SIG_LEN];

    memcpy(s2, pub, sizeof pub);
    memcpy(s2 + sizeof pub, sig + sizeof pub, sizeof sig - sizeof pub);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, (uint16_t)T1024_MSG_LEN, s2, sig));
}

/* The erased slot: NOT_PROVISIONED, the whole key cleared, and not a byte
 * past it written -- each time it is asked. */
static void test_the_unprovisioned_slot_clears_exactly_the_key(void)
{
    uint8_t buf[GUARD + TFW_BOOT_PUBKEY_LEN + GUARD];
    uint8_t keep[sizeof buf];
    size_t i;
    int round;

    for (round = 0; round < 2; round++) {
        for (i = 0; i < sizeof buf; i++) {
            buf[i] = (uint8_t)next_rand();
        }
        memcpy(keep, buf, sizeof buf);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, tfw_boot_pubkey(buf + GUARD));
        TEST_ASSERT_EQUAL_MEMORY(keep, buf, GUARD);
        for (i = 0; i < TFW_BOOT_PUBKEY_LEN; i++) {
            TEST_ASSERT_EQUAL_HEX8(0u, buf[GUARD + i]);
        }
        TEST_ASSERT_EQUAL_MEMORY(keep + GUARD + TFW_BOOT_PUBKEY_LEN,
                                 buf + GUARD + TFW_BOOT_PUBKEY_LEN, GUARD);
    }
}

/* COSE alg -8, EdDSA (RFC 9053 §2.2), CBOR-encoded as the one byte 0x27. */
static void test_the_cose_alg_is_eddsa(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x27u, tfw_boot_cose_alg);
}

/* Every length donna could ask for up to 40 bytes: exactly that many zeros,
 * nothing after them. Length 0 writes nothing. */
static void test_the_shim_clears_exactly_the_length_asked(void)
{
    uint8_t buf[40u + GUARD];
    size_t len;
    size_t i;

    for (len = 0; len <= 40u; len++) {
        memset(buf, 0xA5, sizeof buf);
        ed25519_randombytes_unsafe(buf, len);
        for (i = 0; i < len; i++) {
            TEST_ASSERT_EQUAL_HEX8(0x00u, buf[i]);
        }
        for (; i < sizeof buf; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xA5u, buf[i]);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc8032_test1024_verifies);
    RUN_TEST(test_the_inputs_are_left_as_they_were);
    RUN_TEST(test_a_shorter_length_is_refused);
    RUN_TEST(test_a_flipped_bit_anywhere_is_refused);
    RUN_TEST(test_the_key_and_signature_are_not_interchangeable);
    RUN_TEST(test_the_unprovisioned_slot_clears_exactly_the_key);
    RUN_TEST(test_the_cose_alg_is_eddsa);
    RUN_TEST(test_the_shim_clears_exactly_the_length_asked);
    return UNITY_END();
}
