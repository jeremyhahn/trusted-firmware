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
 *  - There is no TRNG. No AVR in this family has one. ag_crypto_random returns
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
 * So this tier reports AG_CRYPTO_SOFTWARE, and a verifier that treats it as
 * equivalent to an element-backed device is making a mistake the firmware has
 * done everything it can to prevent.
 */

#include <string.h>
#include "ag/crypto.h"
#include "ag/config.h"
#include "ag/protocol.h"
#include "ag/hal.h"
#include "../sha256.h"

/*
 * The device secret comes from the PLATFORM, not from this file.
 *
 * ag_crypto_soft_uds() is declared here and defined elsewhere -- by
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
const uint8_t *ag_crypto_soft_uds(void);

static uint8_t cdi[32];
static uint8_t provisioned;

ag_status_t ag_crypto_init(void)
{
    uint8_t        measurement[32];
    const uint8_t *uds = ag_crypto_soft_uds();

    provisioned = 0u;
    if (!uds) {
        return AG_OK;      /* not an error: an unprovisioned device still boots */
    }

    /* CDI = HMAC(UDS, H(firmware)).
     *
     * The DICE hardware requirements give this as the stronger of its two
     * forms: an HMAC's security is the full width of its key, where a bare
     * hash's is half its digest width.
     *
     * The measurement is a placeholder until the trusted bootloader exists to
     * supply a real one -- and it is deliberately NOT a hash of nothing, which
     * would produce a CDI that looks derived and is not.
     */
    ag_sha256((const uint8_t *)"ag-unmeasured-application-v1", 28u, measurement);
    ag_hmac_sha256(uds, 32u, measurement, sizeof measurement, cdi);
    provisioned = 1u;
    return AG_OK;
}

ag_crypto_tier_t ag_crypto_tier(void) { return AG_CRYPTO_SOFTWARE; }

uint16_t ag_crypto_algorithms(void)
{
    /* HMAC-SHA256 and nothing else. This tier has no asymmetric key at all, so
     * claiming ECDSA here would be the same defect as a KMS backend advertising
     * Ed25519 it cannot perform: the host negotiates down to something the
     * device then fails to do, at the point where it matters most. */
    return AG_ALGBIT_HMAC_SHA256;
}

ag_status_t ag_crypto_random(uint8_t *out, uint8_t len)
{
    (void)out; (void)len;
    /* Refused, on purpose. See the header of this file. */
    return AG_ERR_CRYPTO;
}

ag_status_t ag_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    ag_sha256(data, len, out);
    return AG_OK;
}

ag_status_t ag_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    if (slot != 0u) {
        return AG_ERR_RANGE;       /* one key on this tier */
    }
    if (!provisioned) {
        return AG_ERR_NOT_PROVISIONED;
    }
    ag_hmac_sha256(cdi, (uint8_t)sizeof cdi, data, len, out);
    return AG_OK;
}

ag_status_t ag_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    uint8_t     mac[32];
    ag_status_t st;

    if (!provisioned) {
        return AG_ERR_NOT_PROVISIONED;
    }
    st = ag_crypto_hmac(0u, digest, 32u, mac);
    if (st != AG_OK) {
        return st;
    }
    /* A 32-byte MAC in a 64-byte field. The remainder is zeroed rather than
     * left as whatever was on the stack: a verifier must be able to tell the
     * signature from the padding, and uninitialised padding would also leak
     * stack contents onto the wire. */
    memcpy(sig, mac, 32u);
    memset(sig + 32u, 0, 32u);
    *alg_id = AG_ALG_HMAC_SHA256;
    return AG_OK;
}

ag_status_t ag_crypto_pubkey(uint8_t out[64])
{
    (void)out;
    /* A symmetric identity has no public half. Saying so is the honest answer;
     * returning the key would hand out the ability to forge. */
    return AG_ERR_UNSUPPORTED;
}

ag_status_t ag_crypto_counter(uint8_t idx, uint32_t *out)
{
    (void)idx; (void)out;
    /* No monotonic counter without an element. The boot count and firmware
     * anti-rollback both need one, so they are unavailable on this tier rather
     * than backed by something that can be rewound. */
    return AG_ERR_UNSUPPORTED;
}
