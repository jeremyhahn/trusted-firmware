/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * image-seal: the build's trailer v1 sealer for tfw-boot rows (seal.h).
 *
 *   image-seal tbs <image.bin> <image_version> <image_flags> <tbs.out>
 *       the trailer's 48-byte signed span, the bytes go-xkms signs with ES256
 *   image-seal seal <image.bin> <tbs> <sig.bin> <stream.out> <region.out> [<envelope.suit>]
 *       the signed trailer around go-xkms's 64-byte r||s signature, laid out
 *       as the OTA stream and as the application region; the SUIT envelope
 *       suit-emit assembled, if named, goes in the stream after the image
 *
 * Built per board: byte 6 is this build's TFW_BOARD_ID (the board's profile),
 * the id the board's tfw-boot compares against.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seal.h"
#include "suit.h"
#include "tfw/config.h"

static size_t slurp(const char *path, uint8_t *buf, size_t cap)
{
    FILE  *f = fopen(path, "rb");
    size_t n;

    if (!f) {
        fprintf(stderr, "image-seal: cannot open %s\n", path);
        return 0u;
    }
    n = fread(buf, 1u, cap, f);
    if (!feof(f) && fgetc(f) != EOF) {
        fprintf(stderr, "image-seal: %s is larger than %zu bytes\n", path, cap);
        n = 0u;
    }
    fclose(f);
    return n;
}

static int spill(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(buf, 1u, n, f) != n) {
        fprintf(stderr, "image-seal: cannot write %s\n", path);
        if (f) {
            fclose(f);
        }
        return 1;
    }
    return fclose(f) == 0 ? 0 : 1;
}

/* Exactly `n` bytes, or a message naming the file. */
static int slurp_exact(const char *path, uint8_t *buf, size_t n, const char *what)
{
    if (slurp(path, buf, n) != n) {
        fprintf(stderr, "image-seal: %s must be %s, exactly %zu bytes\n", path, what, n);
        return 1;
    }
    return 0;
}

static int refused(tfw_status_t st)
{
    switch (st) {
    case TFW_ERR_RANGE:
        fprintf(stderr, "image-seal: the image is empty or reaches the trailer: at most %lu bytes\n",
                (unsigned long)TFW_BOOT_MAX_IMAGE_LEN);
        break;
    case TFW_ERR_UNSUPPORTED:
        fprintf(stderr, "image-seal: the span names another board (this is 0x%02X) or an\n"
                        "  image flag trailer v1 does not define\n", (unsigned)TFW_BOARD_ID);
        break;
    case TFW_ERR_CRYPTO:
        fprintf(stderr, "image-seal: the span is not this image's: its length or measurement\n"
                        "  differs, or its image_version is zero. Emit it again from this image.\n");
        break;
    default:
        fprintf(stderr, "image-seal: refused, status %d\n", (int)st);
        break;
    }
    return 1;
}

/* The image file, up to the region; tfw_seal_tbs bounds it by the trailer. */
static uint8_t image[TFW_BOOT_APP_SIZE];

static int cmd_tbs(char **argv)
{
    uint8_t       tbs[TFW_BOOT_TR_SIGNED_LEN];
    char         *end_v, *end_f;
    unsigned long version = strtoul(argv[3], &end_v, 0);
    unsigned long flags   = strtoul(argv[4], &end_f, 0);
    size_t        len;
    tfw_status_t  st;

    if (*argv[3] == '\0' || *end_v != '\0' || version == 0ul || version > 0xFFFFFFFFul) {
        fprintf(stderr, "image-seal: image_version must be 1..4294967295\n");
        return 1;
    }
    if (*argv[4] == '\0' || *end_f != '\0' || flags > 0xFFul) {
        fprintf(stderr, "image-seal: image_flags must be 0..255\n");
        return 1;
    }
    len = slurp(argv[2], image, sizeof image);
    if (len == 0u) {
        return 1;
    }
    st = tfw_seal_tbs(image, (uint32_t)len, (uint32_t)version, (uint8_t)flags, tbs);
    if (st != TFW_OK) {
        return refused(st);
    }
    return spill(argv[5], tbs, sizeof tbs);
}

static int cmd_seal(int argc, char **argv)
{
    static uint8_t out[TFW_BOOT_APP_SIZE];
    uint8_t        tbs[TFW_BOOT_TR_SIGNED_LEN], sig[TFW_BOOT_SIG_LEN];
    uint8_t        trailer[TFW_BOOT_TRAILER_LEN];
    uint8_t        env[TFW_SUIT_ENVELOPE_MAX];
    size_t         len = slurp(argv[2], image, sizeof image);
    size_t         env_len = 0u;
    uint32_t       total;
    tfw_status_t   st;

    if (len == 0u || slurp_exact(argv[3], tbs, sizeof tbs, "the span `tbs` wrote") ||
        slurp_exact(argv[4], sig, sizeof sig, "a raw r||s ES256 signature")) {
        return 1;
    }
    /* The SUIT envelope, where the board checks one at OTA_ACTIVATE (ruling
     * 121): no larger than the device reads, and never empty, which the
     * device refuses as no envelope at all. */
    if (argc == 8) {
        env_len = slurp(argv[7], env, sizeof env);
        if (env_len == 0u) {
            fprintf(stderr, "image-seal: %s is no SUIT envelope\n", argv[7]);
            return 1;
        }
    }
    st = tfw_seal_trailer(image, (uint32_t)len, tbs, sig, trailer);
    if (st != TFW_OK) {
        return refused(st);
    }
    /* The stager takes at most its staging area. The image is within the
     * region, and on every tfw-boot row the area holds the region's stream;
     * checked rather than assumed, as a profile can change one and not the
     * other, and an envelope can push the stream a page further. */
    total = tfw_seal_stream_len((uint32_t)len, (uint16_t)env_len);
    if (total > (uint32_t)TFW_OTA_STORE_STAGE_LEN || total > (uint32_t)sizeof out) {
        fprintf(stderr, "image-seal: the stream is %lu bytes, past the %lu-byte staging area\n",
                (unsigned long)total, (unsigned long)TFW_OTA_STORE_STAGE_LEN);
        return 1;
    }
    tfw_seal_stream(image, (uint32_t)len, env, (uint16_t)env_len, trailer, out);
    if (spill(argv[5], out, total)) {
        return 1;
    }
    tfw_seal_region(image, (uint32_t)len, trailer, out);
    return spill(argv[6], out, TFW_BOOT_APP_SIZE);
}

int main(int argc, char **argv)
{
    if (argc == 6 && strcmp(argv[1], "tbs") == 0) {
        return cmd_tbs(argv);
    }
    if ((argc == 7 || argc == 8) && strcmp(argv[1], "seal") == 0) {
        return cmd_seal(argc, argv);
    }
    fprintf(stderr, "usage: image-seal tbs <image.bin> <image_version> <image_flags> <tbs.out>\n"
                    "       image-seal seal <image.bin> <tbs> <sig.bin> <stream.out> <region.out>"
                    " [<envelope.suit>]\n");
    return 2;
}
