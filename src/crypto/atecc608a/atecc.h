/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATECC608A transport: one command in, one response out.
 *
 * This is the whole element driver. It is small on purpose -- see
 * docs/attestation/cryptoauthlib-evaluation.md for why Microchip's own library
 * is not linked here (its licence restricts use to Microchip products, which
 * AGPL-3.0 cannot carry, and its command layer alone measures 12 475 bytes
 * against the 4 684 this part has left).
 *
 * The opcodes, modes and packet layout below are facts about the silicon, not
 * anyone's design. The implementation is ours.
 *
 * Nothing here knows about DICE. provider.c builds identity on top; this file
 * moves bytes and checks CRCs, and that separation is what lets the element
 * protocol be tested against a software model with no key material in sight.
 */
#ifndef TFW_ATECC_H
#define TFW_ATECC_H

#include <stdint.h>
#include "tfw/status.h"

/* The element's I2C address. Blacklisted from the generic I2C_XFER command --
 * see src/caps/bus.c -- because a host that can talk to the element directly
 * can ask it to sign things this firmware never agreed to. */
#define TFW_ATECC_ADDR   0x60u

/* The bus rate the wake pulse depends on -- see tfw_atecc_wake()'s comment.
 * I2C_CONFIG (src/caps/bus.c) reconfigures this same TWI peripheral for the
 * generic bus, and its whole point is to let a host leave it somewhere else
 * than the boot default. tfw_atecc_wake() re-asserts this rate itself, every
 * time, rather than trusting whatever the generic command left behind --
 * the alternative is an element that answers until the first I2C_CONFIG and
 * silently stops. */
#define TFW_ATECC_I2C_HZ 100000uL

/* Word addresses. The first byte of every I2C write selects what follows. */
#define TFW_ATECC_WA_RESET   0x00u
#define TFW_ATECC_WA_SLEEP   0x01u
#define TFW_ATECC_WA_IDLE    0x02u
#define TFW_ATECC_WA_COMMAND 0x03u

/* Opcodes. */
#define TFW_ATECC_OP_READ      0x02u
#define TFW_ATECC_OP_NONCE     0x16u
#define TFW_ATECC_OP_LOCK      0x17u
#define TFW_ATECC_OP_RANDOM    0x1Bu
#define TFW_ATECC_OP_COUNTER   0x24u
#define TFW_ATECC_OP_INFO      0x30u
#define TFW_ATECC_OP_GENKEY    0x40u
#define TFW_ATECC_OP_SIGN      0x41u
#define TFW_ATECC_OP_PRIVWRITE 0x46u
#define TFW_ATECC_OP_SHA       0x47u
#define TFW_ATECC_OP_WRITE     0x12u
#define TFW_ATECC_OP_VERIFY    0x45u

/* Modes, for the commands this firmware issues. */
#define TFW_ATECC_SHA_SHA256_START  0x00u
#define TFW_ATECC_SHA_SHA256_UPDATE 0x01u
#define TFW_ATECC_SHA_SHA256_END    0x02u
#define TFW_ATECC_SHA_HMAC_START  0x04u
#define TFW_ATECC_SHA_HMAC_UPDATE 0x01u
#define TFW_ATECC_SHA_HMAC_END    0x05u
#define TFW_ATECC_NONCE_PASSTHRU  0x03u
#define TFW_ATECC_SIGN_EXTERNAL   0x80u
/* Verify, External Public Key mode: the datasheet's 5.2.4.1. The public key
 * travels in the command rather than naming a slot -- unlike
 * boot/verify/verify_atecc608a.c's own Verify use, which is deliberately
 * Stored mode for a different reason (see that file). Param2/KeyID 0x0004
 * names the P-256 curve, not a slot; it is a curve selector in this mode. */
#define TFW_ATECC_VERIFY_EXTERNAL   0x02u
#define TFW_ATECC_VERIFY_KEYID_P256 0x0004u
#define TFW_ATECC_GENKEY_PRIVATE  0x04u
#define TFW_ATECC_GENKEY_PUBLIC   0x00u
#define TFW_ATECC_COUNTER_READ    0x00u
#define TFW_ATECC_COUNTER_INCR    0x01u
#define TFW_ATECC_INFO_REVISION   0x00u
#define TFW_ATECC_LOCK_CONFIG     0x00u
#define TFW_ATECC_LOCK_DATA       0x01u
#define TFW_ATECC_LOCK_NO_CRC     0x80u
#define TFW_ATECC_ZONE_CONFIG     0x00u
#define TFW_ATECC_ZONE_DATA       0x02u
#define TFW_ATECC_ZONE_32         0x80u

