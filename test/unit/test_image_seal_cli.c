/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image-seal command line, tools/image-seal/main.c, as make image-tbs and
 * make image-seal run it: the usage, the image_version and image_flags
 * bounds, the file-size refusals, and the files it writes, which are seal.c's
 * own output (test_image_seal.c tests seal.c). main.c is compiled into this
 * suite unchanged, its main renamed, and each case calls it with a built argv
 * over files in a fresh mkdtemp directory.
 */
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"

/* By its path from here: -Isrc would find the firmware's src/main.c first. */
#define main tfw_image_seal_main
#include "../../tools/image-seal/main.c"
#undef main

/* verify_p256_soft.c, linked as for test_image_seal, reads the key slot;
 * nothing here verifies, so the slot stays zero. */
uint8_t tfw_boot_p256_key[64];

#define CLI_IMAGE_LEN 300u
#define CLI_PATH_MAX  256u

static char cli_dir[CLI_PATH_MAX];
static char p_img[CLI_PATH_MAX], p_tbs[CLI_PATH_MAX], p_sig[CLI_PATH_MAX];
static char p_stream[CLI_PATH_MAX], p_region[CLI_PATH_MAX], p_env[CLI_PATH_MAX];
static char p_none[CLI_PATH_MAX], p_err[CLI_PATH_MAX];

static uint8_t cli_image[CLI_IMAGE_LEN];
static uint8_t cli_sig[TFW_BOOT_SIG_LEN];
static uint8_t cli_want[TFW_BOOT_APP_SIZE];
static uint8_t cli_got[TFW_BOOT_APP_SIZE + 1u];

static void cli_path(char *out, const char *name)
{
    int n = snprintf(out, CLI_PATH_MAX, "%s/%s", cli_dir, name);

    TEST_ASSERT_TRUE(n > 0 && (size_t)n < CLI_PATH_MAX);
}

static void cli_put(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(n, fwrite(buf, 1u, n, f));
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

/* The file's length, or -1 where there is no file. */
static long cli_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  n = 0;

    if (!f) {
        return -1;
    }
    while (fgetc(f) != EOF) {
        n++;
    }
    fclose(f);
    return n;
}

static size_t cli_read(const char *path, uint8_t *buf, size_t cap)
{
    FILE  *f = fopen(path, "rb");
    size_t n;

    TEST_ASSERT_NOT_NULL(f);
    n = fread(buf, 1u, cap, f);
    fclose(f);
    return n;
}

/* image-seal with `n` arguments after its name, its stderr in p_err. */
static int cli_run(int n, const char *const *args)
{
    static char store[10][CLI_PATH_MAX];
    char       *argv[11];
    int         i, rc, saved, fd;

    TEST_ASSERT_TRUE(n >= 0 && n < 10);
    snprintf(store[0], CLI_PATH_MAX, "image-seal");
    argv[0] = store[0];
    for (i = 0; i < n; i++) {
        snprintf(store[i + 1], CLI_PATH_MAX, "%s", args[i]);
        argv[i + 1] = store[i + 1];
    }
    argv[n + 1] = NULL;
    fflush(stderr);
    saved = dup(2);
    fd    = open(p_err, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_TRUE(saved >= 0 && fd >= 0);
    TEST_ASSERT_EQUAL_INT(2, dup2(fd, 2));
    close(fd);
    rc = tfw_image_seal_main(n + 1, argv);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    return rc;
}

/* Whether the last run's stderr holds `text`. */
static int cli_said(const char *text)
{
    static char said[1024];
    size_t      n;
    FILE       *f = fopen(p_err, "rb");

    TEST_ASSERT_NOT_NULL(f);
    n = fread(said, 1u, sizeof said - 1u, f);
    fclose(f);
    said[n] = '\0';
    return strstr(said, text) != NULL;
}

static int cli_tbs(const char *version, const char *flags)
{
    const char *const a[] = { "tbs", p_img, version, flags, p_tbs };

    return cli_run(5, a);
}

static int cli_seal(void)
{
    const char *const a[] = { "seal", p_img, p_tbs, p_sig, p_stream, p_region };

    return cli_run(6, a);
}

/* The span seal.c makes of the image, written where `seal` reads it. */
static void cli_good_tbs(uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN])
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(cli_image, CLI_IMAGE_LEN, 7u, 0u, tbs));
    cli_put(p_tbs, tbs, TFW_BOOT_TR_SIGNED_LEN);
}

