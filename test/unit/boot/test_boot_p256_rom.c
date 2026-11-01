/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32-P4 layer 0's ES256 verify through the ROM
 * (boot/hal/esp32/verify_p256_rom.c, #48), against a host ROM that takes the
 * key and signature little-endian as ESP-IDF documents (esp_rom_ecdsa_mock.c):
 * every Wycheproof P-256 P1363 vector gets the verdict p256-soft gives it, so
 * the byte order is right, and the backend believes the ROM's 1 and nothing
 * else.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_verify.h"
#include "esp_rom_ecdsa_mock.h"
#include "rom/ecdsa.h"
#include "sha256.h"
#include "wycheproof_p256.h"

/* The host test's key slot, in place of the generated one. */
uint8_t tfw_boot_p256_key[64];

void setUp(void)
{
    tfw_rom_ecdsa_mock_reset();
}

void tearDown(void) {}

static uint16_t first_valid_of_key0(void)
{
    uint16_t i;

    for (i = 0u; i < WYC_NVECS; i++) {
        if (wyc_vecs[i].key == 0u && wyc_vecs[i].valid) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("no valid vector for key 0");
    return 0u;
}

/* Vector v through the backend, its key in the slot. */
static tfw_status_t verify_vec(uint16_t v, const uint8_t sig[64])
{
    memcpy(tfw_boot_p256_key, wyc_keys[wyc_vecs[v].key], 64u);
    return tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off], wyc_vecs[v].len, sig, 0);
}

static void test_every_wycheproof_vector_gets_its_verdict(void)
{
    uint16_t i;
    uint16_t valid = 0u;
    uint16_t wrong = 0u;

    for (i = 0u; i < WYC_NVECS; i++) {
        tfw_status_t want = wyc_vecs[i].valid ? TFW_OK : TFW_ERR_CRYPTO;
        tfw_status_t got  = verify_vec(i, wyc_vecs[i].sig);
        if (got != want) {
            printf("WRONG tcId %u, expected %d, got %d, %s\n",
                        (unsigned)wyc_vecs[i].tcid, (int)want, (int)got, wyc_vecs[i].why);
            wrong++;
        }
        valid = (uint16_t)(valid + wyc_vecs[i].valid);
    }
    TEST_ASSERT_EQUAL_UINT16(0u, wrong);
    TEST_ASSERT_EQUAL_UINT16(173u, valid);
    TEST_ASSERT_EQUAL_UINT(WYC_NVECS, tfw_rom_ecdsa_mock_calls());
}

static void test_the_rom_is_asked_for_p256_over_the_sha256(void)
{
    uint16_t v = first_valid_of_key0();
    uint8_t  h[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, verify_vec(v, wyc_vecs[v].sig));
    TEST_ASSERT_EQUAL_INT(ECDSA_CURVE_P256, tfw_rom_ecdsa_mock_curve());
    tfw_sha256(&wyc_msgs[wyc_vecs[v].off], wyc_vecs[v].len, h);
    TEST_ASSERT_EQUAL_MEMORY(h, tfw_rom_ecdsa_mock_digest(), 32u);
}

/* The host ROM is not symmetric with the backend by accident: the same key and
 * signature handed over big-endian, as the trailer carries them, fail. */
static void test_big_endian_halves_fail_at_the_rom(void)
{
    uint16_t v = first_valid_of_key0();
    uint8_t  h[32];
    uint8_t  out[32];

    tfw_sha256(&wyc_msgs[wyc_vecs[v].off], wyc_vecs[v].len, h);
    TEST_ASSERT_EQUAL_INT(0, ets_ecdsa_verify(wyc_keys[0], wyc_vecs[v].sig,
                                              ECDSA_CURVE_P256, h, out));
}

static void test_a_bit_flipped_anywhere_is_refused(void)
{
    uint16_t v = first_valid_of_key0();
    uint8_t  sig[64];
    uint8_t  msg[256];
    uint16_t len = wyc_vecs[v].len;
    uint8_t  i;

    /* r's and s's first and last bytes, each end of each half. */
    static const uint8_t at[] = { 0u, 31u, 32u, 63u };
    for (i = 0u; i < (uint8_t)sizeof at; i++) {
        memcpy(sig, wyc_vecs[v].sig, 64u);
        sig[at[i]] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_vec(v, sig));
    }

    TEST_ASSERT_TRUE(len > 0u && len <= sizeof msg);
    memcpy(msg, &wyc_msgs[wyc_vecs[v].off], len);
    msg[len - 1u] ^= 0x80u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, len, wyc_vecs[v].sig, 0));

    tfw_boot_p256_key[63] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off], len,
                                                          wyc_vecs[v].sig, 0));
}

static void test_only_the_roms_1_passes(void)
{
    uint16_t v = first_valid_of_key0();
    uint8_t  sig[64];

    memcpy(sig, wyc_vecs[v].sig, 64u);
    sig[5] ^= 0x01u;                    /* a bad signature, the ROM's word taken */
    tfw_rom_ecdsa_mock_answer(1);
    TEST_ASSERT_EQUAL_INT(TFW_OK, verify_vec(v, sig));
    tfw_rom_ecdsa_mock_answer(0);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_vec(v, wyc_vecs[v].sig));
    tfw_rom_ecdsa_mock_answer(2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_vec(v, wyc_vecs[v].sig));
    tfw_rom_ecdsa_mock_answer(-1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_vec(v, wyc_vecs[v].sig));
}

static void test_an_erased_slot_refuses_without_asking_the_rom(void)
{
    uint16_t v = first_valid_of_key0();

    tfw_rom_ecdsa_mock_answer(1);
    memset(tfw_boot_p256_key, 0xFF, sizeof tfw_boot_p256_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                          wyc_vecs[v].len, wyc_vecs[v].sig, 0));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_rom_ecdsa_mock_calls());
}

static void test_the_contract_matches_p256_soft(void)
{
    uint8_t buf[TFW_BOOT_PUBKEY_LEN];
    uint8_t sig[TFW_BOOT_SIG_LEN];

    memset(buf, 0, sizeof buf);
    memset(sig, 0, sizeof sig);
    memcpy(tfw_boot_p256_key, wyc_keys[0], 64u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(buf, 1u, sig, buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(0, 0u, sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(buf, 1u, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_rom_ecdsa_mock_calls());
}

/* The COSE algorithm this backend names goes into the protected header of the
 * OTA_ACTIVATE authorization (#137), which a host signs: ES256 is COSE
 * algorithm -7 (RFC 9053 section 2.1), CBOR's negative integer 0x26. EdDSA,
 * -8, is 0x27, donna's. The one #163 mutant the cases above left alive. */
static void test_the_backend_names_es256(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x26u, tfw_boot_cose_alg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_wycheproof_vector_gets_its_verdict);
    RUN_TEST(test_the_rom_is_asked_for_p256_over_the_sha256);
    RUN_TEST(test_big_endian_halves_fail_at_the_rom);
    RUN_TEST(test_a_bit_flipped_anywhere_is_refused);
    RUN_TEST(test_only_the_roms_1_passes);
    RUN_TEST(test_an_erased_slot_refuses_without_asking_the_rom);
    RUN_TEST(test_the_contract_matches_p256_soft);
    RUN_TEST(test_the_backend_names_es256);
    return UNITY_END();
}
