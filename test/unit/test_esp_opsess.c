/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32-P4 operator session (row #606): src/core/auth_session.c over
 * src/hal/esp32/crypto_hmac_p256.c, on the host, with the HMAC peripheral and
 * the eFuse purpose lookup faked as test_esp_crypto_hmac_p256 fakes them. The
 * operator key in the platform block is the operator's only when its
 * operator_bind tag is HMAC(UDS, "tfw-opsess-v1" || key)
 * (docs/multi-arch/esp32-p4-design.md 2.5). The fake is the one place the
 * UDS is held; the provider never sees it. Each of the tag's 32 bytes is
 * checked on its own: a tag wrong in any one byte is refused (#48).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "auth_session.h"
#include "boot_platform.h"
#include "platform_id.h"
#include "handoff.h"
#include "sha256.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "tfw/hal.h"

static uint8_t           uds[32];
static int               uds_burned;
static esp_efuse_block_t uds_block;
static unsigned          hmac_calls;
/* hmac_fail: the peripheral computes the true HMAC and then refuses it, so
 * a unit that ignored the return would still see a good tag. nvm_err: the
 * read copies the bytes and then returns this, for the same reason. */
static int               hmac_fail;
static unsigned          nvm_reads;
static tfw_status_t      nvm_err;
static uint8_t           test_handoff[TFW_BOOT_HANDOFF_LEN];
static uint8_t           test_nvm[TFW_BOOT_EE_LEN];

static const uint8_t SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0x60, 0x6E, 0x50, 0x34, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
};
static uint8_t operator_priv[32], operator_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t stranger_priv[32], stranger_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];

/* --------------------------------------------------------- the ESP-IDF fakes */
bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block)
{
    if (purpose != ESP_EFUSE_KEY_PURPOSE_HMAC_UP || !uds_burned) {
        return false;
    }
    *block = uds_block;
    return true;
}

esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message,
                             size_t message_len, uint8_t *hmac)
{
    hmac_calls++;
    if (!uds_burned || (int)key_id != (int)uds_block - (int)EFUSE_BLK_KEY0) {
        return ESP_ERR_INVALID_ARG;
    }
    tfw_hmac_sha256(uds, (uint8_t)sizeof uds, (const uint8_t *)message,
                    (uint16_t)message_len, hmac);
    if (hmac_fail) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* The ADC lock the entropy source shares (#607): held elsewhere when
 * adc_held, and then no nonce may be drawn. */
static int      adc_held;
static int      entropy_on;
static unsigned plain_draws;

esp_err_t adc_lock_try_acquire(adc_unit_t adc_unit)
{
    (void)adc_unit;
    return adc_held ? ESP_FAIL : ESP_OK;
}

esp_err_t adc_lock_release(adc_unit_t adc_unit)
{
    (void)adc_unit;
    return ESP_OK;
}

void bootloader_random_enable(void)  { entropy_on = 1; }
void bootloader_random_disable(void) { entropy_on = 0; }

void esp_fill_random(void *buf, size_t len)
{
    static uint8_t ctr;
    uint8_t       *p = buf;
    size_t         i;

    if (!entropy_on) { plain_draws++; }
    for (i = 0u; i < len; i++) { p[i] = (uint8_t)(0x5Au ^ ctr++); }
}

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    (void)level; (void)tag; (void)fmt;
}

uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    nvm_reads++;
    if ((uint32_t)addr + len > sizeof test_nvm) {
        return TFW_ERR_CRC;
    }
    memcpy(buf, &test_nvm[addr], len);
    return nvm_err;
}

