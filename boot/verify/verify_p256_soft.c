/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * BOOT_SIG=p256-soft: P-256 ECDSA verified in software (#130, CA m453).
 *
 * Every tfw-boot row verifies the image signature, the soft rows included: an
 * OTA image that is not verified is remote code execution. The element rows
 * verify P-256 ECDSA over the trailer v1 with a 64-byte r || s signature
 * (verify_atecc608a.c). This backend verifies the same signature over the same
 * bytes (FIPS 186-5 section 6.4.2, SEC 1 v2 section 4.1.4), so one signed image
 * serves both tiers. The curve arithmetic is micro-ecc v1.2, vendored in
 * src/third_party/micro-ecc; nothing here reimplements it.
 */
#include <string.h>

#include "../boot_verify.h"
#include "boot_measure.h"
#include "tfw/progmem.h"
#include "uECC.h"

/* p256_key_slot.c, or the host test's own key. */
extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

/* The key lives in the boot section, as the element's lives in its slot, and
 * it is 64 bytes against the 32-byte pin. So this is the element's answer:
 * the backend keeps the key, the EEPROM pin is skipped, and the caller passes
 * a NULL key. */
tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    uint8_t key[64];
    uint8_t digest[32];
    uint8_t erased = 0xFFu;
    uint8_t i;
    int     ok;

    /* A caller that supplied a key expects it used, and this backend uses its
     * own. Refused rather than ignored, as the element does. */
    if (pub != 0) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (msg == 0 || sig == 0) {
        return TFW_ERR_CRYPTO;
    }
    for (i = 0u; i < 64u; i++) {
        key[i]  = (uint8_t)tfw_pgm_far_u8(tfw_boot_p256_key, &tfw_boot_p256_key[i]);
        erased &= key[i];
    }
    /* No key provisioned is a refusal, never a pass. */
    if (erased == 0xFFu) {
        return TFW_ERR_CRYPTO;
    }

    /* ECDSA signs a digest: the build's one hash, as the element backend. */
    tfw_boot_hash(msg, msglen, digest);
    ok = uECC_verify(key, digest, 32u, sig, uECC_secp256r1());
    return ok == 1 ? TFW_OK : TFW_ERR_CRYPTO;
}
