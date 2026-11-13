/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The element tier's side of tfwsim: the real ATECC608A driver and provider,
 * with test/model/atecc608a.c standing in for the part on the I2C bus.
 *
 * THE IDENTITY THIS SEEDS IS A TEST IDENTITY AND MUST NEVER BE TRUSTED.
 * The model does real P-256 (see its header), so the evidence from this binary
 * VERIFIES -- under a key whose private half is the SHA-256 of a published
 * string. That is what lets a host exercise the real firmware's DICE paths end
 * to end, signature checks included; it is not a reason for anyone to attest
 * to anything with it, enrol it or allow-list it.
 *
 * A device needs three things, and each has its own explicit, off-by-default
 * option, so a start with no options is a blank part and says so:
 *
 *   platform EEPROM block   --eeprom FILE      Intel HEX, what `tfwprov
 *                           --eeprom-bin FILE  --bake-eeprom` writes; or a raw
 *                                              image
 *   provisioned element     --provision-element  the TEST DeviceID key in slot 0 and
 *                                              a TEST UDS, as tfwprov leaves a part;
 *                                              tfwsim-manifest also puts the
 *                                              TEST image key in the image
 *                                              slot (kImageKeySeed)
 *   tfw-boot's CDI handoff   --image-hash HEX64 a block built by the SAME
 *                                              builder tfw-boot uses
 *                                              (boot/boot_handoff.c)
 *   ...from a soft tfw-boot  --boot-tier soft   the handoff names the soft
 *                                              tier, as BOOT_TIER=soft's does;
 *                                              the default names the element
 *   ...holding a UDS         --uds HEX64        a soft tfw-boot's handoff with
 *                                              the CDI and the DeviceID seed
 *                                              it derives from that UDS
 *                                              (boot/tier/cdi_soft.c)
 *
 * Element provisioning is seeded directly rather than driven over the wire:
 * the provisioning commands exist only in a jig image (TFW_FEATURE_PROVISION),
 * and the model's state lives in this process, so a provisioning session would
 * not survive into the production-shaped binary a test then talks to.
 *
 * tfwsim-prov is that jig image: this file and the same object list, plus
 * src/caps/provision.c and tools/tfwsim/reboot.c (mk/host.mk). Started without
 * --provision-element its element is blank, `tfwprov --commit` provisions it
 * over the wire, and after a RESET, which keeps the element, the same process
 * attests with the key the element generated.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tier.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "boot_measure.h"
#include "deviceid_p256.h"
#include "hal_mock.h"
#include "sha256.h"
#include "tfw/types.h"

#ifdef TFWSIM_IMAGE_KEY
/* The seed of the image slot's TEST key, compiled into tfwsim-manifest alone
 * (mk/host.mk sets TFWSIM_IMAGE_KEY on that binary only): every other element
 * variant's --provision-element leaves the image slot as it was. PUBLIC ON
 * PURPOSE: the private key is SHA-256 of these bytes (tfw_model_p256_key), so
 * nothing may ever trust an image it verifies.
 * sdk/go/cmd/tfwctl/e2e_ota_manifest_test.go derives the same key from the
 * same string. */
static const char kImageKeySeed[] = "tfwsim-atecc608a TEST image key v1";
#endif

static uint8_t *eeprom_image;          /* NULL until --eeprom/--eeprom-bin */
static size_t   eeprom_len;
static uint8_t  provision_element;
static uint8_t  no_element;
static uint8_t  have_image_hash;
static uint8_t  boot_tier = (uint8_t)TFW_CRYPTO_SECURE_ELEMENT;
static uint8_t  image_hash[TFW_BOOT_MEASURE_LEN];
static uint8_t  have_uds;
static uint8_t  uds[32];

/* ------------------------------------------------------------ parsing */

