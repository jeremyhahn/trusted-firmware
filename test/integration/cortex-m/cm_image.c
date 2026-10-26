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
 * the load address of each, for test/integration/cortex-m/qemu_test.sh. On a
 * two-slot board those are the factory copies' addresses, TFW_CM_LOAD_*
 * (boot/hal/cortex-m/boot_factory.c), not the primary's. With
 * them goes DEVICEID, the soft root's P-256 DeviceID public point (x||y, hex,
 * wirecheck's -deviceid) derived from the UDS just made, by the
 * src/crypto/deviceid_p256.c the firmware links: the seed is HMAC(UDS,
 * TFW_DEVICEID_LABEL), as tfw-boot derives it (boot/tier/cdi_soft.c) and
 * hands it to src/crypto/soft/id_p256.c in handoff v4 beside the CDI.
 *
 * The key pairs and the UDS are made fresh on every run, so no private key
 * and no device secret is in the tree.
 *
 *   cm_image <tfw-boot.bin> <tfw-boot.nm> <trusted-firmware.bin> <outdir> <staged.bin>
 *
 * The store row's run (docs/cortex-m/README.md, the OTA cycle) names a fifth
 * file: the application the cycle stages. It goes into <outdir> as staged.bin,
 * in the layout OTA stages and boot/boot_stage.c's take() reads -- the image,
 * erased padding to whole pages, the trailer last -- signed at version 2 by
 * the key app.bin is signed with, and beside it activate.sig, that key's
 * OTA_ACTIVATE authorization for it (#137). The private key never leaves this
 * process, so the signature is made here and not by the test that sends it.
 *
 *   cm_image <tfw-boot.bin> <tfw-boot.nm> <trusted-firmware.bin> <outdir> <staged.bin> ab
 *
 * The A/B row's run (docs/cortex-m/README.md, two slots) stages the image it
 * runs, so each staged copy carries a short tag after the image's last byte
 * and its measurement differs from app.bin's. staged.bin and activate.sig are
 * as above at version 2, with confirm.sig its OTA_CONFIRM authorization (#231);
 * below-floor.bin, below-floor.sig and below-floor-confirm.sig are the same at
 * version 1, the downgrade tfw-boot refuses once a confirmed 2 raised the
 * floor.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uECC.h"   /* the soft row's signer: src/third_party/micro-ecc */

#include "tfw/boot.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "boot_verify.h"
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

/* ES256, the soft row's (boot/verify/verify_p256_soft.c): boot_cose.c names it
 * in the activate Sig_structure's protected header. */
const uint8_t tfw_boot_cose_alg = 0x26u;

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
    unsigned long a, off;
    size_t        n = strlen(name);

    if (f == NULL) {
        die("cannot open", nm_path);
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "%lx %c %255s", &a, &type, sym) == 3
            && strncmp(sym, name, n) == 0 && (sym[n] == '\0' || sym[n] == '.')) {
            (void)fclose(f);
            /* No `a < TFW_BOOT_START`: on mps2-an500 the start is 0, and the
             * compare is always false (-Wtype-limits). An address below the
             * start wraps past boot_len instead. */
            off = a - (unsigned long)TFW_BOOT_START;
            if (off >= boot_len || len > boot_len - off) {
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

/* The image the OTA cycle stages, and its OTA_ACTIVATE and OTA_CONFIRM
 * authorizations. The
 * trailer is signed as sign_image signs one; it sits at the end of whole
 * pages (OTA_BEGIN refuses any other total, src/core/ota_stage.c), not at the
 * slot's end, because the store holds the image and not the slot. The
 * authorization is P-256 over the SHA-256 of the Sig_structure tfw-boot
 * builds at the reset (boot/boot_cose.c) for this measurement and version,
 * mode 0: activate at the next reset. */
static void stage_image(const uint8_t priv[32], const char *path, const char *outdir, uint32_t version,
                        const char *tag, const char *bin_name, const char *sig_name,
                        const char *confirm_name)
{
    tfw_boot_trailer_t tr;
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    uint8_t            msg[TFW_BOOT_COSE_ACTIVATE_MAX];
    uint8_t            cfm[TFW_BOOT_COSE_CONFIRM_MAX];
    uint8_t            digest[32];
    uint8_t            sig[TFW_BOOT_SIG_LEN];
    uint32_t           len;
    uint32_t           total;
    uint32_t           tag_len = (tag != NULL) ? (uint32_t)strlen(tag) : 0u;

    memset(image, 0xFF, sizeof image);
    len = (uint32_t)read_file(path, image, sizeof image - TFW_BOOT_TRAILER_LEN - tag_len);
    /* The A/B row stages the image it runs: a tag after its last byte is all
     * that tells the two apart, and the measurement covers it. */
    if (tag_len != 0u) {
        memcpy(&image[len], tag, tag_len);
        len += tag_len;
    }
    total = (len + TFW_BOOT_TRAILER_LEN + TFW_OTA_PAGE_MAX - 1u) / TFW_OTA_PAGE_MAX * TFW_OTA_PAGE_MAX;
    if (len == tag_len || total > (uint32_t)TFW_OTA_STORE_STAGE_LEN || total > TFW_BOOT_APP_SIZE) {
        die("the staged application does not fit the store with its trailer", path);
    }

    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = len;
    tr.board_id      = (uint8_t)TFW_BOARD_ID;
    tr.image_flags   = (uint8_t)TFW_IMAGE_FLAGS;
    sha256_span(image, len, tr.measurement);

    tfw_boot_trailer_serialise(&tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, digest);
    if (!uECC_sign(priv, digest, sizeof digest, &raw[TFW_BOOT_TR_SIGNATURE], uECC_secp256r1())) {
        die("uECC_sign failed", NULL);
    }
    memcpy(&image[total - TFW_BOOT_TRAILER_LEN], raw, TFW_BOOT_TRAILER_LEN);
    write_file(outdir, bin_name, image, total);

    tfw_sha256(msg, tfw_boot_cose_activate(msg, tr.measurement, tr.image_version, 0u), digest);
    if (!uECC_sign(priv, digest, sizeof digest, sig, uECC_secp256r1())) {
        die("uECC_sign failed", NULL);
    }
    write_file(outdir, sig_name, sig, sizeof sig);

    /* OTA_CONFIRM's authorization (#231): its own Sig_structure, which
     * tfw-boot verifies at the reset before it settles the trial. The
     * activation's never verifies there, and the trial falls back. */
    tfw_sha256(cfm, tfw_boot_cose_confirm(cfm, tr.measurement, tr.image_version), digest);
    if (!uECC_sign(priv, digest, sizeof digest, sig, uECC_secp256r1())) {
        die("uECC_sign failed", NULL);
    }
    write_file(outdir, confirm_name, sig, sizeof sig);
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

    if (!(argc == 5 || argc == 6 || (argc == 7 && strcmp(argv[6], "ab") == 0))) {
        die("usage", "cm_image <tfw-boot.bin> <tfw-boot.nm> <trusted-firmware.bin> <outdir> [<staged.bin> [ab]]");
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

    /* boot-unprovisioned.bin is the same boot with its UDS slot left erased:
     * the soft root reports NOT_PROVISIONED and the application refuses ATTEST,
     * which is the one row the provisioned boot.bin cannot exercise. */
    patch_slot(argv[2], "tfw_boot_p256_key", pub, sizeof pub);
    write_file(argv[4], "boot-unprovisioned.bin", boot, boot_len);
    patch_slot(argv[2], "uds_slot", uds, sizeof uds);
    write_file(argv[4], "boot.bin", boot, boot_len);

    sign_image(priv, -1);
    write_file(argv[4], "app.bin", image, sizeof image);
    sign_image(priv, (long)(app_len / 2u));
    write_file(argv[4], "app-tampered.bin", image, sizeof image);
    sign_image(other_priv, -1);
    write_file(argv[4], "app-otherkey.bin", image, sizeof image);
    /* Version 2 is above app.bin's 1: tfw-boot's rollback check. The A/B row
     * adds a version 1 image of its own for the floor a confirmed 2 leaves. */
    if (argc == 6) {
        stage_image(priv, argv[5], argv[4], 2u, NULL, "staged.bin", "activate.sig", "confirm.sig");
    }
    if (argc == 7) {
        stage_image(priv, argv[5], argv[4], 2u, "tfw A/B update 2", "staged.bin", "activate.sig",
                    "confirm.sig");
        stage_image(priv, argv[5], argv[4], 1u, "tfw A/B update 1", "below-floor.bin", "below-floor.sig",
                    "below-floor-confirm.sig");
    }

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
                         (unsigned long)TFW_BOOT_START, (unsigned long)TFW_CM_LOAD_APP_AT,
                         (unsigned long)(TFW_CM_LOAD_PLATFORM_AT + TFW_BOOT_EE_BASE));
    for (i = 0u; i < sizeof deviceid; i++) {
        n += (size_t)snprintf(&env[n], sizeof env - n, "%02x", deviceid[i]);
    }
    (void)snprintf(&env[n], sizeof env - n, "\n");
    write_file(argv[4], "addrs.env", (const uint8_t *)env, strlen(env));
    return 0;
}
