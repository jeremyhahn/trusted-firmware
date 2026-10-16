/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SOFTWARE crypto tier.
 *
 * What it is: SHA-256 and HMAC in software, with the identity key in flash.
 *
 * What it is NOT, and the reason this file says so at length rather than
 * quietly doing its best:
 *
 *  - There is no TRNG. No AVR in this family has one. tfw_crypto_random returns
 *    an ERROR here rather than something that merely looks random, because a
 *    predictable nonce presented as a random one is worse than a refusal -- the
 *    refusal stops the protocol and the prediction does not.
 *
 *  - There is no key isolation. Once the application runs it can read all of
 *    SRAM and, on a part without a boot section, all of flash. Lock bits stop
 *    an external programmer and say nothing about application code.
 *
 *  - The signature is an HMAC, so the VERIFIER HOLDS THE KEY and can forge any
 *    response it could verify. That is the TCG symmetric-identity path, which
 *    the spec defines precisely for parts too small for asymmetric crypto -- and
 *    it means this tier is for local attestation behind an attested gateway,
 *    never for admission to a public network.
 *
 * So this tier reports TFW_CRYPTO_SOFTWARE, and a verifier that treats it as
 * equivalent to an element-backed device is making a mistake the firmware has
 * done everything it can to prevent.
 */

#include <string.h>
#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/hal.h"
#include "../sha256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"
#include "tfw/progmem.h"

/* What an unprovisioned-by-boot device derives its CDI from: 29 bytes, no NUL. */
static const char UNMEASURED[] TFW_PROGMEM = "tfw-unmeasured-application-v1";
#define UNMEASURED_LEN ((uint16_t)(sizeof UNMEASURED - 1u))

/*
 * The device secret comes from the PLATFORM, not from this file.
 *
 * tfw_crypto_soft_uds() is declared here and defined elsewhere -- by
 * src/crypto/soft/uds_flash.c on a device, reading the boot section behind lock
 * bits, and by the test mock on the host. Link-time substitution, exactly as
 * the HAL does it, so neither build carries a conditional and neither can
 * accidentally get the other's secret.
 *
 * It returns NULL when the device has not been provisioned, and everything
 * below refuses rather than improvising. A device with no identity that signed
 * anyway would produce evidence that verifies against a key every build of this
 * source shares -- which is to say, evidence that proves nothing while looking
 * exactly like evidence that proves something.
 */
const uint8_t *tfw_crypto_soft_uds(void);

static uint8_t cdi[32];
static uint8_t provisioned;

tfw_status_t tfw_crypto_init(void)
{
    const uint8_t *uds;
    uint8_t        measurement[32];

    provisioned = 0u;

    /*
     * PREFERRED: the CDI tfw-boot derived and handed off -- HMAC(UDS, the
     * measurement tfw-boot actually verified), not a placeholder. This is what
     * makes the DICE hardware requirement real on this tier: the identity
     * this device signs with depends on the firmware it is running. See
     * issues #6 and #18.
     *
     * ACCEPTED ONLY WHEN THE HANDED-OFF TIER MATCHES. A handoff naming
     * TFW_CRYPTO_SECURE_ELEMENT means tfw-boot derived that CDI from the
     * element's UDS -- a secret this tier cannot name, has no way to
     * reproduce, and never provisions into anywhere this tier reads from. A
     * verifier configured for the software tier's own UDS could never
     * recompute a signature made under that key: evidence would be signed,
     * self-consistent within this one boot, and unverifiable by anyone. A
     * mismatched-tier CDI is drained and discarded all the same, rather than
     * left resident for the tier it does not belong to.
     *
     * A MISMATCH DOES NOT FALL THROUGH TO THE LOCAL FALLBACK BELOW. A real
     * tfw-boot ran, verified a real image, and derived a CDI from a UDS this
     * tier cannot use -- that is a genuinely misconfigured board (this
     * application's tier does not match the bootloader that shipped with
     * it), not "no bootloader ran." Falling through would sign with a key
     * derived from this application's OWN local UDS and a PLACEHOLDER
     * measurement, while FWHash (src/caps/attest.c, independent of this
     * provider) reports the REAL measurement tfw-boot verified: a verifier
     * would see a signature that does not correspond to the measurement
     * claimed alongside it, which is the exact "evidence that verifies
     * against nothing" failure the mismatch check exists to prevent, just
     * reached by a different path. The device stays unprovisioned instead --
     * `provisioned` is already 0 from the top of this function.
     *
     * NOR DOES A RIGHT-TIER HANDOFF WITH NO USABLE CDI. `tfw_boot_handoff_
     * take_cdi` also returns 0 when the handoff was valid, named THIS tier,
     * but carried tfw-boot's own all-zero "unprovisioned" sentinel
     * (src/core/handoff.c) -- tfw-boot ran, verified a real image, and found
     * no UDS to derive from. That is the same misbinding as the mismatch
     * above, reached from the other direction: falling through here would
     * again sign with this application's OWN local UDS and a placeholder
     * measurement while FWHash reports the real one tfw-boot verified. A
     * valid handoff for this tier is therefore authoritative either way --
     * a usable CDI provisions, an unusable one refuses -- and only the
     * ABSENCE of any valid handoff reaches the local fallback below. See
     * docs/dice-attestation/architecture.md's data-flow-status section.
     */
    {
        const uint8_t *handoff_tier = tfw_boot_handoff_tier();

        if (handoff_tier && *handoff_tier == (uint8_t)TFW_CRYPTO_SOFTWARE) {
            if (tfw_boot_handoff_take_cdi(cdi)) {
                provisioned = 1u;
            }
            return TFW_OK;
        } else if (handoff_tier) {
            uint8_t discard[32];
            if (tfw_boot_handoff_take_cdi(discard)) {
                tfw_secure_zero(discard, sizeof discard);
            }
            return TFW_OK;
        }
    }

    /*
     * FALLBACK: no valid handoff at all -- no bootloader ran, a stock
     * bootloader ran (issue #18's own case), or this build predates the
     * handoff. Derives locally from this application's own flash-patched
     * UDS exactly as before the handoff existed, so a device behind a stock
     * bootloader still has an identity rather than none at all.
     *
     * CDI = HMAC(UDS, H(firmware)). The DICE hardware requirements give this
     * as the stronger of its two forms: an HMAC's security is the full width
     * of its key, where a bare hash's is half its digest width.
     *
     * The measurement here is a placeholder, deliberately NOT a hash of
     * nothing (which would produce a CDI that looks derived and is not) --
     * there being no bootloader in this path means there is no real one to
     * use, which is also why FWHash stays zero for a device on this path
     * (src/caps/attest.c).
     */
    uds = tfw_crypto_soft_uds();
    if (!uds) {
        return TFW_OK;      /* not an error: an unprovisioned device still boots */
    }
    /* The label is hashed in place: it lives in flash, not in .data. */
    tfw_pgm_copy(measurement, UNMEASURED, UNMEASURED_LEN);
    tfw_sha256(measurement, UNMEASURED_LEN, measurement);
    tfw_hmac_sha256(uds, 32u, measurement, sizeof measurement, cdi);
    provisioned = 1u;
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void) { return TFW_CRYPTO_SOFTWARE; }

