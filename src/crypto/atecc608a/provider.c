/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DICE-HW tier: an ATECC608A holds the root of trust.
 *
 * This is the profile the plan calls DICE-HW, and the reason it exists is that
 * the element supplies the two things a bare ATmega cannot: a UDS in a slot
 * configured never-readable, and a real TRNG.
 *
 * THE SHAPE OF IT, and why it is not the obvious shape.
 *
 * The obvious design would derive the signing key from the CDI, as DICE
 * describes. The ATECC608A cannot do that -- there is no command that turns an
 * arbitrary 32-byte secret into a P-256 private key inside the part, and a key
 * derived outside it would be a key that left it, which is the one thing this
 * tier exists to prevent.
 *
 * DICE's own layering routes around this, and we follow it rather than invent
 * something:
 *
 *   "The DeviceID key ... is used to sign one or more certificates for keys
 *    generated on behalf of a higher TCB layer. Therefore, the DeviceID key is
 *    an ECA Key."  -- DICE Layering Architecture, 8.1.1.5
 *
 * So the DeviceID key is an element-resident ECC key that never leaves slot 0
 * and acts as the ECA. The measurement binding lives where DICE puts it, in the
 * CDI -- computed as HMAC(UDS, H(firmware)) by the element's own SHA/HMAC
 * engine, so the UDS never crosses the bus. Form (2) of DICE HW Requirements
 * r78 section 5, which TCG calls the stronger of the two.
 *
 * WHAT THIS TIER STILL DOES NOT MEET, stated because a conformance claim that
 * hides its gaps is worse than no claim:
 *
 *  - The CDI transits SRAM on its way from the element to the Alias derivation.
 *    No classic AVR has an MPU, so "first mutable code has exclusive access"
 *    (6.4) is partial. It is also NOT zeroized: cdi[] below is resident for the
 *    lifetime of the image, because every derivation re-reads it. The bootloader
 *    zeroizes its own copy (boot/boot_main.c); this one is a different buffer
 *    and it stays. Issue #7 tracks whether that can be narrowed here.
 *  - Immutability of the DICE itself (6.2.1) rests on BLB mode 3 and lock bits,
 *    which are procedural rather than architectural.
 *
 * The honest phrase is "DICE-based device identity with a hardware root of
 * trust", never "TCG DICE compliant", and DiceTcbInfo.OperationalFlags carries
 * notSecure accordingly on tiers below this one.
 */

#include <string.h>
#include "atecc.h"
#include "ag/crypto.h"
#include "ag/config.h"
#include "ag/protocol.h"
#include "ag/hal.h"
#include "../sha256.h"

/* Datasheet maximum execution times, in milliseconds. They bound the poll in
 * ag_atecc_exec; a number that is too small gives up on a part that was about
 * to answer, which presents as intermittent hardware. */
#define T_INFO     5u
#define T_RANDOM  23u
#define T_NONCE    7u
#define T_SHA     36u
#define T_SIGN    64u
#define T_GENKEY 115u
#define T_COUNTER  6u
#define T_READ     1u

static uint8_t present;        /* the element answered a wake and an Info */
static uint8_t have_cdi;
static uint8_t cdi[32];

ag_status_t ag_crypto_init(void)
{
    uint8_t     rev[4];
    uint8_t     measurement[32];
    ag_status_t st;

    present  = 0u;
    have_cdi = 0u;

    /* 100 kHz, not 400. The wake pulse is produced by the low period of a byte
     * written to address zero, and at 400 kHz that period is 22 us -- below the
     * 60 us the part requires. The symptom of getting this wrong is an element
     * that appears absent, which sends you looking at the wiring.
     *
     * ag_atecc_wake() reasserts AG_ATECC_I2C_HZ itself before every wake, so
     * this call is not the only thing standing between the element and a
     * host's later I2C_CONFIG -- it exists here so the bus is already at a
     * known rate for the presence probe below, before anything has decided
     * whether an element is even on it. */
    if (ag_hal_i2c_init(AG_ATECC_I2C_HZ) != AG_OK) {
        return AG_OK;          /* no bus: not an error, just no element */
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_OK;          /* no element on this board */
    }
    if (ag_atecc_exec(AG_ATECC_OP_INFO, AG_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, T_INFO) != AG_OK) {
        (void)ag_atecc_sleep();
        return AG_OK;
    }
    present = 1u;

    /*
     * CDI = HMAC(UDS, H(firmware)), computed inside the element.
     *
     * The measurement is a placeholder until the trusted bootloader exists to
     * supply a real one, and it is deliberately not a hash of nothing -- that
     * would yield a CDI which looks derived and is not. The same string the
     * software tier uses, so the two tiers' placeholder measurements are
     * visibly the same placeholder rather than accidentally different.
     */
    ag_sha256((const uint8_t *)"ag-unmeasured-application-v1", 28u, measurement);
    st = ag_crypto_hmac(AG_ATECC_SLOT_UDS, measurement, sizeof measurement, cdi);
    (void)ag_atecc_sleep();
    if (st == AG_OK) {
        have_cdi = 1u;
    }
    /* An element that is present but has no UDS yet is an unprovisioned device.
     * It reports its tier truthfully and refuses to sign; it does not pretend
     * to be the software tier, because the host's decision about what to trust
     * depends on knowing an element is there and empty. */
    return AG_OK;
}

