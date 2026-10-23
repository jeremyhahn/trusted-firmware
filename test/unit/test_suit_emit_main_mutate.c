/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for suit-emit's command line, tools/suit-emit/main.c (#163).
 *
 * The unit is included with its main() renamed, so its file handling (slurp,
 * spill) and both commands run in-process against files in a fresh temporary
 * directory. What the encoder writes is test_suit_emit's; this test checks that
 * main.c hands it the right inputs -- the digest of the whole image, its size,
 * the sequence, the board's ids -- and writes exactly what it returned, and
 * that every refusal is a refusal with its exit code.
 *
 * Built twice: against the ATmega2560's profile, which states a SUIT class id,
 * and against the ATtiny85's, which states none and so must emit nothing.
 */
#define _XOPEN_SOURCE 700             /* mkdtemp, ftruncate, dup: before any header */
#define main suit_emit_main
#include "../../tools/suit-emit/main.c"
#undef main

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unity.h"

static const uint8_t board_class[16] = TFW_SUIT_CLASS_ID;
static const uint8_t board_vendor[16] = TFW_SUIT_VENDOR_ID;

static char dir[64];
static char p_img[96], p_man[96], p_tbs[96], p_sig[96], p_env[96], p_bad[96];
static char errbuf[1024];

static int bound(void)
{
    static const uint8_t zero[16] = { 0u };
    return memcmp(board_class, zero, sizeof zero) != 0;
}

void setUp(void)
{
    snprintf(dir, sizeof dir, "/tmp/suitemit.XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(dir));
    snprintf(p_img, sizeof p_img, "%s/img.bin", dir);
    snprintf(p_man, sizeof p_man, "%s/man.cbor", dir);
    snprintf(p_tbs, sizeof p_tbs, "%s/tbs.cbor", dir);
    snprintf(p_sig, sizeof p_sig, "%s/sig.bin", dir);
    snprintf(p_env, sizeof p_env, "%s/env.cbor", dir);
    snprintf(p_bad, sizeof p_bad, "%s/no/such/file", dir);
}

void tearDown(void)
{
    unlink(p_img); unlink(p_man); unlink(p_tbs); unlink(p_sig); unlink(p_env);
    rmdir(dir);
}

static void put(const char *path, const uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_UINT(n, fwrite(b, 1u, n, f));
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

static size_t get(const char *path, uint8_t *b, size_t cap)
{
    FILE  *f = fopen(path, "rb");
    size_t n;
    TEST_ASSERT_NOT_NULL(f);
    n = fread(b, 1u, cap, f);
    fclose(f);
    return n;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* suit_emit_main with its stderr kept in errbuf. */
static int run_args(int argc, char **argv)
{
    int     p[2], saved, rc;
    ssize_t r;

    TEST_ASSERT_EQUAL_INT(0, pipe(p));
    fflush(stderr);
    saved = dup(2);
    dup2(p[1], 2);
    rc = suit_emit_main(argc, argv);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(p[1]);
    r = read(p[0], errbuf, sizeof errbuf - 1u);
    errbuf[r > 0 ? r : 0] = '\0';
    close(p[0]);
    return rc;
}

/* argv strings are writable in a real main(); give it writable copies. */
static char av[7][128];

static int run_strs(int argc, const char *const *s)
{
    char *argv[7];
    int   i;

    for (i = 0; i < argc; i++) {
        snprintf(av[i], sizeof av[i], "%s", s[i]);
        argv[i] = av[i];
    }
    argv[argc] = NULL;
    return run_args(argc, argv);
}

static int tbs(const char *img, const char *seq, const char *man, const char *tb)
{
    const char *const s[] = { "suit-emit", "tbs", img, seq, man, tb };
    return run_strs(6, s);
}

static int envelope(const char *man, const char *sig, const char *env)
{
    const char *const s[] = { "suit-emit", "envelope", man, sig, env };
    return run_strs(5, s);
}

static uint8_t img[3u * 4096u + 7u];

static void fill_image(size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { img[i] = (uint8_t)(i * 7u + (i >> 8)); }
}

/* What tbs must have written for these image bytes and sequence. */
static void expect_tbs(const uint8_t *image, size_t n, uint32_t seq)
{
    uint8_t      d[32], man[OUT_MAX], tb[OUT_MAX], got[OUT_MAX + 1u];
    tfw_sha256_t c;
    size_t       off, mn, tn;

    tfw_sha256_init(&c);
    for (off = 0; off < n; off += 1000u) {
        tfw_sha256_update(&c, image + off, (uint16_t)(n - off < 1000u ? n - off : 1000u));
    }
    tfw_sha256_final(&c, d);
    mn = tfw_suit_emit_manifest(man, sizeof man, d, (uint32_t)n, seq, board_vendor, board_class);
    tn = tfw_suit_emit_tbs(tb, sizeof tb, man, mn);
    TEST_ASSERT_NOT_EQUAL(0u, mn);
    TEST_ASSERT_NOT_EQUAL(0u, tn);
    TEST_ASSERT_EQUAL_UINT(mn, get(p_man, got, sizeof got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(man, got, mn);
    TEST_ASSERT_EQUAL_UINT(tn, get(p_tbs, got, sizeof got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tb, got, tn);
}

/* ---- usage ------------------------------------------------------------- */

static void test_a_wrong_command_line_is_usage_and_2(void)
{
    char *a[] = { "suit-emit", "tbs", "a", "b", "c", "d", "e", NULL };
    char *b[] = { "suit-emit", "envelope", "a", "b", "c", "d", NULL };
    char *c[] = { "suit-emit", "tb", "a", "b", "c", "d", NULL };
    char *d[] = { "suit-emit", "envelopes", "a", "b", "c", NULL };
    char *e[] = { "suit-emit", "tbs", "a", "b", "c", NULL };

    TEST_ASSERT_EQUAL_INT(2, run_args(7, a));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "usage: suit-emit tbs <image.bin> <seq> <manifest.out> <tbs.out>\n"));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "suit-emit envelope <manifest> <sig.bin> <envelope.out>\n"));
    TEST_ASSERT_EQUAL_INT(2, run_args(6, b));
    TEST_ASSERT_EQUAL_INT(2, run_args(6, c));
    TEST_ASSERT_EQUAL_INT(2, run_args(5, d));
    TEST_ASSERT_EQUAL_INT(2, run_args(5, e));
}

/* ---- tbs --------------------------------------------------------------- */

static void test_tbs_on_a_board_with_no_class_id_emits_nothing(void)
{
    fill_image(100u);
    put(p_img, img, 100u);
    if (bound()) {
        TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "1", p_man, p_tbs));
        TEST_ASSERT_EQUAL_STRING("", errbuf);
        return;
    }
    TEST_ASSERT_EQUAL_INT(1, tbs(p_img, "1", p_man, p_tbs));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "states no TFW_SUIT_CLASS_ID"));
    TEST_ASSERT_FALSE(exists(p_man));
    TEST_ASSERT_FALSE(exists(p_tbs));
}

