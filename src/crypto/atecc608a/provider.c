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
 *  - The CDI transits SRAM on its way out of the element, so "first mutable
 *    code has exclusive access" (6.4) is partial on a part with no MPU. IT IS
 *    ZEROIZED IMMEDIATELY AFTER USE, though -- tfw_crypto_init below holds it in
 *    a stack buffer that exists only long enough to learn whether the element
 *    answered, and clears it before returning THROUGH tfw_secure_zero
 *    (src/core/secure_zero.c), not a plain memset: a local buffer's last
 *    write before it goes out of scope is a dead store an optimizer is free
 *    to remove at -Os, and this codebase's own build did exactly that until
 *    the primitive existed. Nothing on this tier signs with the CDI (the
 *    DeviceID key below is element-resident and never derived from it), so
 *    nothing needs the value again once that one bit is known. Checked, not
 *    assumed -- issue #7.
 *  - Immutability of the DICE itself (6.2.1) rests on BLB mode 3 and lock bits,
 *    which are procedural rather than architectural.
 *
 * The honest phrase is "DICE-based device identity with a hardware root of
 * trust", never "TCG DICE compliant", and DiceTcbInfo.OperationalFlags carries
 * notSecure accordingly on tiers below this one.
 */

#include <string.h>
#include "atecc.h"
#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/hal.h"
#include "../sha256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"

static uint8_t present;        /* the element answered a wake and an Info */
static uint8_t have_cdi;

tfw_status_t tfw_crypto_init(void)
{
    uint8_t     rev[4];
    uint8_t     measurement[32];
    uint8_t     cdi[32];
    tfw_status_t st;

    present  = 0u;
    have_cdi = 0u;

    /*
     * Drains and discards any CDI tfw-boot handed off, whether or not this
     * tier is even the one it named. This tier's DeviceID key is
     * element-resident and never CDI-derived (see the file header), so it
     * has no use for the value -- but "no use for it" is not the same as
     * "safe to leave resident," and src/core/handoff.c holds it until
     * something takes it. Every crypto provider's tfw_crypto_init is the one
     * call site per image; leaving this out would mean the application's
     * only other tier honours the handoff's "consumed once" contract and
     * this one quietly does not.
     *
     * CHECKED, NOT ASSUMED, for the soft tier's "valid handoff but no usable
     * CDI" misbinding (src/crypto/soft/provider.c): it does not reach here.
     * `have_cdi` below gates on the element's OWN on-chip HMAC probe against
     * its OWN slot-resident UDS -- entirely independent of whatever this
     * unconditional drain above did or did not find in the handoff. Whether
     * the handoff was absent, named a different tier, or named this one with
     * tfw-boot's all-zero "unprovisioned" sentinel, this tier's signing key
     * and its own provisioned state are unaffected either way, because
     * neither was ever derived from the handed-off CDI to begin with. The
     * placeholder measurement below exists for the same reason: this tier's
     * key is never measurement-bound (see the file header's DICE-layering
     * discussion), so there is no claimed-measurement-vs-signing-key
     * consistency for a mismatched or unusable handoff to break.
     */
    {
        uint8_t discard[32];
        if (tfw_boot_handoff_take_cdi(discard)) {
            tfw_secure_zero(discard, sizeof discard);
        }
    }

    /* 100 kHz, not 400. The wake pulse is produced by the low period of a byte
     * written to address zero, and at 400 kHz that period is 22 us -- below the
     * 60 us the part requires. The symptom of getting this wrong is an element
     * that appears absent, which sends you looking at the wiring.
     *
     * tfw_atecc_wake() reasserts TFW_ATECC_I2C_HZ itself before every wake, so
     * this call is not the only thing standing between the element and a
     * host's later I2C_CONFIG -- it exists here so the bus is already at a
     * known rate for the presence probe below, before anything has decided
     * whether an element is even on it. */
    if (tfw_hal_i2c_init(TFW_ATECC_I2C_HZ) != TFW_OK) {
        return TFW_OK;          /* no bus: not an error, just no element */
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_OK;          /* no element on this board */
    }
    if (tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, TFW_ATECC_T_INFO) != TFW_OK) {
        (void)tfw_atecc_sleep();
        return TFW_OK;
    }
    present = 1u;

    /*
     * CDI = HMAC(UDS, H(firmware)), computed inside the element.
     *
     * The measurement is a placeholder rather than the one tfw-boot verified:
     * the DeviceID key this tier signs with is element-resident and never
     * derived from the CDI (see the file header), so nothing downstream reads
     * this value for what it says about the firmware -- it exists only to
     * learn whether the element has a UDS at all, and a placeholder answers
     * that exactly as well as a real measurement would. The same string the
     * software tier uses for the same reason, so the two tiers' placeholders
     * are visibly the same placeholder rather than accidentally different.
     * FWHash, the measurement a verifier actually checks, comes from the
     * bootloader's handoff by way of src/caps/attest.c -- not from here.
     */
    tfw_sha256((const uint8_t *)"tfw-unmeasured-application-v1", 29u, measurement);
    st = tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, measurement, sizeof measurement, cdi);
    (void)tfw_atecc_sleep();
    /* Zeroized here, the moment the one bit it was for -- did the element
     * answer with a UDS in place -- is known. See the file header. */
    tfw_secure_zero(cdi, sizeof cdi);
    if (st == TFW_OK) {
        have_cdi = 1u;
    }
    /* An element that is present but has no UDS yet is an unprovisioned device.
     * It reports its tier truthfully and refuses to sign; it does not pretend
     * to be the software tier, because the host's decision about what to trust
     * depends on knowing an element is there and empty. */
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return present ? TFW_CRYPTO_SECURE_ELEMENT : TFW_CRYPTO_NONE;
}

