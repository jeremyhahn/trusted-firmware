/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Provisioning on the secure element -- the backend under the wire handlers
 * of provision.c for a board whose identity lives in an ATECC608A/C. Linked
 * only beside provision.c, so everything that file says about a production
 * image applies here.
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
 *
 * THE PART ON THE BENCH IS ALREADY CONFIGURED. The element tier targets the
 * ATECC608C (MIKROE-6774 Click board), whose TrustFLEX part arrives with its
 * config zone locked and its data zone locked (DS40002515B section 3.1). On it
 * PROV_WRITE_CONFIG, PROV_LOCK, PROV_GENKEY and PROV_SEED_UDS come back with
 * the element's execution error, as they would on any locked part; PROV_INFO
 * and PROV_PUBKEY are what apply. What provisioning means for a pre-configured
 * part is open -- docs/device-provisioning/engineering.md.
 */

#include <string.h>
#include "provision.h"
#include "tfw/config.h"
#include "../crypto/atecc608a/atecc.h"

/* The config zone is 128 bytes, written 32 at a time. Word address units are
 * 4-byte words, and a 32-byte write is 8 words -- so block n starts at word
 * 8n, which is what the element's address encoding expects. */
#define CONFIG_ZONE_WORDS 32u
#define BLOCK_WORDS        8u

tfw_status_t tfw_prov_info(uint8_t rev[4], uint8_t cfg[32], uint8_t lock[2])
{
    tfw_status_t st;

    if (tfw_atecc_wake() != TFW_OK) {
        /* Not "no element": we cannot tell an absent part from one whose bus is
         * miswired or running too fast for the wake pulse. Saying TFW_ERR_NACK
         * points at the bus, which is where the fault usually is. */
        return TFW_ERR_NACK;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                       0, 0u, rev, 4u, TFW_ATECC_T_INFO);
    if (st == TFW_OK) {
        /* Config block 0 carries the serial number, in bytes 0-3 and 8-12. */
        st = tfw_atecc_exec(TFW_ATECC_OP_READ,
                           (uint8_t)(TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32),
                           0u, 0, 0u, cfg, 32u, TFW_ATECC_T_READ);
    }
    if (st == TFW_OK) {
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
                           (uint16_t)(2u * 8u), 0, 0u, blk2, 32u, TFW_ATECC_T_READ);
        if (st == TFW_OK) {
            lock[0] = blk2[86u - 64u];   /* LockValue:  the data zone */
            lock[1] = blk2[87u - 64u];   /* LockConfig: the config zone */
        }
    }

    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_write_config(uint8_t block, const uint8_t data[32])
{
    tfw_status_t st;

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
                       (uint16_t)(block * BLOCK_WORDS), data, 32u, 0, 0u, TFW_ATECC_T_WRITE);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_lock(uint8_t zone, uint16_t crc)
{
    tfw_status_t st;

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
    st = tfw_atecc_exec(TFW_ATECC_OP_LOCK, zone, crc, 0, 0u, 0, 0u, TFW_ATECC_T_LOCK);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_genkey(uint8_t slot, uint8_t pub[64])
{
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
                       slot, 0, 0u, pub, 64u, TFW_ATECC_T_GENKEY);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_pubkey(uint8_t slot, uint8_t pub[64])
{
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
                       slot, 0, 0u, pub, 64u, TFW_ATECC_T_GENKEY);
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_prov_seed_uds(uint8_t slot)
{
    uint8_t     rnd[32];
    tfw_status_t st;

    if (slot != TFW_ATECC_SLOT_UDS) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    /*
     * The UDS comes from the element's own TRNG and goes into a slot configured
     * never-readable. It never crosses the serial link: the host never holds
     * it, so a compromised provisioning station learns nothing it could use.
     *
     * It DOES cross the I2C bus, three times -- Random returns it to this
     * MCU, Nonce sends it back to set TempKey, and the Write carries it in the
     * clear -- and it sits in SRAM until the scrub below. An earlier comment
     * here said otherwise. That exposure is the jig's, on a bench, before the
     * part leaves it; an encrypted Write would close it and needs an IO
     * protection key this flow does not have.
     *
     * On the MIKROE-6774's TrustFLEX ATECC608C this command cannot succeed:
     * its data zone ships locked and slot 1 holds an internal-sign ECC key
     * (DS40002515B sections 3.1 and 3.2.3), so the Write is refused and the
     * element's execution error comes back.
     *
     * This is the difference between "the device has a secret" and "the device
     * has a secret somebody typed in".
     */
    st = tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, rnd, 32u, TFW_ATECC_T_RANDOM);
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                           rnd, 32u, 0, 0u, TFW_ATECC_T_NONCE);
    }
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_WRITE,
                           (uint8_t)(TFW_ATECC_ZONE_DATA | TFW_ATECC_ZONE_32),
                           (uint16_t)((uint16_t)slot << 3), rnd, 32u, 0, 0u, TFW_ATECC_T_WRITE);
    }
    /* The copy in SRAM is scrubbed whether or not the write succeeded. A
     * failed provisioning attempt must not leave the secret in a buffer for
     * the next command to reuse. */
    memset(rnd, 0, sizeof rnd);
    (void)tfw_atecc_sleep();
    return st;
}

