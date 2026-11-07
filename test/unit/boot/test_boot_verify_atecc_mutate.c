/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/verify/verify_atecc608a.c under mutation (#163), against the software
 * element. test_boot_element proves the backend works; this suite proves each
 * check in it is load-bearing: every test here fails against a copy of the
 * backend with one comparison flipped, one check dropped, one bound changed or
 * one call skipped.
 *
 * The hash is the software one (measure_soft.c), not the element's, so the
 * Nonce and the Verify are the only commands the part sees and the first read
 * of a verify is its wake token. The digest is the same bytes either way;
 * test_boot_element pins that.
 *
 * Real ECDSA wherever a verdict is asserted: the image-signing slot holds a
 * real P-256 public key (tfw_model_set_stored_pubkey), and the signature is the
 * operator's over SHA-256 of the message. A flag that answers "valid" regardless
 * would let a mutant that verifies the wrong digest, or with the wrong slot,
 * pass.
 */

#include <string.h>
#include "unity.h"
#include "boot_verify.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "atecc.h"
#include "sha256.h"

#define SLOT_IMAGE_SIGNING 2u

/* The operator's pair, made in setUp: no private key is a literal. */
static uint8_t g_priv[32];

static const uint8_t MSG[] = "tfw-boot trailer v1, signed bytes";
#define MSG_LEN ((uint16_t)(sizeof MSG - 1u))

static uint8_t g_pub[64];
static uint8_t g_sig[64];
static uint8_t g_digest[32];

void setUp(void)
{
    tfw_model_reset();
    tfw_boot_mock_reset();
    tfw_model_p256_key("operator", g_priv, g_pub);
    tfw_sha256(MSG, MSG_LEN, g_digest);
    TEST_ASSERT_EQUAL_INT(1, tfw_model_p256_sign(g_priv, g_digest, g_sig));
}
void tearDown(void) {}

/* The operator's key in the image-signing slot, as provisioning leaves it. */
static void provision_image_key(void)
{
    tfw_model_set_stored_pubkey(SLOT_IMAGE_SIGNING, g_pub);
}

/* ---------------------------------------------------------------- verdicts */

static void test_the_operators_signature_verifies_in_the_image_slot(void)
{
    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
}

/* Kills: a digest over the wrong bytes, a dropped or off-by-one msglen. */
static void test_one_flipped_message_byte_is_refused(void)
{
    uint8_t msg[sizeof MSG];

    provision_image_key();
    memcpy(msg, MSG, sizeof MSG);
    msg[MSG_LEN - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, g_sig, 0));
}

static void test_a_shorter_message_is_refused(void)
{
    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO,
                      tfw_boot_verify(MSG, (uint16_t)(MSG_LEN - 1u), g_sig, 0));
}

static void test_one_flipped_signature_byte_is_refused(void)
{
    uint8_t sig[64];

    provision_image_key();
    memcpy(sig, g_sig, sizeof sig);
    sig[63] ^= 0x80u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, sig, 0));
}

/* Kills: Verify naming any slot but the image-signing one. The key sits in
 * slot 1 here; a slot the model holds no key for answers from the flag, which
 * is "invalid", so only a backend that names slot 1 would pass. */
static void test_a_key_in_another_slot_does_not_verify(void)
{
    tfw_model_set_stored_pubkey(1u, g_pub);
    tfw_model_set_stored_pubkey(0u, g_pub);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
}

/* ---------------------------------------------------------------- the frames */

/* Kills: a different Nonce mode, a digest other than SHA-256 of the message, a
 * stored-mode change, a slot change, a signature length change, a second or a
 * missing command. */
static void test_the_part_sees_one_nonce_then_one_stored_verify(void)
{
    const tfw_model_cmd_t *c;

    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_model_log_count());

    c = tfw_model_log(0u);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_NONCE, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_NONCE_PASSTHRU, c->p1);
    TEST_ASSERT_EQUAL_HEX16(0u, c->p2);
    TEST_ASSERT_EQUAL_UINT8(32u, c->dlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_digest, c->data, 32);

    c = tfw_model_log(1u);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_VERIFY, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_VERIFY_STORED, c->p1);
    TEST_ASSERT_EQUAL_HEX16(SLOT_IMAGE_SIGNING, c->p2);
    TEST_ASSERT_EQUAL_UINT8(64u, c->dlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_sig, c->data, 64);
    TEST_ASSERT_EACH_EQUAL_HEX8(0u, tfw_model_verify_last_pubkey(), 64);
}

/* Kills: a dropped sleep. TempKey holds the image digest until the part
 * sleeps; a reset between commands must not find it there. */
static void test_the_part_sleeps_after_a_pass(void)
{
    static const uint8_t zero[32] = { 0 };

    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, tfw_model_tempkey(), 32);
}