static void test_tbs_digests_every_chunk_of_the_image(void)
{
    if (!bound()) { return; }
    fill_image(sizeof img);
    put(p_img, img, sizeof img);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "7", p_man, p_tbs));
    expect_tbs(img, sizeof img, 7u);
}

static void test_tbs_digests_an_image_of_exactly_one_chunk(void)
{
    if (!bound()) { return; }
    fill_image(4096u);
    put(p_img, img, 4096u);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "1", p_man, p_tbs));
    expect_tbs(img, 4096u, 1u);
}

static void test_tbs_takes_a_sequence_in_any_c_base_up_to_u32_max(void)
{
    if (!bound()) { return; }
    fill_image(33u);
    put(p_img, img, 33u);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "0x10", p_man, p_tbs));
    expect_tbs(img, 33u, 16u);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "4294967295", p_man, p_tbs));
    expect_tbs(img, 33u, 0xFFFFFFFFu);
}

static void test_tbs_refuses_a_sequence_out_of_range_or_malformed(void)
{
    static const char *bad[] = { "0", "", "7x", "4294967296", "-" };
    size_t i;

    if (!bound()) { return; }
    fill_image(33u);
    put(p_img, img, 33u);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, tbs(p_img, bad[i], p_man, p_tbs), bad[i]);
        TEST_ASSERT_NOT_NULL(strstr(errbuf, "sequence must be 1..4294967295"));
        TEST_ASSERT_FALSE(exists(p_man));
    }
}

static void test_tbs_refuses_a_missing_or_empty_image(void)
{
    if (!bound()) { return; }
    TEST_ASSERT_EQUAL_INT(1, tbs(p_bad, "1", p_man, p_tbs));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "suit-emit: cannot open "));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, p_bad));
    put(p_img, img, 0u);
    TEST_ASSERT_EQUAL_INT(1, tbs(p_img, "1", p_man, p_tbs));
    TEST_ASSERT_FALSE(exists(p_man));
}

