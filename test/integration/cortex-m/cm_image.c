/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M QEMU suite's signer and jig (#161): what the factory does to a
 * part before its first boot, done to the files QEMU loads.
 *
 *   cm_image <tfw-boot.bin> <tfw-boot.nm> <trusted-firmware.bin> <outdir>
 *
 * It provisions tfw-boot's two soft slots -- the P-256 public key
 * (boot/verify/p256_key_slot.c) and the UDS (boot/tier/cdi_soft.c) -- in the
 * raw boot image, found by symbol in the nm listing, as
 * test/integration/simavr/boot_harness.c's soft_patch_slot does in simavr's
 * flash. It signs the application as that harness's build_image_for does: the
 * slot erased to 0xFF, the image copied in, and the trailer at the slot's end
 * with a P-256 signature over the SHA-256 of its signed prefix. And it writes
 * the platform block (include/tfw/boot.h) tfw-boot reads before it verifies
 * anything, with the hash of the public key pinned.
 *
 * Into <outdir> go boot.bin, platform.bin, three applications -- app.bin
 * (genuine), app-tampered.bin (one bit of the image flipped after signing,
 * so its trailer claims the measurement of the image nobody edited) and
 * app-otherkey.bin (signed by a key tfw-boot does not hold) -- and addrs.env,
 * the load address of each, for test/integration/cortex-m/qemu_test.sh. With
 * them goes DEVICEID, the soft root's P-256 DeviceID public point (x||y, hex,
 * wirecheck's -deviceid) derived from the UDS just made, by the
 * src/crypto/deviceid_p256.c the firmware links: the seed is HMAC(UDS,
 * TFW_DEVICEID_LABEL), as tfw-boot derives it (boot/tier/cdi_soft.c) and
 * hands it to src/crypto/soft/id_p256.c in handoff v4 beside the CDI.
 *
 * The key pairs and the UDS are made fresh on every run, so no private key
 * and no device secret is in the tree.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uECC.h"   /* the soft row's signer: src/third_party/micro-ecc */

#include "tfw/boot.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "sha256.h"
#include "deviceid_p256.h"

#define UDS_LEN 32u

/* Not a serial: the pattern boot_harness.c's platform block carries. */
static const uint8_t TEST_DEVICE_SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,
    0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5
};

static uint8_t  boot[TFW_BOOT_REGION_SIZE];
static size_t   boot_len;
static uint8_t  app[TFW_BOOT_APP_SIZE];
static uint32_t app_len;
static uint8_t  image[TFW_BOOT_APP_SIZE];

static void die(const char *what, const char *detail)
{
    fprintf(stderr, "cm_image: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    exit(2);
}

static size_t read_file(const char *path, uint8_t *buf, size_t max)
{
    FILE  *f = fopen(path, "rb");
    size_t n;
    int    more;

    if (f == NULL) {
        die("cannot open", path);
    }
    n    = fread(buf, 1u, max, f);
    more = fgetc(f) != EOF;
    (void)fclose(f);
    if (more) {
        die("larger than the region it is for", path);
    }
    return n;
}

static void write_file(const char *dir, const char *name, const uint8_t *buf, size_t len)
{
    char  path[4096];
    FILE *f;

    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) {
        die("path too long", name);
    }
    f = fopen(path, "wb");
    if (f == NULL || fwrite(buf, 1u, len, f) != len || fclose(f) != 0) {
        die("cannot write", path);
    }
}

static void random_bytes(uint8_t *out, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");

    if (f == NULL || fread(out, 1u, len, f) != len) {
        die("cannot read", "/dev/urandom");
    }
    (void)fclose(f);
}

/* The symbol `name`, or `name.<suffix>` as LTO renames a local, from a line of
 * nm's default output: "<address> <type> <name>". */
static uint32_t slot_addr(const char *nm_path, const char *name, size_t len)
{
    FILE         *f = fopen(nm_path, "r");
    char          line[512], sym[256], type;
    unsigned long a;
    size_t        n = strlen(name);

    if (f == NULL) {
        die("cannot open", nm_path);
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "%lx %c %255s", &a, &type, sym) == 3
            && strncmp(sym, name, n) == 0 && (sym[n] == '\0' || sym[n] == '.')) {
            (void)fclose(f);
            if (a < TFW_BOOT_START || a - TFW_BOOT_START + len > boot_len) {
                die("slot outside the boot image", name);
            }
            return (uint32_t)a;
        }
    }
    (void)fclose(f);
    die("tfw-boot.elf has no slot to provision", name);
    return 0u;
}

/* An erased slot is the proof the address is right: the build leaves both
 * slots 0xFF (p256_key_slot.c, cdi_soft.c), and nothing else in tfw-boot is a
 * run of erased bytes that long at a symbol of that name. */
static void patch_slot(const char *nm_path, const char *name,
                       const uint8_t *bytes, size_t len)
{
    size_t off = slot_addr(nm_path, name, len) - TFW_BOOT_START;
    size_t i;

    for (i = 0u; i < len; i++) {
        if (boot[off + i] != 0xFFu) {
            die("slot is not erased", name);
        }
    }
    memcpy(&boot[off], bytes, len);
}

