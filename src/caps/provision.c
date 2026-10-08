/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Provisioning -- the commands that write a device's identity.
 *
 * THIS FILE IS NOT IN A PRODUCTION IMAGE. TFW_FEATURE_PROVISION defaults to 0,
 * the Makefile does not compile this translation unit, commands.def emits no
 * dispatch entries, and a fielded device answers TFW_ERR_UNKNOWN_CMD to every id
 * below. The gate is file selection, not a runtime check, because a runtime
 * check is a thing an attacker argues with.
 *
 * That is the security property, and it is worth stating plainly: a device that
 * can be re-provisioned over its own command channel has no identity worth
 * attesting to. Whoever reaches the serial port would become the manufacturer.
 * So provisioning happens on a jig, with an image that is replaced by the
 * production image before the part leaves the bench.
 *
 * WHAT THIS DOES NOT DO, and must not:
 *
 *  - It does not generate the UDS on the host. TFW_CMD_PROV_SEED_UDS takes no
 *    key material; the element fills its own slot from its own TRNG. A UDS the
 *    host generated is a UDS the host has seen, and a provisioning station is
 *    exactly the machine an attacker would target to get it.
 *  - It does not read any secret back. PROV_PUBKEY returns a public point.
 *    There is no command here that can return the contents of a never-readable
 *    slot, because the element would refuse and offering the command anyway
 *    would suggest otherwise.
 *  - It does not decide policy. The slot configuration is a 32-byte block the
 *    host supplies from a template; this file writes what it is given and
 *    reports what the element said. The template is reviewable; a policy
 *    encoded here would not be.
 */

#include <string.h>
#include "provision.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "../crypto/atecc608a/atecc.h"

/* Datasheet maxima, in milliseconds. */
#define T_INFO      5u
#define T_READ      1u
#define T_WRITE    26u
#define T_LOCK     15u
#define T_GENKEY  115u
#define T_RANDOM   23u
#define T_NONCE     7u

/* The config zone is 128 bytes, written 32 at a time. Word address units are
 * 4-byte words, and a 32-byte write is 8 words -- so block n starts at word
 * 8n, which is what the element's address encoding expects. */
#define CONFIG_ZONE_WORDS 32u
#define BLOCK_WORDS        8u

tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t     rev[4];
    uint8_t     cfg[32];
    tfw_status_t st;

    (void)r;
    if (tfw_atecc_wake() != TFW_OK) {
        /* Not "no element": we cannot tell an absent part from one whose bus is
         * miswired or running too fast for the wake pulse. Saying TFW_ERR_NACK
         * points at the bus, which is where the fault usually is. */
        return TFW_ERR_NACK;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                       0, 0u, rev, 4u, T_INFO);
    if (st == TFW_OK) {
        /* Config block 0 carries the serial number, in bytes 0-3 and 8-12. */
        st = tfw_atecc_exec(TFW_ATECC_OP_READ,
                           (uint8_t)(TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32),
                           0u, 0, 0u, cfg, 32u, T_READ);
    }
    if (st == TFW_OK) {
        tfw_write_blob(w, rev, 4u);
        tfw_write_blob(w, cfg, 32u);

        /*
         * THE LOCK BYTES ARE IN BLOCK 2, NOT BLOCK 0, and this read is the
         * reason the reply is 38 bytes rather than 36.
         *
         * The element puts LockValue at config byte 86 and LockConfig at 87,
         * which is block 2 offsets 22 and 23. An earlier version of this
         * function described reading block 2 in its comment and then did not do
         * it, and the host parser indexed byte 90 of a 36-byte reply -- so
         * every legal answer panicked the Go SDK, and nothing caught it because
         * neither side had a test.
         *
         * Only the two bytes are forwarded, not the whole block. Block 2 also
         * holds slot access policies, and a host that received them would have
         * a second copy of the provisioning template to disagree with the real
         * one; `tfwprov --verify` compares against the template, not against
         * what the device thinks the template was.
         */
        uint8_t blk2[32];

        st = tfw_atecc_exec(TFW_ATECC_OP_READ,
                           (uint8_t)(TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32),
                           (uint16_t)(2u * 8u), 0, 0u, blk2, 32u, T_READ);
        if (st == TFW_OK) {
            tfw_write_u8(w, blk2[86u - 64u]);   /* LockValue:  the data zone */
            tfw_write_u8(w, blk2[87u - 64u]);   /* LockConfig: the config zone */
        }
    }

    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t        block = tfw_read_u8(r);
    const uint8_t *data  = tfw_read_blob(r, 32u);
    tfw_status_t    st;

    (void)w;
    if (!data) {
        return TFW_ERR_BAD_ARGC;
    }
    if (block >= CONFIG_ZONE_WORDS / BLOCK_WORDS) {
        return TFW_ERR_RANGE;
    }
    /* Blocks 0 and 1 hold the serial number, the revision and the OTP mode --
     * the element refuses writes to most of it, and the parts it does accept
     * are not ours to set. Refusing here gives a clearer answer than the
     * element's execution error, at the point where a template is being
     * debugged and the difference matters. */
    if (block < 2u) {
        return TFW_ERR_PROTECTED_REGION;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_WRITE,
                       (uint8_t)(TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32),
                       (uint16_t)(block * BLOCK_WORDS), data, 32u, 0, 0u, T_WRITE);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t     zone = tfw_read_u8(r);
    uint16_t    crc  = tfw_read_u16(r);
    tfw_status_t st;

    (void)w;
    if (zone != TFW_ATECC_LOCK_CONFIG && zone != TFW_ATECC_LOCK_DATA) {
        return TFW_ERR_RANGE;
    }
    /*
     * THE SUMMARY CRC IS REQUIRED, and this is the one place in the firmware
     * where refusing a convenience is the whole feature.
     *
     * The element accepts LOCK_ZONE_NO_CRC, which locks without checking that
     * the zone contains what the caller thinks it does. That turns a
     * transcription error in a template into a permanently wrong part, with no
     * erase and no reset. Requiring the CRC means the element itself verifies
     * the zone's contents against the host's expectation before the
     * irreversible step, so the two have to agree.
     */
    if (crc == 0u) {
        return TFW_ERR_BAD_ARGC;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_LOCK, zone, crc, 0, 0u, 0, 0u, T_LOCK);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t     slot = tfw_read_u8(r);
    uint8_t     pub[64];
    tfw_status_t st;

    if (slot != TFW_ATECC_SLOT_DEVICEID) {
        /* One slot holds the DeviceID key. Generating into another would
         * produce a key nothing in this firmware will ever sign with, which is
         * a silent way to appear provisioned and not be. */
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    /* Private mode. The private half is created inside the element and never
     * exists anywhere else; what comes back is the public point. */
    st = tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PRIVATE,
                       slot, 0, 0u, pub, 64u, T_GENKEY);
    if (st == TFW_OK) {
        tfw_write_blob(w, pub, 64u);
    }
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t     slot = tfw_read_u8(r);
    uint8_t     pub[64];
    tfw_status_t st;

    if (slot != TFW_ATECC_SLOT_DEVICEID) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    /* Public mode recomputes the point from the private key already in the
     * slot. It does NOT generate a new one -- that is PROV_GENKEY, and keeping
     * them separate is why `tfwprov --verify` can read a device back without any
     * chance of replacing the identity it is verifying. */
    st = tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PUBLIC,
                       slot, 0, 0u, pub, 64u, T_GENKEY);
    if (st == TFW_OK) {
        tfw_write_blob(w, pub, 64u);
    }
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t     slot = tfw_read_u8(r);
    uint8_t     rnd[32];
    tfw_status_t st;

    (void)w;
    if (slot != TFW_ATECC_SLOT_UDS) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    /*
     * The UDS comes from the element's own TRNG and goes into a slot configured
     * never-readable. It crosses the element's internal bus and not the serial
     * link -- Random into TempKey, then Write from TempKey into the slot -- so
     * the host never holds it and a compromised provisioning station learns
     * nothing it could use.
     *
     * This is the difference between "the device has a secret" and "the device
     * has a secret somebody typed in".
     */
    st = tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, rnd, 32u, T_RANDOM);
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                           rnd, 32u, 0, 0u, T_NONCE);
    }
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_WRITE,
                           (uint8_t)(TFW_ATECC_ZONE_DATA | TFW_ATECC_ZONE_32),
                           (uint16_t)((uint16_t)slot << 3), rnd, 32u, 0, 0u, T_WRITE);
    }
    /* The copy in SRAM is scrubbed whether or not the write succeeded. A
     * failed provisioning attempt must not leave the secret in a buffer for
     * the next command to reuse. */
    memset(rnd, 0, sizeof rnd);
    (void)tfw_atecc_sleep();
    return st;
}