static void test_tbs_takes_an_image_of_exactly_the_cap_and_refuses_one_more(void)
{
    int fd;

    if (!bound()) { return; }
    fd = open(p_img, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(fd, (off_t)IMAGE_MAX));
    close(fd);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "1", p_man, p_tbs));
    TEST_ASSERT_EQUAL_STRING("", errbuf);

    fd = open(p_img, O_WRONLY);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(fd, (off_t)IMAGE_MAX + 1));
    close(fd);
    TEST_ASSERT_EQUAL_INT(1, tbs(p_img, "1", p_man, p_tbs));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "is larger than 16777216 bytes"));
}

static void test_tbs_fails_when_either_output_cannot_be_written(void)
{
    if (!bound()) { return; }
    fill_image(50u);
    put(p_img, img, 50u);
    TEST_ASSERT_EQUAL_INT(1, tbs(p_img, "1", p_bad, p_tbs));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "suit-emit: cannot write "));
    TEST_ASSERT_FALSE(exists(p_tbs));
    TEST_ASSERT_EQUAL_INT(1, tbs(p_img, "1", p_man, p_bad));
    TEST_ASSERT_TRUE(exists(p_man));
}

static void test_a_write_that_fails_at_close_is_a_failure(void)
{
    const uint8_t b[4] = { 1u, 2u, 3u, 4u };

    TEST_ASSERT_TRUE(exists("/dev/full"));
    TEST_ASSERT_EQUAL_INT(1, spill("/dev/full", b, sizeof b));
    TEST_ASSERT_EQUAL_INT(0, spill(p_man, b, sizeof b));
    TEST_ASSERT_EQUAL_UINT(4u, get(p_man, img, sizeof img));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, img, 4u);
}

/* ---- envelope ---------------------------------------------------------- */

static void make_inputs(size_t mn, size_t sn)
{
    uint8_t b[OUT_MAX + 8u];
    size_t  i;

    for (i = 0; i < sizeof b; i++) { b[i] = (uint8_t)(0xA0u + i); }
    put(p_man, b, mn);
    put(p_sig, b + 3, sn);
}

static void test_envelope_wraps_the_manifest_and_signature(void)
{
    uint8_t man[40], sig[64], want[OUT_MAX], got[OUT_MAX + 1u];
    size_t  en;

    make_inputs(40u, 64u);
    TEST_ASSERT_EQUAL_UINT(40u, get(p_man, man, sizeof man));
    TEST_ASSERT_EQUAL_UINT(64u, get(p_sig, sig, sizeof sig));
    en = tfw_suit_emit_envelope(want, sizeof want, man, 40u, sig);
    TEST_ASSERT_NOT_EQUAL(0u, en);
    TEST_ASSERT_EQUAL_INT(0, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_EQUAL_UINT(en, get(p_env, got, sizeof got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, en);
}

static void test_envelope_takes_a_signature_of_exactly_64_bytes(void)
{
    static const size_t bad[] = { 0u, 63u, 65u, 66u, 200u };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        make_inputs(40u, bad[i]);
        TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_env));
        TEST_ASSERT_NOT_NULL(strstr(errbuf, "must be exactly 64 bytes, r||s"));
        TEST_ASSERT_FALSE(exists(p_env));
    }
}

static void test_envelope_refuses_a_missing_empty_or_oversized_manifest(void)
{
    make_inputs(0u, 64u);
    TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "encoding overflowed"));
    TEST_ASSERT_FALSE(exists(p_env));

    make_inputs(OUT_MAX + 1u, 64u);
    TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "is larger than 512 bytes"));
    TEST_ASSERT_FALSE(exists(p_env));

    TEST_ASSERT_EQUAL_INT(1, envelope(p_bad, p_sig, p_env));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "cannot open"));
}

static void test_envelope_refuses_what_does_not_fit_the_output(void)
{
    make_inputs(OUT_MAX, 64u);
    TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "encoding overflowed"));
    TEST_ASSERT_FALSE(exists(p_env));
}

static void test_envelope_fails_when_the_output_cannot_be_written(void)
{
    make_inputs(40u, 64u);
    TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_bad));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "cannot write"));
}

/* ---- chunk boundary, descriptors, exact fits ---------------------------- */