ag_crypto_tier_t ag_crypto_tier(void)
{
    return present ? AG_CRYPTO_SECURE_ELEMENT : AG_CRYPTO_NONE;
}

uint16_t ag_crypto_algorithms(void)
{
    if (!present) {
        return 0u;
    }
    /* ECDSA P-256 and HMAC-SHA256. NOT Ed25519: this part has no hardware
     * Ed25519, and advertising an algorithm the backend cannot perform is the
     * exact defect go-xkms's BuildAlgorithmsResponse has -- four backends
     * claiming Ed25519 they do not have. We report what the silicon does. */
    return (uint16_t)(AG_ALGBIT_ECDSA_P256_SHA256 | AG_ALGBIT_HMAC_SHA256);
}

ag_status_t ag_crypto_random(uint8_t *out, uint8_t len)
{
    uint8_t     rnd[32];
    ag_status_t st;

    if (!present) {
        return AG_ERR_CRYPTO;
    }
    if (len == 0u || len > sizeof rnd) {
        return AG_ERR_RANGE;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }
    st = ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, rnd, 32u, T_RANDOM);
    (void)ag_atecc_sleep();
    if (st != AG_OK) {
        return st;
    }
    memcpy(out, rnd, len);
    return AG_OK;
}

ag_status_t ag_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    /* Deliberately software, even with the element present. The element's SHA
     * command costs a bus round trip per 64-byte block, and this hash is over
     * public data with no secret in it -- there is nothing for the hardware to
     * protect and a great deal of latency to pay. The element is used where it
     * holds a key, which is ag_crypto_hmac and ag_crypto_sign. */
    ag_sha256(data, len, out);
    return AG_OK;
}

ag_status_t ag_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                           uint8_t out[32])
{
    ag_status_t st;
    uint16_t    off = 0u;

    if (!present) {
        return AG_ERR_CRYPTO;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }

    /* Start, then whole 64-byte blocks, then End with the remainder. The key
     * is named by slot and never appears on the bus in any of the three. */
    st = ag_atecc_exec(AG_ATECC_OP_SHA, AG_ATECC_SHA_HMAC_START, slot,
                       0, 0u, 0, 0u, T_SHA);
    while (st == AG_OK && (uint16_t)(len - off) > 64u) {
        st = ag_atecc_exec(AG_ATECC_OP_SHA, AG_ATECC_SHA_HMAC_UPDATE, 64u,
                           &data[off], 64u, 0, 0u, T_SHA);
        off = (uint16_t)(off + 64u);
    }
    if (st == AG_OK) {
        uint8_t tail = (uint8_t)(len - off);
        st = ag_atecc_exec(AG_ATECC_OP_SHA, AG_ATECC_SHA_HMAC_END, tail,
                           tail ? &data[off] : 0, tail, out, 32u, T_SHA);
    }
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_crypto_sign(const uint8_t digest[32], uint8_t sig[64],
                           uint8_t *alg_id)
{
    ag_status_t st;

    if (!present) {
        return AG_ERR_CRYPTO;
    }
    if (!have_cdi) {
        /* An element with no UDS has no identity to sign with, whatever keys
         * happen to be in it. Refusing here rather than signing with slot 0
         * anyway is what keeps "this device attested" from meaning "this device
         * had a key". */
        return AG_ERR_NOT_PROVISIONED;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }

    /* Nonce in pass-through mode loads TempKey with the digest verbatim -- no
     * seed update, no element randomness mixed in. That is what makes the
     * signature one over OUR digest rather than over something derived from it,
     * which is the only form a verifier holding the same digest can check. */
    st = ag_atecc_exec(AG_ATECC_OP_NONCE, AG_ATECC_NONCE_PASSTHRU, 0u,
                       digest, 32u, 0, 0u, T_NONCE);
    if (st == AG_OK) {
        st = ag_atecc_exec(AG_ATECC_OP_SIGN, AG_ATECC_SIGN_EXTERNAL,
                           AG_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, T_SIGN);
    }
    (void)ag_atecc_sleep();
    if (st == AG_OK) {
        *alg_id = AG_ALG_ECDSA_P256_SHA256;
    }
    return st;
}

ag_status_t ag_crypto_pubkey(uint8_t out[64])
{
    ag_status_t st;

    if (!present) {
        return AG_ERR_CRYPTO;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }
    /* GenKey in public mode recomputes the public point from the private key
     * already in the slot. It does NOT generate a new key -- that is
     * GENKEY_PRIVATE, and it is reachable only from provisioning, because
     * running it here would silently replace the device's identity. */
    st = ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PUBLIC,
                       AG_ATECC_SLOT_DEVICEID, 0, 0u, out, 64u, T_GENKEY);
    (void)ag_atecc_sleep();
    return st == AG_OK ? AG_OK : AG_ERR_NOT_PROVISIONED;
}

ag_status_t ag_crypto_counter(uint8_t idx, uint32_t *out)
{
    uint8_t     v[4];
    ag_status_t st;

    if (!present) {
        return AG_ERR_CRYPTO;
    }
    if (idx > 1u) {
        return AG_ERR_RANGE;       /* the part has two */
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }
    st = ag_atecc_exec(AG_ATECC_OP_COUNTER, AG_ATECC_COUNTER_READ, idx,
                       0, 0u, v, 4u, T_COUNTER);
    (void)ag_atecc_sleep();
    if (st != AG_OK) {
        return st;
    }
    *out = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    return AG_OK;
}