uint16_t tfw_crypto_algorithms(void)
{
    if (!present) {
        return 0u;
    }
    /* ECDSA P-256 and HMAC-SHA256. NOT Ed25519: this part has no hardware
     * Ed25519, and advertising an algorithm the backend cannot perform is the
     * exact defect go-xkms's BuildAlgorithmsResponse has -- four backends
     * claiming Ed25519 they do not have. We report what the silicon does. */
    return (uint16_t)(TFW_ALGBIT_ECDSA_P256_SHA256 | TFW_ALGBIT_HMAC_SHA256);
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    uint8_t     rnd[32];
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (len == 0u || len > sizeof rnd) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, rnd, 32u, TFW_ATECC_T_RANDOM);
    (void)tfw_atecc_sleep();
    if (st != TFW_OK) {
        return st;
    }
    memcpy(out, rnd, len);
    return TFW_OK;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    /* Deliberately software, even with the element present. The element's SHA
     * command costs a bus round trip per 64-byte block, and this hash is over
     * public data with no secret in it -- there is nothing for the hardware to
     * protect and a great deal of latency to pay. The element is used where it
     * holds a key, which is tfw_crypto_hmac and tfw_crypto_sign. */
    tfw_sha256(data, len, out);
    return TFW_OK;
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                           uint8_t out[32])
{
    tfw_status_t st;
    uint16_t    off = 0u;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }

    /* Start, then whole 64-byte blocks, then End with the remainder. The key
     * is named by slot and never appears on the bus in any of the three. */
    st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_HMAC_START, slot,
                       0, 0u, 0, 0u, TFW_ATECC_T_SHA);
    while (st == TFW_OK && (uint16_t)(len - off) > 64u) {
        st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_HMAC_UPDATE, 64u,
                           &data[off], 64u, 0, 0u, TFW_ATECC_T_SHA);
        off = (uint16_t)(off + 64u);
    }
    if (st == TFW_OK) {
        uint8_t tail = (uint8_t)(len - off);
        st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_HMAC_END, tail,
                           tail ? &data[off] : 0, tail, out, 32u, TFW_ATECC_T_SHA);
    }
    (void)tfw_atecc_sleep();
    return st;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64],
                           uint8_t *alg_id)
{
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (!have_cdi) {
        /* An element with no UDS has no identity to sign with, whatever keys
         * happen to be in it. Refusing here rather than signing with slot 0
         * anyway is what keeps "this device attested" from meaning "this device
         * had a key". */
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }

    /* Nonce in pass-through mode loads TempKey with the digest verbatim -- no
     * seed update, no element randomness mixed in. That is what makes the
     * signature one over OUR digest rather than over something derived from it,
     * which is the only form a verifier holding the same digest can check. */
    st = tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                       digest, 32u, 0, 0u, TFW_ATECC_T_NONCE);
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_SIGN, TFW_ATECC_SIGN_EXTERNAL,
                           TFW_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, TFW_ATECC_T_SIGN);
    }
    (void)tfw_atecc_sleep();
    if (st == TFW_OK) {
        *alg_id = TFW_ALG_ECDSA_P256_SHA256;
    }
    return st;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    /* GenKey in public mode recomputes the public point from the private key
     * already in the slot. It does NOT generate a new key -- that is
     * GENKEY_PRIVATE, and it is reachable only from provisioning, because
     * running it here would silently replace the device's identity. */
    st = tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PUBLIC,
                       TFW_ATECC_SLOT_DEVICEID, 0, 0u, out, 64u, TFW_ATECC_T_GENKEY);
    (void)tfw_atecc_sleep();
    return st == TFW_OK ? TFW_OK : TFW_ERR_NOT_PROVISIONED;
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    uint8_t     v[4];
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (idx > 1u) {
        return TFW_ERR_RANGE;       /* the part has two */
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_COUNTER, TFW_ATECC_COUNTER_READ, idx,
                       0, 0u, v, 4u, TFW_ATECC_T_COUNTER);
    (void)tfw_atecc_sleep();
    if (st != TFW_OK) {
        return st;
    }
    *out = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    return TFW_OK;
}

