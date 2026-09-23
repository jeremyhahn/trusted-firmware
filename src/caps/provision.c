/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Provisioning -- the commands that write a device's identity.
 *
 * THIS FILE IS NOT IN A PRODUCTION IMAGE. AG_FEATURE_PROVISION defaults to 0,
 * the Makefile does not compile this translation unit, commands.def emits no
 * dispatch entries, and a fielded device answers AG_ERR_UNKNOWN_CMD to every id
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
 *  - It does not generate the UDS on the host. AG_CMD_PROV_SEED_UDS takes no
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
#include "ag/protocol.h"
#include "ag/config.h"
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

ag_status_t ag_prov_cmd_info(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t     rev[4];
    uint8_t     cfg[32];
    ag_status_t st;

    (void)r;
    if (ag_atecc_wake() != AG_OK) {
        /* Not "no element": we cannot tell an absent part from one whose bus is
         * miswired or running too fast for the wake pulse. Saying AG_ERR_NACK
         * points at the bus, which is where the fault usually is. */
        return AG_ERR_NACK;
    }
    st = ag_atecc_exec(AG_ATECC_OP_INFO, AG_ATECC_INFO_REVISION, 0u,
                       0, 0u, rev, 4u, T_INFO);
    if (st == AG_OK) {
        /* Config block 0 carries the serial number in bytes 0-3 and 8-12, and
         * the two lock bytes at 86 and 87 -- which are in block 2. Read block 0
         * for the serial and block 2 for the lock state; the host reassembles,
         * because the split is the element's and not ours to hide. */
        st = ag_atecc_exec(AG_ATECC_OP_READ,
                           (uint8_t)(AG_ATECC_ZONE_CONFIG | AG_ATECC_ZONE_32),
                           0u, 0, 0u, cfg, 32u, T_READ);
    }
    if (st == AG_OK) {
        ag_write_blob(w, rev, 4u);
        ag_write_blob(w, cfg, 32u);
    }
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_prov_cmd_write_config(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t        block = ag_read_u8(r);
    const uint8_t *data  = ag_read_blob(r, 32u);
    ag_status_t    st;

    (void)w;
    if (!data) {
        return AG_ERR_BAD_ARGC;
    }
    if (block >= CONFIG_ZONE_WORDS / BLOCK_WORDS) {
        return AG_ERR_RANGE;
    }
    /* Blocks 0 and 1 hold the serial number, the revision and the OTP mode --
     * the element refuses writes to most of it, and the parts it does accept
     * are not ours to set. Refusing here gives a clearer answer than the
     * element's execution error, at the point where a template is being
     * debugged and the difference matters. */
    if (block < 2u) {
        return AG_ERR_PROTECTED_REGION;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_NACK;
    }
    st = ag_atecc_exec(AG_ATECC_OP_WRITE,
                       (uint8_t)(AG_ATECC_ZONE_CONFIG | AG_ATECC_ZONE_32),
                       (uint16_t)(block * BLOCK_WORDS), data, 32u, 0, 0u, T_WRITE);
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_prov_cmd_lock(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t     zone = ag_read_u8(r);
    uint16_t    crc  = ag_read_u16(r);
    ag_status_t st;

    (void)w;
    if (zone != AG_ATECC_LOCK_CONFIG && zone != AG_ATECC_LOCK_DATA) {
        return AG_ERR_RANGE;
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
        return AG_ERR_BAD_ARGC;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_NACK;
    }
    st = ag_atecc_exec(AG_ATECC_OP_LOCK, zone, crc, 0, 0u, 0, 0u, T_LOCK);
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_prov_cmd_genkey(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t     slot = ag_read_u8(r);
    uint8_t     pub[64];
    ag_status_t st;

    if (slot != AG_ATECC_SLOT_DEVICEID) {
        /* One slot holds the DeviceID key. Generating into another would
         * produce a key nothing in this firmware will ever sign with, which is
         * a silent way to appear provisioned and not be. */
        return AG_ERR_RANGE;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_NACK;
    }
    /* Private mode. The private half is created inside the element and never
     * exists anywhere else; what comes back is the public point. */
    st = ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PRIVATE,
                       slot, 0, 0u, pub, 64u, T_GENKEY);
    if (st == AG_OK) {
        ag_write_blob(w, pub, 64u);
    }
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_prov_cmd_pubkey(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t     slot = ag_read_u8(r);
    uint8_t     pub[64];
    ag_status_t st;

    if (slot != AG_ATECC_SLOT_DEVICEID) {
        return AG_ERR_RANGE;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_NACK;
    }
    /* Public mode recomputes the point from the private key already in the
     * slot. It does NOT generate a new one -- that is PROV_GENKEY, and keeping
     * them separate is why `agprov --verify` can read a device back without any
     * chance of replacing the identity it is verifying. */
    st = ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PUBLIC,
                       slot, 0, 0u, pub, 64u, T_GENKEY);
    if (st == AG_OK) {
        ag_write_blob(w, pub, 64u);
    }
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_prov_cmd_seed_uds(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t     slot = ag_read_u8(r);
    uint8_t     rnd[32];
    ag_status_t st;

    (void)w;
    if (slot != AG_ATECC_SLOT_UDS) {
        return AG_ERR_RANGE;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_NACK;
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
    st = ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, rnd, 32u, T_RANDOM);
    if (st == AG_OK) {
        st = ag_atecc_exec(AG_ATECC_OP_NONCE, AG_ATECC_NONCE_PASSTHRU, 0u,
                           rnd, 32u, 0, 0u, T_NONCE);
    }
    if (st == AG_OK) {
        st = ag_atecc_exec(AG_ATECC_OP_WRITE,
                           (uint8_t)(AG_ATECC_ZONE_DATA | AG_ATECC_ZONE_32),
                           (uint16_t)((uint16_t)slot << 3), rnd, 32u, 0, 0u, T_WRITE);
    }
    /* The copy in SRAM is scrubbed whether or not the write succeeded. A
     * failed provisioning attempt must not leave the secret in a buffer for
     * the next command to reuse. */
    memset(rnd, 0, sizeof rnd);
    (void)ag_atecc_sleep();
    return st;
}
