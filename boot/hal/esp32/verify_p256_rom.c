/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Layer 0's ES256 verify through the chip's ROM (#48), for the targets whose
 * ROM has ECDSA: the ESP32-P4. The check is boot/verify/verify_p256_soft.c's --
 * P-256 ECDSA (FIPS 186-5 section 6.4.2) of the trailer's 64-byte r || s over
 * the SHA-256 of the signed bytes, with the key the bootloader carries -- and
 * so is the contract, so l0_gate.c links either. Nothing here is curve
 * arithmetic: that is the ROM's, and with it micro-ecc leaves the bootloader,
 * whose iram_loader_seg it did not fit on the P4.
 *
 * ESP-IDF v5.5.5 declares the verify in
 * components/esp_rom/esp32p4/include/esp32p4/rom/ecdsa.h:24, and since
 * ESP_ROM_ECDSA_VERIFY_PATCH (esp32p4/esp_rom_caps.h:31) the symbol is its
 * wrapper, components/esp_rom/patches/esp_rom_ecdsa.c:92: it refuses r or s
 * outside [1, n-1], powers the ECC block up and calls the ROM's
 * _rom_ets_ecdsa_verify, and answers 1 for a good signature. Its caller in
 * secure boot v2 (bootloader_support/src/secure_boot_v2/secure_boot.c:124)
 * passes the image's SHA-256 as the hash returns it, and the key and the
 * signature as the signature block's ECDSA member lays them out
 * (esp32p4/rom/secure_boot.h:100,102): X then Y, and r then s, each
 * little-endian. Ours are big-endian, as SEC 1 and the trailer write them, so
 * each 32-byte half is reversed here and nowhere else.
 */
#include "boot_measure.h"
#include "boot_verify.h"
#include "tfw/crypto.h"
#include "tfw/progmem.h"
#include "rom/ecdsa.h"

/* p256_key_slot.c, or the host test's own key: X || Y, big-endian. */
extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

/* Each 32-byte half of a 64-byte pair, byte-reversed into out. */
static void halves_le(uint8_t out[64], const uint8_t in[64])
{
    uint8_t i;

    for (i = 0u; i < 32u; i++) {
        out[i]       = in[31u - i];
        out[32u + i] = in[63u - i];
    }
}

/* The key is the bootloader's, as with p256-soft: no key from the caller. */
tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

/* ES256: SHA-256, then the ROM's P-256 verify. */
const uint8_t tfw_boot_cose_alg = 0x26u;

/* The digest entry, which the SUIT manifest's COSE_Sign1 check reaches through
 * tfw/crypto.h (src/core/suit.c hashes the Sig_structure itself), and the
 * trailer's check below after hashing. */
tfw_status_t tfw_crypto_verify_image(const uint8_t digest[TFW_SHA256_LEN],
                                     const uint8_t sig[TFW_SIG_LEN])
{
    uint8_t key[64];
    uint8_t key_le[64];
    uint8_t sig_le[64];
    uint8_t verified[ETS_DIGEST_SHA256_LEN];
    uint8_t erased = 0xFFu;
    uint8_t i;
    int     ok;

    for (i = 0u; i < 64u; i++) {
        key[i]  = (uint8_t)tfw_pgm_far_u8(tfw_boot_p256_key, &tfw_boot_p256_key[i]);
        erased &= key[i];
    }
    /* No key provisioned is a refusal, never a pass. */
    if (erased == 0xFFu) {
        return TFW_ERR_CRYPTO;
    }

    halves_le(key_le, key);
    halves_le(sig_le, sig);
    /* 1 is the only pass, as secure_boot.c:124 reads it. */
    ok = ets_ecdsa_verify(key_le, sig_le, ECDSA_CURVE_P256, digest, verified);
    return ok == 1 ? TFW_OK : TFW_ERR_CRYPTO;
}

tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    uint8_t digest[ETS_DIGEST_SHA256_LEN];

    if (pub != 0) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (msg == 0 || sig == 0) {
        return TFW_ERR_CRYPTO;
    }
    tfw_boot_hash(msg, msglen, digest);
    return tfw_crypto_verify_image(digest, sig);
}