/*
 * Slot assignment for the DICE-HW profile. These are decisions, and they are
 * here rather than scattered through provider.c because a provisioning template
 * has to agree with them exactly and a mismatch is not detectable at run time --
 * the element would simply refuse, or worse, succeed against the wrong key.
 */
#define TFW_ATECC_SLOT_DEVICEID 0u  /* ECC private key, never readable, the DICE ECA key */
#define TFW_ATECC_SLOT_UDS      1u  /* the UDS, as an HMAC key that never leaves the part */
#define TFW_ATECC_COUNTER_BOOT  0u  /* monotonic boot count and the anti-rollback floor */

/* The largest command payload tfw_atecc_exec's SHARED, FILE-STATIC buffer
 * carries. PrivWrite carries 36 bytes of key plus a 32-byte MAC, which is the
 * worst case and is provisioning-only; SHA HMAC_Update's 64-byte block is the
 * worst case in ordinary operation.
 *
 * Verify in External mode (issue #4's AUTH_RESPONSE) needs 128 -- a 64-byte
 * signature plus a 64-byte public key -- and is deliberately NOT counted
 * here: growing this constant would grow tfw_atecc_exec's static buffer by 60
 * bytes for EVERY element-tier build, whether or not AUTH_CHALLENGE/
 * AUTH_RESPONSE is even compiled in, and blew the ATmega2560's RAM budget by
 * itself. tfw_atecc_verify_external below stages its own, larger command on
 * the stack instead -- a transient cost paid only while that one call runs,
 * not a permanent one every other caller here also pays. */
#define TFW_ATECC_MAX_DATA 68u

/* Wakes the element and confirms it answered. Safe to call repeatedly. */
tfw_status_t tfw_atecc_wake(void);

/* Puts the element to sleep, zeroing its volatile state including TempKey.
 * Called after every operation that loaded a secret into it, so a reset between
 * commands cannot leave a digest sitting in the part. */
tfw_status_t tfw_atecc_sleep(void);

/*
 * Runs one command and returns its response body.
 *
 * `rlen` is the exact number of body bytes expected, because the element's
 * response length is a property of the command and a short read is a protocol
 * error rather than a partial result. An error status from the element -- which
 * arrives as a four-byte response -- is translated and returned, never handed
 * back as data.
 *
 * `exec_ms` is the command's maximum execution time from the datasheet. The
 * element NACKs while it is busy, so this bounds the polling rather than
 * timing it: a command that finishes early is read early.
 */
tfw_status_t tfw_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms);

/*
 * Verify, External Public Key mode -- issue #4. A command of its own rather
 * than a call to tfw_atecc_exec: its 128-byte payload (a 64-byte signature
 * plus a 64-byte public key) is nearly double tfw_atecc_exec's shared
 * TFW_ATECC_MAX_DATA buffer, and growing that buffer to fit would cost every
 * element-tier build the difference permanently, whether or not this command
 * is ever used. This one stages its own, larger command on the CALLER'S
 * stack instead -- see TFW_ATECC_MAX_DATA's comment.
 *
 * The digest must already be loaded into TempKey (a pass-through Nonce via
 * tfw_atecc_exec, exactly as tfw_crypto_sign does it) before this is called;
 * that step's 32-byte payload fits the shared buffer comfortably and is not
 * duplicated here.
 *
 * Returns TFW_OK for a verified signature, TFW_ERR_CRYPTO for a signature that
 * does not verify or an element that failed for any other reason -- the
 * caller's correct response to either is identical, so they are not
 * distinguished, exactly as tfw_atecc_exec's own callers already do not
 * distinguish "wrong" from "the part refused."
 */
tfw_status_t tfw_atecc_verify_external(const uint8_t sig[64], const uint8_t pubkey[64],
                                     uint16_t exec_ms);

/* The element's CRC-16: polynomial 0x8005, register initialised to zero, each
 * data byte consumed least-significant bit first, result emitted little-endian.
 * Exposed because the tests pin it against the datasheet's own example, and
 * because a CRC that is wrong in the same way on both sides is a CRC that
 * checks nothing. */
uint16_t    tfw_atecc_crc16(const uint8_t *data, uint8_t len);

#endif /* TFW_ATECC_H */