/* The data slots PROV_WRITE_PUBKEY writes: the image-signing key's slot as
 * tfw-boot reads it today, and slot 15, the TFLXTLS's secure-boot public key,
 * where the slot-map ADR's option (a) moves it. That ADR is PROPOSED, so the
 * set is one table and ratifying a slot changes one line. */
static const uint8_t kPubkeySlots[] = { TFW_ATECC_SLOT_IMAGE, 15u };

/* A stored P-256 public key is 72 bytes, pad4|X|pad4|Y, the form Verify's
 * stored mode reads. It is read back as two 32-byte blocks and then two 4-byte
 * words, because the slot's third block holds only eight bytes. */
#define PUBKEY_STORED_LEN 72u

/* Data-zone address of word `word` of a slot: block in bits 8+, slot in bits
 * 3-6, word within the block in bits 0-2. */
static tfw_status_t pubkey_read(uint8_t slot, uint8_t stored[PUBKEY_STORED_LEN])
{
    tfw_status_t st  = TFW_OK;
    uint8_t      off = 0u;

    while (st == TFW_OK && off < PUBKEY_STORED_LEN) {
        uint8_t  len  = (off < 64u) ? 32u : 4u;

        st = tfw_atecc_data_read(slot, (uint8_t)(off / 4u), &stored[off], len);
        off = (uint8_t)(off + len);
    }
    return st;
}

tfw_status_t tfw_prov_write_pubkey(uint8_t slot, uint8_t half, const uint8_t coord[32],
                                   uint8_t back[64])
{
    uint8_t      blk2[32];
    uint8_t      stored[PUBKEY_STORED_LEN];
    uint8_t      i;
    tfw_status_t st;

    for (i = 0u; i < sizeof kPubkeySlots && kPubkeySlots[i] != slot; i++) {
    }
    if (i == sizeof kPubkeySlots || half > 1u) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_NACK;
    }
    /* Config block 2 holds LockConfig (byte 87) and SlotLocked (88-89). The
     * element takes no data-zone write before its config is locked, and none
     * into a slot whose SlotLocked bit is clear; asking first gives the host a
     * status that names the cause instead of the element's execution error. */
    st = tfw_atecc_exec(TFW_ATECC_OP_READ,
                       (uint8_t)(TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32),
                       (uint16_t)(2u * BLOCK_WORDS), 0, 0u, blk2, 32u, TFW_ATECC_T_READ);
    if (st == TFW_OK && blk2[87u - 64u] == 0x55u) {
        st = TFW_ERR_BAD_STATE;
    }
    if (st == TFW_OK) {
        uint32_t writable = (uint32_t)blk2[88u - 64u] | ((uint32_t)blk2[89u - 64u] << 8);

        if (((writable >> slot) & 1u) == 0u) {
            st = TFW_ERR_PROTECTED_REGION;
        }
    }
    /* Half 0 is pad4|X, words 0-8 of the stored form; half 1 is pad4|Y, words
     * 9-17. A half straddles a block, so it goes as nine 4-byte words. */
    memset(stored, 0, 4u);
    memcpy(&stored[4], coord, 32u);
    for (i = 0u; st == TFW_OK && i < 9u; i++) {
        st = tfw_atecc_data_write(slot, (uint8_t)(half * 9u + i), &stored[i * 4u], 4u);
    }
    if (st == TFW_OK) {
        st = pubkey_read(slot, stored);
    }
    if (st == TFW_OK) {
        memcpy(back, &stored[4], 32u);
        memcpy(&back[32], &stored[40], 32u);
    }
    (void)tfw_atecc_sleep();
    return st;
}