static void test_tbs_digests_a_last_chunk_one_short_of_full(void)
{
    if (!bound()) { return; }
    fill_image(4096u + 4095u);
    put(p_img, img, 4096u + 4095u);
    TEST_ASSERT_EQUAL_INT(0, tbs(p_img, "3", p_man, p_tbs));
    expect_tbs(img, 4096u + 4095u, 3u);
}

/* A descriptor leaked on any path runs a low RLIMIT_NOFILE dry within the loop. */
static void test_slurp_and_spill_close_every_file_they_open(void)
{
    static uint8_t big[65536];
    struct rlimit  was, low;
    uint8_t        b[8];
    int            i, ok = 1, saved, null;

    put(p_img, img, 8u);
    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_NOFILE, &was));
    low          = was;
    low.rlim_cur = 32u;
    fflush(stderr);
    saved = dup(2);
    null  = open("/dev/null", O_WRONLY);
    TEST_ASSERT_TRUE(saved >= 0 && null >= 0);
    dup2(null, 2);
    close(null);
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_NOFILE, &low));
    for (i = 0; i < 40 && ok; i++) {
        ok = slurp(p_img, b, sizeof b) == 8u
          && slurp(p_img, b, 4u) == 0u
          && spill("/dev/full", big, sizeof big) == 1
          && spill(p_man, b, sizeof b) == 0;
    }
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_NOFILE, &was));
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    TEST_ASSERT_TRUE_MESSAGE(ok, "a file was left open");
}

static void test_envelope_takes_an_output_of_exactly_the_cap(void)
{
    uint8_t man[OUT_MAX], sig[64], want[OUT_MAX], got[OUT_MAX + 1u];
    size_t  mn, en = 0u;

    make_inputs(OUT_MAX, 64u);
    TEST_ASSERT_EQUAL_UINT(OUT_MAX, get(p_man, man, sizeof man));
    TEST_ASSERT_EQUAL_UINT(64u, get(p_sig, sig, sizeof sig));
    for (mn = OUT_MAX; mn > 0u; mn--) {
        en = tfw_suit_emit_envelope(want, sizeof want, man, mn, sig);
        if (en != 0u) { break; }
    }
    TEST_ASSERT_EQUAL_UINT(OUT_MAX, en);
    put(p_man, man, mn);
    TEST_ASSERT_EQUAL_INT(0, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_EQUAL_UINT(en, get(p_env, got, sizeof got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, en);
}

static void test_envelope_names_one_fault_for_a_long_signature(void)
{
    make_inputs(40u, 65u);
    TEST_ASSERT_EQUAL_INT(1, envelope(p_man, p_sig, p_env));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "must be exactly 64 bytes, r||s"));
    TEST_ASSERT_NULL(strstr(errbuf, "is larger than"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_wrong_command_line_is_usage_and_2);
    RUN_TEST(test_tbs_on_a_board_with_no_class_id_emits_nothing);
    RUN_TEST(test_tbs_digests_every_chunk_of_the_image);
    RUN_TEST(test_tbs_digests_an_image_of_exactly_one_chunk);
    RUN_TEST(test_tbs_takes_a_sequence_in_any_c_base_up_to_u32_max);
    RUN_TEST(test_tbs_refuses_a_sequence_out_of_range_or_malformed);
    RUN_TEST(test_tbs_refuses_a_missing_or_empty_image);
    RUN_TEST(test_tbs_takes_an_image_of_exactly_the_cap_and_refuses_one_more);
    RUN_TEST(test_tbs_fails_when_either_output_cannot_be_written);
    RUN_TEST(test_a_write_that_fails_at_close_is_a_failure);
    RUN_TEST(test_envelope_wraps_the_manifest_and_signature);
    RUN_TEST(test_envelope_takes_a_signature_of_exactly_64_bytes);
    RUN_TEST(test_envelope_refuses_a_missing_empty_or_oversized_manifest);
    RUN_TEST(test_envelope_refuses_what_does_not_fit_the_output);
    RUN_TEST(test_envelope_fails_when_the_output_cannot_be_written);
    RUN_TEST(test_tbs_digests_a_last_chunk_one_short_of_full);
    RUN_TEST(test_slurp_and_spill_close_every_file_they_open);
    RUN_TEST(test_envelope_takes_an_output_of_exactly_the_cap);
    RUN_TEST(test_envelope_names_one_fault_for_a_long_signature);
    return UNITY_END();
}
