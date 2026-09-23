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
#ifndef AG_ATECC_H
#define AG_ATECC_H

#include <stdint.h>
#include "ag/status.h"

/* The element's I2C address. Blacklisted from the generic I2C_XFER command --
 * see src/caps/bus.c -- because a host that can talk to the element directly
 * can ask it to sign things this firmware never agreed to. */
#define AG_ATECC_ADDR   0x60u

/* Word addresses. The first byte of every I2C write selects what follows. */
#define AG_ATECC_WA_RESET   0x00u
#define AG_ATECC_WA_SLEEP   0x01u
#define AG_ATECC_WA_IDLE    0x02u
#define AG_ATECC_WA_COMMAND 0x03u

/* Opcodes. */
#define AG_ATECC_OP_READ      0x02u
#define AG_ATECC_OP_NONCE     0x16u
#define AG_ATECC_OP_LOCK      0x17u
#define AG_ATECC_OP_RANDOM    0x1Bu
#define AG_ATECC_OP_COUNTER   0x24u
#define AG_ATECC_OP_INFO      0x30u
#define AG_ATECC_OP_GENKEY    0x40u
#define AG_ATECC_OP_SIGN      0x41u
#define AG_ATECC_OP_PRIVWRITE 0x46u
#define AG_ATECC_OP_SHA       0x47u
#define AG_ATECC_OP_WRITE     0x12u

/* Modes, for the commands this firmware issues. */
#define AG_ATECC_SHA_HMAC_START  0x04u
#define AG_ATECC_SHA_HMAC_UPDATE 0x01u
#define AG_ATECC_SHA_HMAC_END    0x05u
#define AG_ATECC_NONCE_PASSTHRU  0x03u
#define AG_ATECC_SIGN_EXTERNAL   0x80u
#define AG_ATECC_GENKEY_PRIVATE  0x04u
#define AG_ATECC_GENKEY_PUBLIC   0x00u
#define AG_ATECC_COUNTER_READ    0x00u
#define AG_ATECC_COUNTER_INCR    0x01u
#define AG_ATECC_INFO_REVISION   0x00u
#define AG_ATECC_LOCK_CONFIG     0x00u
#define AG_ATECC_LOCK_DATA       0x01u
#define AG_ATECC_LOCK_NO_CRC     0x80u
#define AG_ATECC_ZONE_CONFIG     0x00u
#define AG_ATECC_ZONE_DATA       0x02u
#define AG_ATECC_ZONE_32         0x80u

/*
 * Slot assignment for the DICE-HW profile. These are decisions, and they are
 * here rather than scattered through provider.c because a provisioning template
 * has to agree with them exactly and a mismatch is not detectable at run time --
 * the element would simply refuse, or worse, succeed against the wrong key.
 */
#define AG_ATECC_SLOT_DEVICEID 0u  /* ECC private key, never readable, the DICE ECA key */
#define AG_ATECC_SLOT_UDS      1u  /* the UDS, as an HMAC key that never leaves the part */
#define AG_ATECC_COUNTER_BOOT  0u  /* monotonic boot count and the anti-rollback floor */

/* The largest command payload this firmware sends. PrivWrite carries 36 bytes
 * of key plus a 32-byte MAC, which is the worst case and is provisioning-only;
 * SHA HMAC_Update's 64-byte block is the worst case in ordinary operation. */
#define AG_ATECC_MAX_DATA 68u

/* Wakes the element and confirms it answered. Safe to call repeatedly. */
ag_status_t ag_atecc_wake(void);

/* Puts the element to sleep, zeroing its volatile state including TempKey.
 * Called after every operation that loaded a secret into it, so a reset between
 * commands cannot leave a digest sitting in the part. */
ag_status_t ag_atecc_sleep(void);

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
ag_status_t ag_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms);

/* The element's CRC-16: polynomial 0x8005, register initialised to zero, each
 * data byte consumed least-significant bit first, result emitted little-endian.
 * Exposed because the tests pin it against the datasheet's own example, and
 * because a CRC that is wrong in the same way on both sides is a CRC that
 * checks nothing. */
uint16_t    ag_atecc_crc16(const uint8_t *data, uint8_t len);

#endif /* AG_ATECC_H */