static void sha256_span(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    tfw_sha256_t ctx;
    uint32_t     off = 0u;

    tfw_sha256_init(&ctx);
    while (off < n) {
        uint32_t left = n - off;
        uint16_t take = left > 32768uL ? (uint16_t)32768u : (uint16_t)left;
        tfw_sha256_update(&ctx, &p[off], take);
        off += take;
    }
    tfw_sha256_final(&ctx, out);
}

/* The signature covers the trailer's prefix, version and length included,
 * not the measurement alone (include/tfw/boot.h). The tamper goes in last. */
static void sign_image(const uint8_t priv[32], long tamper_at)
{
    tfw_boot_trailer_t tr;
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    uint8_t            digest[32];

    memset(image, 0xFF, sizeof image);
    memcpy(image, app, app_len);

    memset(&tr, 0, sizeof tr);
    tr.image_version = 1u;
    tr.image_len     = app_len;
    tr.board_id      = (uint8_t)TFW_BOARD_ID;
    tr.image_flags   = (uint8_t)TFW_IMAGE_FLAGS;
    sha256_span(image, app_len, tr.measurement);

    tfw_boot_trailer_serialise(&tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, digest);
    if (!uECC_sign(priv, digest, sizeof digest, &raw[TFW_BOOT_TR_SIGNATURE], uECC_secp256r1())) {
        die("uECC_sign failed", NULL);
    }
    memcpy(&image[TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE], raw, TFW_BOOT_TRAILER_LEN);

    if (tamper_at >= 0) {
        image[tamper_at] ^= 0x01u;
    }
}

int main(int argc, char **argv)
{
    uint8_t             priv[32], pub[64], other_priv[32], other_pub[64];
    uint8_t             op_priv[32], op_pub[64], uds[UDS_LEN];
    uint8_t             seed[32], deviceid[64];
    uint8_t             ee[TFW_BOOT_EE_BLOCK_LEN];
    tfw_boot_platform_t plat;
    char                env[384];
    size_t              n;
    unsigned            i;

    if (argc != 5) {
        die("usage", "cm_image <tfw-boot.bin> <tfw-boot.nm> <trusted-firmware.bin> <outdir>");
    }
    boot_len = read_file(argv[1], boot, sizeof boot);
    app_len  = (uint32_t)read_file(argv[3], app, sizeof app);
    if (app_len == 0u || app_len > TFW_BOOT_MAX_IMAGE_LEN) {
        die("the application does not fit before its trailer", argv[3]);
    }

    if (!uECC_make_key(pub, priv, uECC_secp256r1())
        || !uECC_make_key(other_pub, other_priv, uECC_secp256r1())
        || !uECC_make_key(op_pub, op_priv, uECC_secp256r1())) {
        die("uECC_make_key failed", NULL);
    }
    do {
        random_bytes(uds, sizeof uds);   /* all 0xFF reads as an erased slot */
    } while (uds[0] == 0xFFu && memcmp(uds, &uds[1], sizeof uds - 1u) == 0);

    patch_slot(argv[2], "tfw_boot_p256_key", pub, sizeof pub);
    patch_slot(argv[2], "uds_slot", uds, sizeof uds);
    write_file(argv[4], "boot.bin", boot, boot_len);

    sign_image(priv, -1);
    write_file(argv[4], "app.bin", image, sizeof image);
    sign_image(priv, (long)(app_len / 2u));
    write_file(argv[4], "app-tampered.bin", image, sizeof image);
    sign_image(other_priv, -1);
    write_file(argv[4], "app-otherkey.bin", image, sizeof image);

    /* boot_harness.c's soft row: the key's hash pinned, the state a committed
     * image leaves, the operator's public key a real point nobody uses here. */
    memset(&plat, 0, sizeof plat);
    tfw_sha256(pub, sizeof pub, plat.pubkey_hash);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    memcpy(plat.serial, TEST_DEVICE_SERIAL, TFW_BOOT_SERIAL_LEN);
    memcpy(plat.operator_pubkey, op_pub, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    memset(ee, 0xFF, sizeof ee);
    tfw_boot_platform_serialise(&plat, ee);
    write_file(argv[4], "platform.bin", ee, sizeof ee);

    /* The point the firmware's ATTEST must verify under: the soft root's
     * DeviceID, from the UDS patched above, by the firmware's own code. */
    tfw_hmac_sha256(uds, (uint8_t)UDS_LEN, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, seed);
    tfw_deviceid_p256_public(seed, deviceid);
    memset(seed, 0, sizeof seed);

    n = (size_t)snprintf(env, sizeof env, "BOOT_AT=0x%08lx\nAPP_AT=0x%08lx\nPLATFORM_AT=0x%08lx\nDEVICEID=",
                         (unsigned long)TFW_BOOT_START, (unsigned long)TFW_BOOT_APP_BASE,
                         (unsigned long)(TFW_CM_PLATFORM_BASE + TFW_BOOT_EE_BASE));
    for (i = 0u; i < sizeof deviceid; i++) {
        n += (size_t)snprintf(&env[n], sizeof env - n, "%02x", deviceid[i]);
    }
    (void)snprintf(&env[n], sizeof env - n, "\n");
    write_file(argv[4], "addrs.env", (const uint8_t *)env, strlen(env));
    return 0;
}