uint16_t tfw_crypto_algorithms(void)
{
    /* HMAC-SHA256 and nothing else. This tier has no asymmetric key at all, so
     * claiming ECDSA here would be the same defect as a KMS backend advertising
     * Ed25519 it cannot perform: the host negotiates down to something the
     * device then fails to do, at the point where it matters most. */
    return TFW_ALGBIT_HMAC_SHA256;
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    (void)out; (void)len;
    /* Refused, on purpose. See the header of this file. */
    return TFW_ERR_CRYPTO;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    tfw_sha256(data, len, out);
    return TFW_OK;
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    if (slot != 0u) {
        return TFW_ERR_RANGE;       /* one key on this tier */
    }
    if (!provisioned) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    tfw_hmac_sha256(cdi, (uint8_t)sizeof cdi, data, len, out);
    return TFW_OK;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    uint8_t     mac[32];
    tfw_status_t st;

    if (!provisioned) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    st = tfw_crypto_hmac(0u, digest, 32u, mac);
    if (st != TFW_OK) {
        return st;
    }
    /* A 32-byte MAC in a 64-byte field. The remainder is zeroed rather than
     * left as whatever was on the stack: a verifier must be able to tell the
     * signature from the padding, and uninitialised padding would also leak
     * stack contents onto the wire. */
    memcpy(sig, mac, 32u);
    memset(sig + 32u, 0, 32u);
    *alg_id = TFW_ALG_HMAC_SHA256;
    return TFW_OK;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    (void)out;
    /* A symmetric identity has no public half. Saying so is the honest answer;
     * returning the key would hand out the ability to forge. */
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    (void)idx; (void)out;
    /* No monotonic counter without an element. The boot count and firmware
     * anti-rollback both need one, so they are unavailable on this tier rather
     * than backed by something that can be rewound. */
    return TFW_ERR_UNSUPPORTED;
}

/* issue #4 -- ELEMENT TIER ONLY, and deliberately so on this one.
 *
 * The obvious primitive for a P-256/Ed25519 verify on a bare AVR is
 * ed25519-donna, already vendored here for BOOT_SIG=ed25519-donna. It does
 * not fit: the measured cost of its verify path alone, gc-sectioned, is
 * ~73-85 KB of flash and a single ~2.95 KB call frame. That overflows an
 * ATmega328P's entire 32 KB of flash before the rest of the firmware is
 * counted, and it is 31-47% over this project's own 64 KB application flash
 * budget on an ATmega2560 -- see changelog.d/ for the measurement. There is no
 * smaller vendored alternative and this tier does not get one; the owner
 * ruling for issue #4 is that AUTH_CHALLENGE/AUTH_RESPONSE are simply not
 * compiled into a soft-tier image (mk/avr.mk links auth_session_none.c
 * instead), and the reserved-EEPROM-read gate is
 * not enforced on this tier either. See docs/security/threat-model.md. */
tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                      const uint8_t sig[64],
                                      const uint8_t pubkey[64])
{
    (void)digest; (void)sig; (void)pubkey;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[32], const uint8_t sig[64])
{
    (void)digest; (void)sig;
    return TFW_ERR_UNSUPPORTED;
}