static void cli_assert_no_seal_output(void)
{
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_stream));
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_region));
}

void setUp(void)
{
    size_t i;

    snprintf(cli_dir, sizeof cli_dir, "/tmp/tfw-image-seal-cli-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(cli_dir));
    cli_path(p_img, "image.bin");
    cli_path(p_tbs, "image.tbs");
    cli_path(p_sig, "image.sig");
    cli_path(p_stream, "image.stream");
    cli_path(p_region, "image.region");
    cli_path(p_env, "image.suit");
    cli_path(p_none, "absent.bin");
    cli_path(p_err, "stderr.txt");
    for (i = 0u; i < sizeof cli_image; i++) {
        cli_image[i] = (uint8_t)(i * 7u + 1u);
    }
    for (i = 0u; i < sizeof cli_sig; i++) {
        cli_sig[i] = (uint8_t)(0x5Au ^ i);
    }
    cli_put(p_img, cli_image, sizeof cli_image);
    cli_put(p_sig, cli_sig, sizeof cli_sig);
}

void tearDown(void)
{
    unlink(p_img);
    unlink(p_tbs);
    unlink(p_sig);
    unlink(p_stream);
    unlink(p_region);
    unlink(p_env);
    unlink(p_err);
    rmdir(cli_dir);
}

/* (1) Any other argument count, or another verb: the usage, 2, no file. */
void test_a_wrong_argument_count_or_verb_is_the_usage_and_writes_nothing(void)
{
    const char *const verb_only[]  = { "tbs" };
    const char *const tbs_short[]  = { "tbs", p_img, "1", "0" };
    const char *const tbs_long[]   = { "tbs", p_img, "1", "0", p_tbs, "x" };
    const char *const seal_short[] = { "seal", p_img, p_tbs, p_sig, p_stream };
    const char *const seal_long[]  = { "seal", p_img, p_tbs, p_sig, p_stream, p_region, p_env, "x" };
    const char *const other[]      = { "sign", p_img, "1", "0", p_tbs };
    const char *const seal_as_tbs[] = { "seal", p_img, "1", "0", p_tbs };
    const char *const tbs_as_seal[] = { "tbs", p_img, p_tbs, p_sig, p_stream, p_region };

    TEST_ASSERT_EQUAL_INT(2, cli_run(0, NULL));
    TEST_ASSERT_EQUAL_INT(2, cli_run(1, verb_only));
    TEST_ASSERT_EQUAL_INT(2, cli_run(4, tbs_short));
    TEST_ASSERT_EQUAL_INT(2, cli_run(6, tbs_long));
    TEST_ASSERT_EQUAL_INT(2, cli_run(5, seal_short));
    TEST_ASSERT_EQUAL_INT(2, cli_run(8, seal_long));
    TEST_ASSERT_EQUAL_INT(2, cli_run(5, other));
    TEST_ASSERT_EQUAL_INT(2, cli_run(5, seal_as_tbs));
    TEST_ASSERT_EQUAL_INT(2, cli_run(6, tbs_as_seal));
    TEST_ASSERT_TRUE(cli_said("usage: image-seal tbs"));
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_tbs));
    cli_assert_no_seal_output();
}

/* (2) image_version is 1..4294967295 and a whole number; image_flags 0..255.
 * The command line's own bound refuses each, before seal.c sees it: seal.c
 * refuses version 0 too, so the message is what tells the two apart. */
#define CLI_VERSION_BOUND "image_version must be 1..4294967295"
#define CLI_FLAGS_BOUND   "image_flags must be 0..255"
#define CLI_SEAL_UNSUPPORTED "image flag trailer v1 does not define"

