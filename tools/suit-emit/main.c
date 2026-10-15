/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * suit-emit: the build's SUIT manifest emitter (issue #78; emit.h).
 *
 *   suit-emit tbs <image.bin> <seq> <manifest.out> <tbs.out>
 *       the manifest for this board and image, and the bytes go-xkms signs
 *   suit-emit envelope <manifest> <sig.bin> <envelope.out>
 *       the envelope, around go-xkms's 64-byte r||s signature over the TBS
 *
 * Built per board: the vendor and class ids are this build's
 * TFW_SUIT_VENDOR_ID and TFW_SUIT_CLASS_ID (include/tfw/config.h and the
 * board's profile), the ids the device's gate compares against.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emit.h"
#include "sha256.h"
#include "tfw/config.h"

#define IMAGE_MAX (1024ul * 1024ul)
#define OUT_MAX   512u

static size_t slurp(const char *path, uint8_t *buf, size_t cap)
{
    FILE  *f = fopen(path, "rb");
    size_t n;

    if (!f) {
        fprintf(stderr, "suit-emit: cannot open %s\n", path);
        return 0u;
    }
    n = fread(buf, 1u, cap, f);
    if (!feof(f) && fgetc(f) != EOF) {
        fprintf(stderr, "suit-emit: %s is larger than %zu bytes\n", path, cap);
        n = 0u;
    }
    fclose(f);
    return n;
}

static int spill(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(buf, 1u, n, f) != n) {
        fprintf(stderr, "suit-emit: cannot write %s\n", path);
        if (f) {
            fclose(f);
        }
        return 1;
    }
    return fclose(f) == 0 ? 0 : 1;
}

static int cmd_tbs(char **argv)
{
    static const uint8_t vendor[16]   = TFW_SUIT_VENDOR_ID;
    static const uint8_t class_id[16] = TFW_SUIT_CLASS_ID;
    static const uint8_t unbound[16]  = { 0u };
    static uint8_t       image[IMAGE_MAX];
    uint8_t              digest[32], man[OUT_MAX], tbs[OUT_MAX];
    tfw_sha256_t         ctx;
    char                *end;
    unsigned long        seq = strtoul(argv[3], &end, 0);
    size_t               len = slurp(argv[2], image, sizeof image);
    size_t               off, mn, tn;

    if (memcmp(class_id, unbound, sizeof unbound) == 0) {
        fprintf(stderr, "suit-emit: this board's profile states no TFW_SUIT_CLASS_ID;\n"
                        "  its gate refuses every manifest, so none is emitted\n");
        return 1;
    }
    if (*argv[3] == '\0' || *end != '\0' || seq == 0ul || seq > 0xFFFFFFFFul) {
        fprintf(stderr, "suit-emit: sequence must be 1..4294967295, the trailer's image_version\n");
        return 1;
    }
    if (len == 0u) {
        return 1;
    }
    tfw_sha256_init(&ctx);
    for (off = 0u; off < len; off += 4096u) {
        size_t k = len - off < 4096u ? len - off : 4096u;
        tfw_sha256_update(&ctx, &image[off], (uint16_t)k);
    }
    tfw_sha256_final(&ctx, digest);

    mn = tfw_suit_emit_manifest(man, sizeof man, digest, (uint32_t)len,
                                (uint32_t)seq, vendor, class_id);
    tn = mn ? tfw_suit_emit_tbs(tbs, sizeof tbs, man, mn) : 0u;
    if (tn == 0u) {
        fprintf(stderr, "suit-emit: encoding overflowed\n");
        return 1;
    }
    return spill(argv[4], man, mn) || spill(argv[5], tbs, tn);
}

static int cmd_envelope(char **argv)
{
    uint8_t man[OUT_MAX], sig[65], env[OUT_MAX];
    size_t  mn = slurp(argv[2], man, sizeof man);
    size_t  sn = slurp(argv[3], sig, sizeof sig);
    size_t  en;

    if (sn != 64u) {
        fprintf(stderr, "suit-emit: %s must be exactly 64 bytes, r||s\n", argv[3]);
        return 1;
    }
    en = mn ? tfw_suit_emit_envelope(env, sizeof env, man, mn, sig) : 0u;
    if (en == 0u) {
        fprintf(stderr, "suit-emit: encoding overflowed\n");
        return 1;
    }
    return spill(argv[4], env, en);
}

int main(int argc, char **argv)
{
    if (argc == 6 && strcmp(argv[1], "tbs") == 0) {
        return cmd_tbs(argv);
    }
    if (argc == 5 && strcmp(argv[1], "envelope") == 0) {
        return cmd_envelope(argv);
    }
    fprintf(stderr, "usage: suit-emit tbs <image.bin> <seq> <manifest.out> <tbs.out>\n"
                    "       suit-emit envelope <manifest> <sig.bin> <envelope.out>\n");
    return 2;
}
