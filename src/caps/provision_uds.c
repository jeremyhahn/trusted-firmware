/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Provisioning on a part whose UDS is a one-time key it burns itself -- the
 * backend under the wire handlers of provision.c for the ESP32's hmac-p256
 * tier (docs/multi-arch/esp32-p4-design.md section 2.4). Linked only beside
 * provision.c, so everything that file says about a production image applies.
 *
 * PROV_GENKEY is the whole of it. The part draws 32 bytes from its own true
 * random source, burns them as the UDS key, read- and write-protected, and
 * this file scrubs its copy; the DeviceID point is then derived exactly as
 * the production image derives it -- HMAC-SHA256 of the label under the UDS,
 * computed by the part, then src/crypto/deviceid_p256.c -- so the point the
 * jig records is the point every later boot attests with. The UDS never
 * crosses the wire and nothing here can return it: a burned key block is
 * unreadable to software, which is why the derivation asks the part.
 *
 * The burn is once in the part's life. A second PROV_GENKEY is refused
 * before anything is drawn or burned; PROV_PUBKEY reads the point back
 * without that risk. The element's other commands have nothing to act on
 * here and answer TFW_ERR_UNSUPPORTED.
 */

#include "provision.h"
#include "provision_uds.h"
#include "../core/secure_zero.h"
#include "../crypto/deviceid_p256.h"

/* The same slot number the element's DeviceID key has, so tfwprov drives
 * either backend with the same arguments. */
#define UDS_SLOT_DEVICEID 0u

static tfw_status_t deviceid(uint8_t pub[64])
{
    uint8_t      seed[32];
    tfw_status_t st;

    st = tfw_prov_uds_hmac((const uint8_t *)TFW_DEVICEID_LABEL,
                           TFW_DEVICEID_LABEL_LEN, seed);
    if (st == TFW_OK && !tfw_deviceid_p256_public(seed, pub)) {
        st = TFW_ERR_CRYPTO;
    }
    tfw_secure_zero(seed, sizeof seed);
    return st;
}

tfw_status_t tfw_prov_info(uint8_t rev[4], uint8_t cfg[32], uint8_t lock[2])
{
    (void)rev;
    (void)cfg;
    (void)lock;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_prov_write_config(uint8_t block, const uint8_t data[32])
{
    (void)block;
    (void)data;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_prov_lock(uint8_t zone, uint16_t crc)
{
    (void)zone;
    (void)crc;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_prov_genkey(uint8_t slot, uint8_t pub[64])
{
    uint8_t      uds[32];
    tfw_status_t st;

    if (slot != UDS_SLOT_DEVICEID) {
        return TFW_ERR_RANGE;
    }
    if (tfw_prov_uds_present()) {
        /* The key block is burned and write-protected: there is no second
         * identity to give this part, and asking is refused before the
         * random source is touched. */
        return TFW_ERR_PROTECTED_REGION;
    }
    st = tfw_prov_uds_random(uds);
    if (st == TFW_OK) {
        st = tfw_prov_uds_burn(uds);
    }
    /* Scrubbed whether or not the burn took: a failed attempt must not leave
     * the secret in a buffer for the next command to reuse. */
    tfw_secure_zero(uds, sizeof uds);
    if (st != TFW_OK) {
        return st;
    }
    return deviceid(pub);
}

tfw_status_t tfw_prov_pubkey(uint8_t slot, uint8_t pub[64])
{
    if (slot != UDS_SLOT_DEVICEID) {
        return TFW_ERR_RANGE;
    }
    if (!tfw_prov_uds_present()) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    return deviceid(pub);
}

tfw_status_t tfw_prov_seed_uds(uint8_t slot)
{
    /* PROV_GENKEY draws and burns the UDS in one step on this part; a
     * separate seeding command would be a second way to burn it. */
    (void)slot;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_prov_write_pubkey(uint8_t slot, uint8_t half, const uint8_t coord[32],
                                   uint8_t back[64])
{
    (void)slot;
    (void)half;
    (void)coord;
    (void)back;
    return TFW_ERR_UNSUPPORTED;
}