void test_a_version_or_flags_out_of_bounds_is_refused_and_writes_no_span(void)
{
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("0", "0"));
    TEST_ASSERT_TRUE(cli_said(CLI_VERSION_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("4294967296", "0"));
    TEST_ASSERT_TRUE(cli_said(CLI_VERSION_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("1x", "0"));
    TEST_ASSERT_TRUE(cli_said(CLI_VERSION_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("", "0"));
    TEST_ASSERT_TRUE(cli_said(CLI_VERSION_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("1", "256"));
    TEST_ASSERT_TRUE(cli_said(CLI_FLAGS_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("1", "-1"));
    TEST_ASSERT_TRUE(cli_said(CLI_FLAGS_BOUND));
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("1", ""));
    TEST_ASSERT_TRUE(cli_said(CLI_FLAGS_BOUND));
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_tbs));
}

void test_the_version_and_flags_bounds_themselves_are_spanned(void)
{
    TEST_ASSERT_EQUAL_INT(0, cli_tbs("1", "0"));
    TEST_ASSERT_EQUAL_INT32((long)TFW_BOOT_TR_SIGNED_LEN, cli_size(p_tbs));
    TEST_ASSERT_EQUAL_INT(0, cli_tbs("4294967295", "0"));
    TEST_ASSERT_EQUAL_INT32((long)TFW_BOOT_TR_SIGNED_LEN, cli_size(p_tbs));
}

/* 255 is in the command line's bound; seal.c refuses a flag trailer v1 does
 * not define, so what tbs returns is seal.c's verdict, not a cast's. Both
 * refusals return 1 and write no span, so the message is what tells them
 * apart: never the bound's, and seal.c's where seal.c refuses. */
void test_flags_255_pass_the_bound_and_reach_seal_c(void)
{
    uint8_t      tbs[TFW_BOOT_TR_SIGNED_LEN];
    tfw_status_t st = tfw_seal_tbs(cli_image, CLI_IMAGE_LEN, 1u, 0xFFu, tbs);

    TEST_ASSERT_EQUAL_INT(st == TFW_OK ? 0 : 1, cli_tbs("1", "255"));
    TEST_ASSERT_FALSE(cli_said(CLI_FLAGS_BOUND));
    if (st == TFW_ERR_UNSUPPORTED) {
        TEST_ASSERT_TRUE(cli_said(CLI_SEAL_UNSUPPORTED));
    }
    TEST_ASSERT_EQUAL_INT32(st == TFW_OK ? (long)TFW_BOOT_TR_SIGNED_LEN : -1L, cli_size(p_tbs));
}

/* (3) The span is seal.c's, byte for byte. */
void test_tbs_writes_seal_c_s_span_of_the_image(void)
{
    uint8_t want[TFW_BOOT_TR_SIGNED_LEN];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(cli_image, CLI_IMAGE_LEN, 0x01020304u, 0u, want));
    TEST_ASSERT_EQUAL_INT(0, cli_tbs("0x01020304", "0"));
    TEST_ASSERT_EQUAL_INT32((long)TFW_BOOT_TR_SIGNED_LEN, cli_size(p_tbs));
    TEST_ASSERT_EQUAL_size_t(TFW_BOOT_TR_SIGNED_LEN, cli_read(p_tbs, cli_got, sizeof cli_got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, cli_got, TFW_BOOT_TR_SIGNED_LEN);
}

/* (4) A missing or empty image, for tbs and for seal. */
void test_a_missing_or_empty_image_is_refused(void)
{
    uint8_t           tbs[TFW_BOOT_TR_SIGNED_LEN];
    const char *const tbs_none[]  = { "tbs", p_none, "1", "0", p_tbs };
    const char *const seal_none[] = { "seal", p_none, p_tbs, p_sig, p_stream, p_region };

    cli_good_tbs(tbs);
    unlink(p_tbs);
    TEST_ASSERT_EQUAL_INT(1, cli_run(5, tbs_none));
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_tbs));
    cli_put(p_img, cli_image, 0u);
    TEST_ASSERT_EQUAL_INT(1, cli_tbs("1", "0"));
    TEST_ASSERT_EQUAL_INT32(-1, cli_size(p_tbs));

    cli_good_tbs(tbs);
    TEST_ASSERT_EQUAL_INT(1, cli_run(6, seal_none));
    cli_assert_no_seal_output();
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
}