/*
 * issue #4: the AUTH_CHALLENGE/AUTH_RESPONSE session's verify, entirely
 * additive to everything above -- a new entry point, not a mode of an
 * existing one, so nothing here changes what tfw_crypto_sign or
 * tfw_crypto_hmac do or the keys they touch.
 *
 * EXTERNAL key mode, not Stored: this key is the OPERATOR's, provisioned into
 * the platform-reserved EEPROM block by tfwprov (include/tfw/boot.h), never
 * into an element slot. That is the opposite tradeoff from
 * boot/verify/verify_atecc608a.c's Stored-mode use of the same opcode --
 * there, keeping the image-signing key off the bus is the whole point,
 * because that key is fixed for the product's life. Here the key can and
 * should rotate with the fleet's own key management, which an element slot
 * would make far harder: locking a slot is irreversible, and there is only
 * one data slot's worth of room to spend.
 *
 * The response is a bare status byte, not data -- 0x00 verified, 0x01
 * mismatch, matching the four-byte shape every other status-only command
 * here already gets (rlen=0), so this needs no new response-parsing path in
 * tfw_atecc_exec: buf[1] is read through the same translate() that already
 * carries the "checkmac or verify miscompare" case for exactly this byte.
 */
tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                      const uint8_t sig[64],
                                      const uint8_t pubkey[64])
{
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }

    /* Same pass-through load tfw_crypto_sign uses: TempKey gets the digest
     * verbatim, no seed update, no element randomness mixed in -- the
     * element verifies OUR digest and not something derived from it. */
    st = tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                       digest, 32u, 0, 0u, TFW_ATECC_T_NONCE);
    if (st == TFW_OK) {
        /* Its own command, not tfw_atecc_exec -- see atecc.h's comment on
         * tfw_atecc_verify_external for why: this payload is nearly double
         * the shared buffer tfw_atecc_exec reuses for every other command. */
        st = tfw_atecc_verify_external(sig, pubkey, TFW_ATECC_T_VERIFY);
    }
    (void)tfw_atecc_sleep();
    return st;
}
