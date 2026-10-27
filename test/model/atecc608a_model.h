/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Controls for the software ATECC608 -- the 608C on the bench. See
 * atecc608a.c for what it does and does not model, and why having one is
 * approved rather than assumed. */
#ifndef TFW_ATECC_MODEL_H
#define TFW_ATECC_MODEL_H

#include <stdint.h>

void     tfw_model_reset(void);
/* Gives the element a UDS and an ECC key in slot 0 -- what tfwprov does.
 *
 * BOTH ARE TEST SECRETS, derived from fixed public strings (see atecc608a.c):
 * the DeviceID key is real P-256, so evidence signed with it verifies, and
 * anyone can compute its private half. It must never be trusted. */
void     tfw_model_provision(void);
/* NACK the next n reads, as the real part does while a command executes. */
void     tfw_model_set_busy(uint8_t reads);
/* Make the part busy for this many milliseconds every time this opcode
 * arrives: it NACKs ms*2 reads, the driver's 500 us poll. Zero clears it. A
 * test gives the datasheet's execution time and proves the driver waits that
 * long -- and that a shorter wait in the driver would have failed. */
void     tfw_model_set_exec_ms(uint8_t opcode, uint16_t ms);
/* Make every command fail with this element status code. */
void     tfw_model_force_status(uint8_t status);
/* Flip the bits of mask in byte idx of the next read the part answers -- the
 * wake token or a response -- as a noisy bus would. One read, then clean. */
void     tfw_model_corrupt_next_read(uint8_t idx, uint8_t mask);
/* NACK the next command write, as a part that dropped off the bus would. */
void     tfw_model_nack_next_command(void);
void     tfw_model_set_counter(uint32_t v);
uint8_t  tfw_model_last_opcode(void);
/* The 7-bit address the last non-wake transfer went to, answered or not. */
uint8_t  tfw_model_last_addr(void);
/* The buffer the driver had the last data response (more than four bytes)
 * read into, and its length; 0 if none since tfw_model_reset(). The pointer
 * is the driver's: src/crypto/atecc608a/atecc.c reads into a file-static
 * buffer, so a test can look at what it still holds after the call. */
const uint8_t *tfw_model_last_data_read(uint8_t *len);
/* Every command the part accepted (count and CRC good), in order, so a test
 * can pin the exact sequence a driver sends. The first TFW_MODEL_LOG_MAX are
 * kept; the count goes on (to 255) so a test still sees there were more.
 * Cleared by tfw_model_reset(). */
#define TFW_MODEL_LOG_MAX 16u
typedef struct {
    uint8_t  opcode;
    uint8_t  p1;
    uint16_t p2;
    uint8_t  dlen;
    uint8_t  data[128];
} tfw_model_cmd_t;
uint8_t                tfw_model_log_count(void);
const tfw_model_cmd_t *tfw_model_log(uint8_t i);
uint8_t  tfw_model_is_awake(void);
uint8_t *tfw_model_tempkey(void);
uint8_t  tfw_model_config_locked(void);
uint8_t  tfw_model_data_locked(void);
/* The slot's contents, so a test can assert that what landed is what the
 * ELEMENT produced rather than anything the host sent. Returns NULL for a slot
 * the part does not have. */
const uint8_t *tfw_model_slot(uint8_t n);
/* The 72 bytes a clear Write put in slot n (PROV_WRITE_PUBKEY's path, every
 * slot but the UDS's), or NULL for a slot the part does not have. */
const uint8_t *tfw_model_clear_slot(uint8_t n);

/* Whether the element's Verify answers "valid" in STORED mode (the
 * bootloader's image check, whose key sits in a locked slot the model does not
 * populate). External mode is real ECDSA and ignores this. */
void           tfw_model_set_verify_result(uint8_t pass);
/* Gives a stored-mode slot a real P-256 public key (X||Y). From then on a
 * stored-mode Verify naming that slot is real ECDSA over TempKey and ignores
 * the flag above; one naming another slot still answers from the flag. Issue
 * #78's manifest suite, whose vectors carry a real signature. Cleared by
 * tfw_model_reset(). */
void           tfw_model_set_stored_pubkey(uint8_t slot, const uint8_t pub[64]);
/* No element on the board: every transfer NACKs. Distinct from a busy part --
 * a driver must tell "not there" from "not yet". */
void           tfw_model_set_absent(uint8_t yes);
/* The bus itself refuses to come up: tfw_hal_i2c_init answers TFW_ERR_BUSY.
 * Off by default; cleared by tfw_model_reset(). */
void           tfw_model_fail_i2c_init(uint8_t yes);
uint8_t        tfw_model_verify_seen(void);
/* The signature the driver actually sent, so a test can prove it forwarded the
 * one it was given rather than something it constructed. */
const uint8_t *tfw_model_verify_last_sig(void);
/* issue #4: the public key the driver sent with an External-mode (0x02)
 * Verify. Zero if the last Verify was Stored mode (0x00) or none arrived. */
const uint8_t *tfw_model_verify_last_pubkey(void);

/* ---------------------------------------------------------------- P-256
 *
 * The slot 0 key pair, for tests that check what the element produced. NULL
 * until a key exists (tfw_model_provision, or a GenKey in private mode). The
 * private half is a TEST oracle: the real part never discloses it. */
const uint8_t *tfw_model_deviceid_public(void);
const uint8_t *tfw_model_deviceid_private(void);
/* The key tfw_model_provision() installs, without installing it -- so a test
 * can pin the documented public point. */
void           tfw_model_test_deviceid(uint8_t priv[32], uint8_t pub[64]);

/* The host peer's side of the curve, through the same vendored library as the
 * element: a public point from a private scalar, an ECDSA signature over a
 * 32-byte digest (deterministic k, so reproducible), and a verification. They return 1
 * on success and 0 otherwise. A test uses them to play the OPERATOR, whose key
 * is never in the element. */
int            tfw_model_p256_public(const uint8_t priv[32], uint8_t pub[64]);
int            tfw_model_p256_sign(const uint8_t priv[32], const uint8_t digest[32],
                                  uint8_t sig[64]);
int            tfw_model_p256_verify(const uint8_t pub[64], const uint8_t digest[32],
                                    const uint8_t sig[64]);
/* n more wakes (or command writes) succeed, then every one fails -- a part that
 * drops off the bus partway through a sequence. Off by default; cleared by
 * tfw_model_reset(). */
void           tfw_model_fail_wakes_after(uint8_t n);
void           tfw_model_fail_commands_after(uint8_t n);

#endif /* TFW_ATECC_MODEL_H */