/* (5) The span and the signature are exactly their sizes. */
void test_a_span_or_signature_of_the_wrong_size_is_refused(void)
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN + 1u];

    cli_good_tbs(tbs);
    tbs[TFW_BOOT_TR_SIGNED_LEN] = 0u;
    cli_put(p_tbs, tbs, TFW_BOOT_TR_SIGNED_LEN - 1u);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
    cli_put(p_tbs, tbs, TFW_BOOT_TR_SIGNED_LEN + 1u);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();

    cli_put(p_tbs, tbs, TFW_BOOT_TR_SIGNED_LEN);
    cli_put(p_sig, cli_sig, TFW_BOOT_SIG_LEN - 1u);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
    cli_put(p_sig, cli_sig, 0u);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
    unlink(p_sig);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
}

/* A span seal.c refuses (here, of another image) is refused, nothing staged. */
void test_a_span_seal_c_refuses_returns_1_and_writes_nothing(void)
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN];

    cli_good_tbs(tbs);
    cli_image[0] ^= 0x01u;
    cli_put(p_img, cli_image, sizeof cli_image);
    TEST_ASSERT_EQUAL_INT(1, cli_seal());
    cli_assert_no_seal_output();
}

/* (6) The stream and the region are seal.c's, byte for byte. */
void test_seal_writes_seal_c_s_stream_and_region(void)
{
    uint8_t  tbs[TFW_BOOT_TR_SIGNED_LEN];
    uint8_t  trailer[TFW_BOOT_TRAILER_LEN];
    uint8_t  env[1] = { 0u };
    uint32_t total  = tfw_seal_stream_len(CLI_IMAGE_LEN, 0u);

    cli_good_tbs(tbs);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_trailer(cli_image, CLI_IMAGE_LEN, tbs, cli_sig, trailer));
    TEST_ASSERT_EQUAL_INT(0, cli_seal());

    TEST_ASSERT_EQUAL_INT32((long)total, cli_size(p_stream));
    tfw_seal_stream(cli_image, CLI_IMAGE_LEN, env, 0u, trailer, cli_want);
    TEST_ASSERT_EQUAL_size_t(total, cli_read(p_stream, cli_got, sizeof cli_got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cli_want, cli_got, total);

    TEST_ASSERT_EQUAL_INT32((long)TFW_BOOT_APP_SIZE, cli_size(p_region));
    tfw_seal_region(cli_image, CLI_IMAGE_LEN, trailer, cli_want);
    TEST_ASSERT_EQUAL_size_t(TFW_BOOT_APP_SIZE, cli_read(p_region, cli_got, sizeof cli_got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cli_want, cli_got, TFW_BOOT_APP_SIZE);
}

/* An empty envelope is no envelope: refused, nothing staged. */
void test_an_empty_envelope_is_refused_and_writes_nothing(void)
{
    uint8_t           tbs[TFW_BOOT_TR_SIGNED_LEN];
    const char *const a[] = { "seal", p_img, p_tbs, p_sig, p_stream, p_region, p_env };

    cli_good_tbs(tbs);
    cli_put(p_env, tbs, 0u);
    TEST_ASSERT_EQUAL_INT(1, cli_run(7, a));
    cli_assert_no_seal_output();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_wrong_argument_count_or_verb_is_the_usage_and_writes_nothing);
    RUN_TEST(test_a_version_or_flags_out_of_bounds_is_refused_and_writes_no_span);
    RUN_TEST(test_the_version_and_flags_bounds_themselves_are_spanned);
    RUN_TEST(test_flags_255_pass_the_bound_and_reach_seal_c);
    RUN_TEST(test_tbs_writes_seal_c_s_span_of_the_image);
    RUN_TEST(test_a_missing_or_empty_image_is_refused);
    RUN_TEST(test_a_span_or_signature_of_the_wrong_size_is_refused);
    RUN_TEST(test_a_span_seal_c_refuses_returns_1_and_writes_nothing);
    RUN_TEST(test_seal_writes_seal_c_s_stream_and_region);
    RUN_TEST(test_an_empty_envelope_is_refused_and_writes_nothing);
    return UNITY_END();
}