static int fail(const char *what, const char *detail)
{
    fprintf(stderr, "tfwsim-atecc608a: %s: %s\n", what, detail);
    return -1;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

/* Decodes exactly n bytes of hex from s[0..len); -1 on any other shape. */
static int hex_bytes(const char *s, size_t len, uint8_t *out, size_t n)
{
    size_t i;

    if (len != n * 2u) { return -1; }
    for (i = 0; i < n; i++) {
        int hi = hex_nibble(s[2u * i]);
        int lo = hex_nibble(s[2u * i + 1u]);

        if (hi < 0 || lo < 0) { return -1; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* The whole file, or NULL with a message. */
static uint8_t *slurp(const char *path, size_t *len)
{
    FILE    *f = fopen(path, "rb");
    uint8_t *buf;
    size_t   cap = 4096u, n = 0u, got;

    if (!f) {
        fprintf(stderr, "tfwsim-atecc608a: cannot open %s\n", path);
        return NULL;
    }
    buf = malloc(cap);
    while (buf && (got = fread(buf + n, 1u, cap - n, f)) > 0u) {
        n += got;
        if (n == cap) {
            uint8_t *bigger;

            cap *= 2u;
            bigger = realloc(buf, cap);
            if (!bigger) { free(buf); buf = NULL; }
            else         { buf = bigger; }
        }
    }
    if (buf && ferror(f)) { free(buf); buf = NULL; }
    (void)fclose(f);
    if (!buf) {
        fprintf(stderr, "tfwsim-atecc608a: cannot read %s\n", path);
        return NULL;
    }
    *len = n;
    return buf;
}

/*
 * Intel HEX into an erased (0xFF) EEPROM image. Data records only, plus the
 * EOF record; every record's checksum is verified, and a byte beyond the
 * mock's EEPROM or an address-extension record is an error rather than being
 * wrapped or dropped. A partial file is correct and expected: `tfwprov
 * --bake-eeprom` covers only the platform block, and the rest stays erased.
 */
static int ihex_load(const char *text, size_t n, uint8_t *img, size_t cap)
{
    size_t p = 0u;
    int    eof = 0;

    memset(img, 0xFF, cap);
    while (p < n) {
        uint8_t rec[5 + 255 + 1];
        size_t  start, end, len, k;
        uint8_t sum = 0u;

        if (text[p] == '\r' || text[p] == '\n') { p++; continue; }
        if (text[p] != ':') { return -1; }
        start = ++p;
        while (p < n && text[p] != '\r' && text[p] != '\n') { p++; }
        end = p;
        len = end - start;
        if (len < 10u || (len & 1u)) { return -1; }
        if (hex_bytes(&text[start], len, rec, len / 2u) < 0) { return -1; }
        if ((size_t)rec[0] + 5u != len / 2u) { return -1; }
        for (k = 0u; k < (size_t)rec[0] + 5u; k++) { sum = (uint8_t)(sum + rec[k]); }
        if (sum != 0u || eof) { return -1; }

        if (rec[3] == 0x00u) {
            size_t addr = ((size_t)rec[1] << 8) | rec[2];

            if (addr + rec[0] > cap) { return -1; }
            memcpy(&img[addr], &rec[4], rec[0]);
        } else if (rec[3] == 0x01u) {
            eof = 1;
        } else {
            return -1;
        }
    }
    return eof ? 0 : -1;
}

static int load_eeprom(const char *path, int is_hex)
{
    size_t   n = 0u;
    uint8_t *file = slurp(path, &n);
    size_t   cap = tfw_mock_eeprom_size();

    if (!file) { return -1; }
    free(eeprom_image);
    eeprom_image = malloc(cap);
    if (!eeprom_image) { free(file); return fail("out of memory", path); }
    eeprom_len = cap;

    if (is_hex) {
        if (ihex_load((const char *)file, n, eeprom_image, cap) < 0) {
            free(file);
            return fail("not a valid Intel HEX EEPROM image for this part", path);
        }
    } else {
        if (n > cap) {
            free(file);
            return fail("image is larger than the EEPROM", path);
        }
        memset(eeprom_image, 0xFF, cap);
        memcpy(eeprom_image, file, n);
    }
    free(file);
    return 0;
}

/* ------------------------------------------------------------ the seam */

int tfwsim_tier_option(int argc, char **argv)
{
    if (strcmp(argv[0], "--provision-element") == 0) {
        provision_element = 1u;
        return 1;
    }
    /* tfwsim-auto (issue #130): a board with no element fitted. The model
     * NACKs every transfer, as an empty bus does, so CRYPTO=auto's probe
     * finds nothing and the image runs the soft root. */
    if (strcmp(argv[0], "--no-element") == 0) {
        no_element = 1u;
        return 1;
    }
    /* tfwsim-auto (issue #130): the CDI a soft tfw-boot hands off, derived
     * from its UDS in boot-section flash, so a board with no element carries
     * the soft root's identity. */
    if (strcmp(argv[0], "--boot-tier") == 0) {
        if (argc < 2) { return fail(argv[0], "needs a value"); }
        if (strcmp(argv[1], "soft") == 0) {
            boot_tier = (uint8_t)TFW_CRYPTO_SOFTWARE;
        } else if (strcmp(argv[1], "element") == 0) {
            boot_tier = (uint8_t)TFW_CRYPTO_SECURE_ELEMENT;
        } else {
            return fail("--boot-tier", "want soft or element");
        }
        return 2;
    }
    /* tfwsim-auto: what tfw-boot hands a board with no element. On a device
     * the soft root's DeviceID seed comes only from tfw-boot's handoff, and
     * the application never sees the UDS (#173), so a host build of that
     * board needs a stand-in for tfw-boot that holds one. */
    if (strcmp(argv[0], "--uds") == 0) {
        if (argc < 2) { return fail(argv[0], "needs a value"); }
        if (hex_bytes(argv[1], strlen(argv[1]), uds, sizeof uds) < 0) {
            return fail("--uds", "want 64 hex characters (32 bytes)");
        }
        have_uds = 1u;
        return 2;
    }
    if (strcmp(argv[0], "--eeprom") == 0 || strcmp(argv[0], "--eeprom-bin") == 0 ||
        strcmp(argv[0], "--image-hash") == 0) {
        if (argc < 2) { return fail(argv[0], "needs a value"); }
        if (strcmp(argv[0], "--image-hash") == 0) {
            if (hex_bytes(argv[1], strlen(argv[1]), image_hash, sizeof image_hash) < 0) {
                return fail("--image-hash", "want 64 hex characters (a SHA-256)");
            }
            have_image_hash = 1u;
            return 2;
        }
        return load_eeprom(argv[1], strcmp(argv[0], "--eeprom") == 0) < 0 ? -1 : 2;
    }
    return 0;
}

const char *tfwsim_tier_usage(void)
{
    return " [--eeprom FILE.eep | --eeprom-bin FILE] [--provision-element]"
           " [--image-hash HEX64] [--boot-tier soft|element] [--uds HEX64]"
           " [--no-element]";
}

/*
 * A CDI for the handoff, because the block tfw-boot hands over has one and the
 * builder takes one. This tier never uses it -- its DeviceID key is
 * element-resident and not CDI-derived (src/crypto/atecc608a/provider.c) -- so
 * it is only required to be non-zero, which is what makes the block read as
 * provisioned rather than tfw-boot's all-zero "unprovisioned" sentinel.
 * Derived from the image hash so a different image gets a different CDI, as
 * on a device.
 */
static void test_cdi(uint8_t cdi[TFW_BOOT_MEASURE_LEN])
{
    static const char domain[] = "tfwsim-test-cdi-v1";
    uint8_t           in[sizeof domain - 1u + TFW_BOOT_MEASURE_LEN];

    memcpy(in, domain, sizeof domain - 1u);
    memcpy(in + sizeof domain - 1u, image_hash, TFW_BOOT_MEASURE_LEN);
    tfw_sha256(in, (uint16_t)sizeof in, cdi);
}

void tfwsim_tier_seed(void)
{
    tfw_model_reset();
    tfw_model_set_absent(no_element);
    if (provision_element) {
        tfw_model_provision();
#ifdef TFWSIM_IMAGE_KEY
        {
            uint8_t image_priv[32], image_pub[64];

            /* The key OTA_ACTIVATE's manifest gate verifies against, which
             * PROV_* writes on a device. A TEST key: its private half is the
             * model's key from kImageKeySeed, so a test signs with it. */
            tfw_model_p256_key(kImageKeySeed, image_priv, image_pub);
            tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, image_pub);
        }
#endif
    }
    if (eeprom_image) {
        memcpy(tfw_mock_eeprom(), eeprom_image, eeprom_len);
    }
    if (have_uds) {
        /* What boot/tier/cdi_soft.c and boot/boot_main.c leave for this UDS:
         * CDI = HMAC-SHA256(UDS, measurement), device_seed =
         * HMAC-SHA256(UDS, TFW_DEVICEID_LABEL), in a handoff naming the soft
         * tier. The measurement is --image-hash, or zero without one. */
        uint8_t cdi[TFW_BOOT_MEASURE_LEN];
        uint8_t seed[TFW_BOOT_SEED_LEN];

        tfw_hmac_sha256(uds, (uint8_t)sizeof uds, image_hash, TFW_BOOT_MEASURE_LEN, cdi);
        tfw_hmac_sha256(uds, (uint8_t)sizeof uds, (const uint8_t *)TFW_DEVICEID_LABEL,
                        TFW_DEVICEID_LABEL_LEN, seed);
        tfw_boot_handoff_build(tfw_mock_handoff(), (uint8_t)TFW_CRYPTO_SOFTWARE,
                              cdi, image_hash, 1u, TFW_RESET_POWER_ON);
        tfw_boot_handoff_set_seed(tfw_mock_handoff(), seed);
    } else if (have_image_hash) {
        uint8_t cdi[TFW_BOOT_MEASURE_LEN];

        test_cdi(cdi);
        tfw_boot_handoff_build(tfw_mock_handoff(), boot_tier,
                              cdi, image_hash, 1u, TFW_RESET_POWER_ON);
    }
}