/* ------------------------------------------------------------------- helpers */
/* What provisioning writes at operator_bind: the tag under the fake's UDS. */
static void bind_tag(const uint8_t pub[TFW_BOOT_OPERATOR_PUBKEY_LEN], uint8_t tag[32])
{
    static const char label[] = "tfw-opsess-v1";
    uint8_t           msg[sizeof label - 1u + TFW_BOOT_OPERATOR_PUBKEY_LEN];

    memcpy(msg, label, sizeof label - 1u);
    memcpy(msg + sizeof label - 1u, pub, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    tfw_hmac_sha256(uds, (uint8_t)sizeof uds, msg, (uint16_t)sizeof msg, tag);
}

/* Bake the platform block: `pub` as operator key, `tag` at operator_bind. */
static void bake(const uint8_t pub[TFW_BOOT_OPERATOR_PUBKEY_LEN], const uint8_t tag[32])
{
    tfw_boot_platform_t plat;

    memset(test_nvm, 0, sizeof test_nvm);
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, pub, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    memcpy(&test_nvm[TFW_BOOT_EE_O_OPERATOR_BIND], tag, TFW_BOOT_OPERATOR_BIND_LEN);
    tfw_boot_platform_serialise(&plat, test_nvm);
    tfw_platform_id_init();
}

static void bake_bound(const uint8_t pub[TFW_BOOT_OPERATOR_PUBKEY_LEN])
{
    uint8_t tag[32];

    bind_tag(pub, tag);
    bake(pub, tag);
}

static void key(uint8_t seed, uint8_t priv[32], uint8_t pub[64])
{
    memset(priv, 0, 32);
    priv[31] = seed;
    priv[0]  = 0x11u;
    TEST_ASSERT_TRUE(uECC_compute_public_key(priv, pub, uECC_secp256r1()));
}

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

static tfw_status_t challenge(uint8_t nonce[TFW_AUTH_NONCE_LEN])
{
    tfw_status_t st;

    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    st = tfw_auth_cmd_challenge(&r, &w);
    memcpy(nonce, outbuf, TFW_AUTH_NONCE_LEN);
    return st;
}

static tfw_status_t response(const uint8_t sig[TFW_AUTH_SIG_LEN])
{
    memcpy(inbuf, sig, TFW_AUTH_SIG_LEN);
    tfw_reader_init(&r, inbuf, TFW_AUTH_SIG_LEN);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_auth_cmd_response(&r, &w);
}

/* The operator's half: ES256 over "tfw-auth-v1" || serial || nonce. */
static void sign(const uint8_t priv[32], const uint8_t nonce[TFW_AUTH_NONCE_LEN],
                 uint8_t sig[TFW_AUTH_SIG_LEN])
{
    static const char domain[] = "tfw-auth-v1";
    uint8_t           msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];
    uint8_t           digest[32];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(msg + sizeof domain - 1u, SERIAL, TFW_BOOT_SERIAL_LEN);
    memcpy(msg + sizeof domain - 1u + TFW_BOOT_SERIAL_LEN, nonce, TFW_AUTH_NONCE_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, digest);
    TEST_ASSERT_TRUE(uECC_sign(priv, digest, sizeof digest, sig, uECC_secp256r1()));
}

static tfw_status_t authenticate_with(const uint8_t priv[32])
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign(priv, nonce, sig);
    return response(sig);
}

void setUp(void)
{
    uint8_t i;

    for (i = 0u; i < 32u; i++) { uds[i] = (uint8_t)(0xC0u + i); }
    uds_burned = 1;
    uds_block  = EFUSE_BLK_KEY3;
    hmac_calls = 0u;
    hmac_fail  = 0;
    nvm_err    = TFW_OK;
    adc_held   = 0;
    entropy_on = 0;
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    key(0x01u, operator_priv, operator_pub);
    key(0x02u, stranger_priv, stranger_pub);
    bake_bound(operator_pub);
    tfw_auth_session_init();
}

void tearDown(void) {}

/* -------------------------------------------------------------------- tests */
static void test_the_bound_operators_signature_opens_the_gate(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
}

static void test_the_tag_comes_from_the_peripheral(void)
{
    unsigned before = hmac_calls;

    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_TRUE(hmac_calls > before);
}