static void test_the_part_sleeps_after_a_refusal(void)
{
    uint8_t sig[64] = { 0 };

    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, sig, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* ---------------------------------------------------------------- refusals */

/* Kills: dropping the caller-key check, or flipping it. Nothing reaches the
 * part, so a key the caller substituted cannot be half-honoured. The key is
 * the element's, named by a slot: honouring a supplied one would mean
 * verifying against a key an attacker who reached the flash could have put
 * there -- which is the entire thing stored mode exists to prevent. */
static void test_a_caller_key_is_unsupported_and_nothing_is_sent(void)
{
    provision_image_key();
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_verify(MSG, MSG_LEN, g_sig, g_pub));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_verify_seen());
}

/* Kills: dropping the msg check, or joining the two with && -- an empty
 * message with no buffer hashes without touching memory, so only the check
 * refuses it. */
static void test_no_message_is_refused_before_the_part(void)
{
    tfw_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(0, 0u, g_sig, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

static void test_no_signature_is_refused_before_the_part(void)
{
    tfw_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* Kills: ignoring the wake. The part is awake but its token is garbled, so a
 * backend that carried on would get a valid verdict from it. */
static void test_a_garbled_wake_is_a_refusal_not_a_pass(void)
{
    provision_image_key();
    tfw_model_corrupt_next_read(0u, 0xFFu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_verify_seen());
}

/* Kills: running the Verify after a failed Nonce. The Nonce outlasts its
 * datasheet time, so the driver gives up on it, though the part did load
 * TempKey and would verify. The backend must not ask. */
static void test_a_nonce_that_times_out_skips_the_verify(void)
{
    provision_image_key();
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE, (uint16_t)(TFW_ATECC_T_NONCE + 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_verify_seen());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
}

/* Kills: a shorter wait bound on either command. Each takes its datasheet
 * time plus the driver's last millisecond of slack, the longest the bound
 * admits (the tests either side show one more is refused), and still
 * answers. A bound one millisecond shorter gives up first. */
static void test_each_command_is_waited_for_its_datasheet_time(void)
{
    provision_image_key();
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE, (uint16_t)(TFW_ATECC_T_NONCE + 1u));
    tfw_model_set_exec_ms(TFW_ATECC_OP_VERIFY, (uint16_t)(TFW_ATECC_T_VERIFY + 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
}

/* Kills: returning the driver's status as is. A Verify that timed out is the
 * same refusal as a bad signature, not a TIMEOUT a caller might retry. */
static void test_a_verify_that_times_out_is_a_crypto_refusal(void)
{
    provision_image_key();
    tfw_model_set_exec_ms(TFW_ATECC_OP_VERIFY, (uint16_t)(TFW_ATECC_T_VERIFY + 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
}

static void test_an_element_status_is_a_crypto_refusal(void)
{
    provision_image_key();
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify(MSG, MSG_LEN, g_sig, 0));
}

/* ---------------------------------------------------------------- constants */

/* There is no way to read the key out, because the bootloader never has it.
 * A backend that handed one back would invite a caller to verify in software,
 * which is the arrangement this one exists to avoid. */
static void test_the_key_cannot_be_read_out_and_the_buffer_is_untouched(void)
{
    uint8_t pub[64];

    memset(pub, 0xA5, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(pub));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, pub, 64);
}

/* COSE alg -7, ES256 (RFC 9053 section 2.1), as its CBOR encoding 0x26. */
static void test_the_cose_alg_is_es256(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x26u, tfw_boot_cose_alg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_operators_signature_verifies_in_the_image_slot);
    RUN_TEST(test_one_flipped_message_byte_is_refused);
    RUN_TEST(test_a_shorter_message_is_refused);
    RUN_TEST(test_one_flipped_signature_byte_is_refused);
    RUN_TEST(test_a_key_in_another_slot_does_not_verify);
    RUN_TEST(test_the_part_sees_one_nonce_then_one_stored_verify);
    RUN_TEST(test_the_part_sleeps_after_a_pass);
    RUN_TEST(test_the_part_sleeps_after_a_refusal);
    RUN_TEST(test_a_caller_key_is_unsupported_and_nothing_is_sent);
    RUN_TEST(test_no_message_is_refused_before_the_part);
    RUN_TEST(test_no_signature_is_refused_before_the_part);
    RUN_TEST(test_a_garbled_wake_is_a_refusal_not_a_pass);
    RUN_TEST(test_a_nonce_that_times_out_skips_the_verify);
    RUN_TEST(test_each_command_is_waited_for_its_datasheet_time);
    RUN_TEST(test_a_verify_that_times_out_is_a_crypto_refusal);
    RUN_TEST(test_an_element_status_is_a_crypto_refusal);
    RUN_TEST(test_the_key_cannot_be_read_out_and_the_buffer_is_untouched);
    RUN_TEST(test_the_cose_alg_is_es256);
    return UNITY_END();
}