static void test_a_bad_signature_is_refused(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign(operator_priv, nonce, sig);
    sig[17] ^= 0x01u;
    TEST_ASSERT_NOT_EQUAL(TFW_OK, response(sig));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

static void test_another_keys_signature_is_refused(void)
{
    TEST_ASSERT_NOT_EQUAL(TFW_OK, authenticate_with(stranger_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* A key written into flash without the device's peripheral: its own signature
 * is good, but nothing bound it, so it is not the operator. */
static void test_an_unbound_key_is_refused_even_with_its_own_signature(void)
{
    uint8_t zero[32] = {0};

    bake(stranger_pub, zero);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, authenticate_with(stranger_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* The rewrite copies the operator's tag beside the attacker's key. */
static void test_a_tag_moved_to_another_key_is_refused(void)
{
    uint8_t tag[32];

    bind_tag(operator_pub, tag);
    bake(stranger_pub, tag);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, authenticate_with(stranger_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* A tag made under another UDS: another part's block copied onto this one. */
static void test_another_parts_tag_is_refused(void)
{
    uint8_t tag[32];

    bind_tag(operator_pub, tag);
    tag[0] ^= 0x80u;
    bake(operator_pub, tag);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* #48: every byte of the tag is compared, not a prefix of it. One bit wrong in
 * any one of the 32 bytes and the bound operator's own signature is refused;
 * the true tag baked again opens the gate, so the refusals met a live gate. */
static void test_a_tag_wrong_in_any_one_byte_is_refused(void)
{
    uint8_t tag[32];
    uint8_t i;
    char    msg[24];

    for (i = 0u; i < (uint8_t)sizeof tag; i++) {
        bind_tag(operator_pub, tag);
        tag[i] ^= 0x01u;
        bake(operator_pub, tag);
        tfw_auth_session_init();
        snprintf(msg, sizeof msg, "tag byte %u", (unsigned)i);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, authenticate_with(operator_priv), msg);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), msg);
    }
    bake_bound(operator_pub);
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
}

static void test_no_uds_means_no_session(void)
{
    uds_burned = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_NOT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* #607: no entropy source, no challenge -- never a nonce drawn with the
 * source off, and the gate stays shut. */
static void test_a_busy_entropy_source_refuses_the_challenge(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN];

    adc_held    = 1;
    plain_draws = 0u;
    TEST_ASSERT_NOT_EQUAL(TFW_OK, challenge(nonce));
    TEST_ASSERT_EQUAL_UINT(0u, plain_draws);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* Every operator nonce is drawn with the entropy source on. */
static void test_the_challenge_is_drawn_with_the_entropy_source_on(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN];

    plain_draws = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    TEST_ASSERT_EQUAL_UINT(0u, plain_draws);
    TEST_ASSERT_FALSE(entropy_on);
}

/* tfw_crypto_verify_operator at the unit, not through the session. */
static const uint8_t D[32] = {
    0x5Au, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
    0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu,
    0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0xA5u,
};

static tfw_status_t verify(const uint8_t priv[32], const uint8_t digest[32],
                           const uint8_t pub[64])
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32u, sig, uECC_secp256r1()));
    return tfw_crypto_verify_operator(digest, sig, pub);
}

static void test_verify_operator_accepts_the_bound_key_and_its_signature(void)
{
    unsigned before = hmac_calls;

    nvm_reads = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, verify(operator_priv, D, operator_pub));
    TEST_ASSERT_EQUAL_UINT(1u, nvm_reads);
    TEST_ASSERT_EQUAL_UINT(before + 1u, hmac_calls);
}

static void test_verify_operator_refuses_an_unreadable_platform_block(void)
{
    unsigned before = hmac_calls;

    nvm_reads = 0u;
    nvm_err   = TFW_ERR_CRC;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, verify(operator_priv, D, operator_pub));
    TEST_ASSERT_EQUAL_UINT(1u, nvm_reads);
    TEST_ASSERT_EQUAL_UINT(before, hmac_calls);
}

static void test_verify_operator_refuses_when_the_peripheral_refuses_the_bind(void)
{
    unsigned before = hmac_calls;

    hmac_fail = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, verify(operator_priv, D, operator_pub));
    TEST_ASSERT_EQUAL_UINT(before + 1u, hmac_calls);
}

static void test_verify_operator_refuses_a_tag_off_in_its_last_byte(void)
{
    uint8_t tag[32];

    bind_tag(operator_pub, tag);
    tag[31] ^= 0x01u;
    bake(operator_pub, tag);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, verify(operator_priv, D, operator_pub));
}

static void test_verify_operator_without_a_uds_reads_nothing(void)
{
    unsigned before;

    uds_burned = 0;
    (void)tfw_crypto_init();
    nvm_reads = 0u;
    before    = hmac_calls;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, verify(operator_priv, D, operator_pub));
    TEST_ASSERT_EQUAL_UINT(0u, nvm_reads);
    TEST_ASSERT_EQUAL_UINT(before, hmac_calls);
}

static void test_verify_operator_refuses_a_signature_over_another_digest(void)
{
    uint8_t sig[64];
    uint8_t d2[32];

    TEST_ASSERT_TRUE(uECC_sign(operator_priv, D, 32u, sig, uECC_secp256r1()));
    memcpy(d2, D, sizeof d2);
    d2[0] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(d2, sig, operator_pub));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_bound_operators_signature_opens_the_gate);
    RUN_TEST(test_the_tag_comes_from_the_peripheral);
    RUN_TEST(test_a_bad_signature_is_refused);
    RUN_TEST(test_another_keys_signature_is_refused);
    RUN_TEST(test_an_unbound_key_is_refused_even_with_its_own_signature);
    RUN_TEST(test_a_tag_moved_to_another_key_is_refused);
    RUN_TEST(test_another_parts_tag_is_refused);
    RUN_TEST(test_a_tag_wrong_in_any_one_byte_is_refused);
    RUN_TEST(test_no_uds_means_no_session);
    RUN_TEST(test_a_busy_entropy_source_refuses_the_challenge);
    RUN_TEST(test_the_challenge_is_drawn_with_the_entropy_source_on);
    RUN_TEST(test_verify_operator_accepts_the_bound_key_and_its_signature);
    RUN_TEST(test_verify_operator_refuses_an_unreadable_platform_block);
    RUN_TEST(test_verify_operator_refuses_when_the_peripheral_refuses_the_bind);
    RUN_TEST(test_verify_operator_refuses_a_tag_off_in_its_last_byte);
    RUN_TEST(test_verify_operator_without_a_uds_reads_nothing);
    RUN_TEST(test_verify_operator_refuses_a_signature_over_another_digest);
    return UNITY_END();
}
