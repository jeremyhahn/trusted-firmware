/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for tfwsim's element tier (tools/tfwsim/tier_atecc608a.c,
 * #163): its options, the EEPROM image it loads, and what its seed leaves in
 * the model and the mock before the firmware starts. A wrong refusal or a
 * wrong acceptance here lets the cross-implementation suite pass against a
 * device the element tier never is.
 *
 * The tier keeps its options in file statics that nothing clears, as a process
 * that parses its argv once would. The tests therefore run in the order main()
 * lists them: the blank seed first, then every refusal (none of which sets
 * anything), then the options that do.
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "boot_measure.h"
#include "hal_mock.h"
#include "sha256.h"
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "tfw/types.h"
#include "tier.h"

#define ECC_ADDR TFW_ATECC_ADDR

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

/* ---------------------------------------------------------------- helpers */

static char tmp_path[64];

/* Writes n bytes to a fresh temporary file; its path is tmp_path. */
static char *write_tmp(const void *data, size_t n)
{
    int fd;

    strcpy(tmp_path, "/tmp/tfw-tier-atecc-XXXXXX");
    fd = mkstemp(tmp_path);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_TRUE(write(fd, data, n) == (ssize_t)n);
    TEST_ASSERT_EQUAL_INT(0, close(fd));
    return tmp_path;
}

/* The option's answer for `name FILE` with FILE holding text. */
static int load_text(const char *name, const char *text)
{
    char  n[32];
    char *argv[3];
    int   rc;

    strcpy(n, name);
    argv[0] = n;
    argv[1] = write_tmp(text, strlen(text));
    argv[2] = NULL;
    rc = tfwsim_tier_option(2, argv);
    (void)unlink(tmp_path);
    return rc;
}

static int option1(const char *name)
{
    char  n[32];
    char *argv[2];

    strcpy(n, name);
    argv[0] = n;
    argv[1] = NULL;
    return tfwsim_tier_option(1, argv);
}

static int image_hash(const char *hex)
{
    char  n[] = "--image-hash";
    char  v[80];
    char *argv[3];

    strcpy(v, hex);
    argv[0] = n;
    argv[1] = v;
    argv[2] = NULL;
    return tfwsim_tier_option(2, argv);
}

/* One Intel HEX record, checksum and line ending included. */
static size_t ihex_rec(char *out, uint8_t type, uint16_t addr, const uint8_t *d, uint8_t n,
                       const char *eol)
{
    uint8_t sum = (uint8_t)(n + (addr >> 8) + (addr & 0xFFu) + type);
    size_t  w   = (size_t)sprintf(out, ":%02X%04X%02X", n, addr, type);
    uint8_t i;

    for (i = 0u; i < n; i++) {
        sum = (uint8_t)(sum + d[i]);
        w += (size_t)sprintf(out + w, "%02X", d[i]);
    }
    w += (size_t)sprintf(out + w, "%02X%s", (uint8_t)(0u - sum), eol);
    return w;
}

/* Whether the element answers its address after a wake. */
static int element_answers(void)
{
    uint8_t wake = 0u, rsp[4] = {0};

    (void)tfw_hal_i2c_xfer(0x00u, &wake, 1u, NULL, 0u);
    return tfw_hal_i2c_xfer(ECC_ADDR, NULL, 0u, rsp, sizeof rsp) == TFW_OK &&
           rsp[0] == 0x04u && rsp[1] == 0x11u;
}

static void expect_no_handoff(void)
{
    uint8_t tier, cdi[32], m[32], cause, seed[TFW_BOOT_SEED_LEN];
    uint32_t ver;

    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_handoff_parse(tfw_mock_handoff(), &tier, cdi, m,
                                                         &ver, &cause, seed));
}

/* ------------------------------------------------------------- the blank */

/* Mutants: the seed without tfw_model_reset (a key, an absent bus or a
 * forced status left from before survives), the EEPROM copy without its
 * NULL guard (a crash), a handoff laid with no --image-hash. A start with no
 * options is a blank part on a working bus, and the EEPROM is the mock's. */
static void test_the_blank_seed_is_a_blank_part(void)
{
    tfw_model_provision();
    tfw_model_set_absent(1u);
    memset(tfw_mock_eeprom(), 0x5A, tfw_mock_eeprom_size());

    tfwsim_tier_seed();

    TEST_ASSERT_NULL(tfw_model_deviceid_public());
    TEST_ASSERT_TRUE(element_answers());
    TEST_ASSERT_EACH_EQUAL_HEX8(0x5Au, tfw_mock_eeprom(), tfw_mock_eeprom_size());
    expect_no_handoff();
}

/* Mutant: the usage line dropping or misspelling an option. */
static void test_the_usage_names_every_option(void)
{
    TEST_ASSERT_EQUAL_STRING(" [--eeprom FILE.eep | --eeprom-bin FILE] [--provision-element]"
                             " [--image-hash HEX64] [--boot-tier soft|element] [--uds HEX64] [--no-element]",
                             tfwsim_tier_usage());
}

/* ------------------------------------------------------------- refusals */

/* Mutants: a prefix or a near name taken as an option (strncmp, or a
 * comparison dropped), and an unknown one answered as consumed. */
static void test_an_unknown_option_is_not_consumed(void)
{
    static const char *const names[] = {
        "--pty", "--eth", "--provision-elemen", "--provision-elements", "--no-elemen",
        "--eeprom=x", "--eeprom-binx", "--image-has", "-", "",
    };
    size_t i;

    for (i = 0u; i < sizeof names / sizeof names[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, option1(names[i]), names[i]);
    }
}

/* Mutant: the argc < 2 check dropped, or bound by one (argc < 1). Each value
 * option alone is an error, not a read of argv[1]. */
static void test_a_value_option_without_its_value_fails(void)
{
    TEST_ASSERT_EQUAL_INT(-1, option1("--eeprom"));
    TEST_ASSERT_EQUAL_INT(-1, option1("--eeprom-bin"));
    TEST_ASSERT_EQUAL_INT(-1, option1("--image-hash"));
}

/* Mutants: hex_bytes' length check dropped or bound by one, its nibble check
 * dropped for either half, and each hex_nibble range widened by one. */
static void test_a_malformed_image_hash_fails(void)
{
    static const char *const bad[] = {
        "000000000000000000000000000000000000000000000000000000000000000",   /* 63 */
        "00000000000000000000000000000000000000000000000000000000000000000", /* 65 */
        "",
        "g000000000000000000000000000000000000000000000000000000000000000",
        "0G00000000000000000000000000000000000000000000000000000000000000",
        "/000000000000000000000000000000000000000000000000000000000000000",
        "0:00000000000000000000000000000000000000000000000000000000000000",
        "`000000000000000000000000000000000000000000000000000000000000000",
        "0@00000000000000000000000000000000000000000000000000000000000000",
        "000000000000000000000000000000000000000000000000000000000000000 ",
    };
    size_t i;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, image_hash(bad[i]), bad[i]);
    }
    /* None of them set the hash. */
    tfwsim_tier_seed();
    expect_no_handoff();
}

/* Mutant: slurp's open failure not reported. */
static void test_a_missing_eeprom_file_fails(void)
{
    char  n[] = "--eeprom-bin";
    char  v[] = "/nonexistent/tfw-tier-atecc";
    char *argv[] = { n, v, NULL };

    TEST_ASSERT_EQUAL_INT(-1, tfwsim_tier_option(2, argv));
    n[8] = '\0';                                           /* "--eeprom" */
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_tier_option(2, argv));
}

/* Mutants: each Intel HEX check dropped -- the start code, the record length
 * and its parity, the byte count, the checksum, the address bound, a record
 * after EOF, an unknown record type, and the missing EOF. */
static void test_a_malformed_intel_hex_fails(void)
{
    static const char *const bad[] = {
        "",                                     /* no EOF record */
        ":0100000041BE\n",                      /* data, no EOF */
        "X00000001FF\n",                        /* no start code */
        ":\n:00000001FF\n",                     /* empty record */
        ":000000\n:00000001FF\n",               /* shorter than a record */
        ":00000001F\n",                         /* odd length */
        ":0100000001\n:00000001FF\n",           /* count says 1, has 0 */
        ":000000000041\n:00000001FF\n",         /* count says 0, has 1 */
        ":020000040000FA\n",                    /* only an unknown type */
        ":0100000041BF\n:00000001FF\n",         /* bad checksum */
        ":0100000041BE\n:00000001FE\n",         /* bad checksum on EOF */
        ":00000001FF\n:0100000041BE\n",         /* data after EOF */
        ":00000001FF\n:00000001FF\n",           /* two EOFs */
        ":020000020000FC\n:00000001FF\n",       /* extended segment address */
        ":020000040000FA\n:00000001FF\n",       /* extended linear address */
        ":0104000041BA\n:00000001FF\n",         /* byte 1024, one past the end */
        ":0203FF00414279\n:00000001FF\n",       /* 1023..1024 straddles the end */
        ":0100000g41BE\n:00000001FF\n",         /* not hex */
    };
    size_t i;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, load_text("--eeprom", bad[i]), bad[i]);
    }
}

/* Mutants: the n > cap check dropped or bound by one (n > cap + 1), so a raw
 * image larger than the EEPROM is accepted. 4096 bytes is also slurp's first buffer exactly, so the
 * read crosses its n == cap growth. */
static void test_a_raw_image_larger_than_the_eeprom_fails(void)
{
    static uint8_t big[4096];
    char  n[] = "--eeprom-bin";
    char *argv[3];

    memset(big, 0xA5, sizeof big);
    argv[0] = n;
    argv[1] = write_tmp(big, tfw_mock_eeprom_size() + 1u);
    argv[2] = NULL;
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_tier_option(2, argv));
    (void)unlink(tmp_path);
    argv[1] = write_tmp(big, sizeof big);
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_tier_option(2, argv));
    (void)unlink(tmp_path);
}

/* --------------------------------------------------------------- loads */

/* Mutants: n > cap as n >= cap (an image exactly the EEPROM's size refused),
 * the copy skipped, and --eeprom-bin read as Intel HEX. */
static void test_a_raw_image_of_the_eeprom_size_is_the_eeprom(void)
{
    static uint8_t img[1024];
    char  n[] = "--eeprom-bin";
    char *argv[3];
    size_t i;

    TEST_ASSERT_EQUAL_size_t(sizeof img, tfw_mock_eeprom_size());
    for (i = 0u; i < sizeof img; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    img[0] = ':';                                          /* not a HEX file */
    argv[0] = n;
    argv[1] = write_tmp(img, sizeof img);
    argv[2] = NULL;
    TEST_ASSERT_EQUAL_INT(2, tfwsim_tier_option(2, argv));
    (void)unlink(tmp_path);
    TEST_ASSERT_EQUAL_PTR(n, argv[0]);

    tfwsim_tier_seed();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, tfw_mock_eeprom(), sizeof img);
}

/* Mutant: the erase before a short raw image's copy skipped. A second load
 * also replaces the first rather than merging into it. */
static void test_a_short_raw_image_leaves_the_rest_erased(void)
{
    static const uint8_t img[3] = { 0x01u, 0x02u, 0x03u };
    char  n[] = "--eeprom-bin";
    char *argv[3];

    argv[0] = n;
    argv[1] = write_tmp(img, sizeof img);
    argv[2] = NULL;
    TEST_ASSERT_EQUAL_INT(2, tfwsim_tier_option(2, argv));
    (void)unlink(tmp_path);

    tfwsim_tier_seed();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, tfw_mock_eeprom(), sizeof img);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, tfw_mock_eeprom() + sizeof img,
                                tfw_mock_eeprom_size() - sizeof img);
}

/* Mutants: the erase before Intel HEX skipped, a record written at the wrong
 * address or length, the address bound as >= (the last byte refused), CR or
 * LF not skipped, and --eeprom read as a raw image. Every byte of the EEPROM
 * from its own one-byte record, 14 KB of text: slurp grows twice. */
static void test_an_intel_hex_image_covers_every_byte(void)
{
    static char text[1024u * 16u + 64u];
    size_t  w = 0u;
    uint16_t a;

    w += (size_t)sprintf(text, "\r\n\n");
    for (a = 0u; a < 1024u; a++) {
        uint8_t d = (uint8_t)(a ^ (a >> 8) ^ 0x3Cu);

        w += ihex_rec(text + w, 0x00u, a, &d, 1u, (a & 1u) ? "\r\n" : "\n");
    }
    w += ihex_rec(text + w, 0x01u, 0u, NULL, 0u, "");          /* no final newline */
    TEST_ASSERT_TRUE(w < sizeof text);
    TEST_ASSERT_TRUE(w > 8192u);
    TEST_ASSERT_EQUAL_INT(2, load_text("--eeprom", text));

    tfwsim_tier_seed();
    for (a = 0u; a < 1024u; a++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(a ^ (a >> 8) ^ 0x3Cu), tfw_mock_eeprom()[a]);
    }
}

/* Mutant: the erase skipped on a partial file, the case tfwprov writes. A
 * multi-byte record lands in order at its address. The last word of the
 * EEPROM is reachable. */
static void test_a_partial_intel_hex_leaves_the_rest_erased(void)
{
    static const uint8_t d[4] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    char    text[128];
    size_t  w = 0u;
    uint8_t *e;
    size_t   i;

    w += ihex_rec(text + w, 0x00u, 0x0010u, d, 4u, "\n");
    w += ihex_rec(text + w, 0x00u, 0x03FEu, d, 2u, "\n");
    (void)ihex_rec(text + w, 0x01u, 0u, NULL, 0u, "\n");
    TEST_ASSERT_EQUAL_INT(2, load_text("--eeprom", text));

    tfwsim_tier_seed();
    e = tfw_mock_eeprom();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(d, &e[0x10], 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(d, &e[0x3FE], 2u);
    for (i = 0u; i < 0x3FEu; i++) {
        if (i < 0x10u || i >= 0x14u) {
            TEST_ASSERT_EQUAL_HEX8(0xFFu, e[i]);
        }
    }
}

/* ------------------------------------------------------------- the part */

/* Mutants: --no-element consuming its argument or not answered 1, and the
 * seed not passing it to the model (or passing its inverse). */
static void test_no_element_takes_the_part_off_the_bus(void)
{
    char  n[] = "--no-element";
    char  v[] = "--provision-element";
    char *argv[] = { n, v, NULL };

    tfwsim_tier_seed();
    TEST_ASSERT_TRUE(element_answers());
    TEST_ASSERT_EQUAL_INT(1, tfwsim_tier_option(2, argv));
    TEST_ASSERT_EQUAL_STRING("--provision-element", argv[1]);
    tfwsim_tier_seed();
    TEST_ASSERT_FALSE(element_answers());
    TEST_ASSERT_NULL(tfw_model_deviceid_public());   /* the next one was not taken */
}

/* Mutants: --provision-element answered other than 1, the flag not set, and
 * the seed not provisioning. The key is the model's documented TEST key. */
static void test_provision_element_seeds_the_test_key(void)
{
    uint8_t priv[32], pub[64];

    TEST_ASSERT_EQUAL_INT(1, option1("--provision-element"));
    tfwsim_tier_seed();
    tfw_model_test_deviceid(priv, pub);
    TEST_ASSERT_NOT_NULL(tfw_model_deviceid_public());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pub, tfw_model_deviceid_public(), sizeof pub);
}

/* Mutants: every hex_nibble bound by one (0 9 a f A F decode), the nibble
 * shift, --image-hash answered other than 2 or the flag not set, and the
 * handoff the seed lays: its tier, CDI (the domain-separated hash of the
 * image hash), measurement, version and reset cause. */
static void test_image_hash_lays_tfw_boots_handoff(void)
{
    static const char hex[] =
        "0123456789abcdefABCDEF00ff9a0Ff0" "a9f0F0A00909affaFAAF5aA5c3D2e1B4";
    static const uint8_t want[32] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xAB, 0xCD, 0xEF, 0x00, 0xFF, 0x9A, 0x0F, 0xF0,
        0xA9, 0xF0, 0xF0, 0xA0, 0x09, 0x09, 0xAF, 0xFA, 0xFA, 0xAF, 0x5A, 0xA5, 0xC3, 0xD2, 0xE1, 0xB4,
    };
    static const char domain[] = "tfwsim-test-cdi-v1";
    uint8_t  in[sizeof domain - 1u + 32u], cdi_want[32];
    uint8_t  tier = 0u, cdi[32], m[32], cause = 0xFFu, seed[TFW_BOOT_SEED_LEN];
    uint32_t ver = 0u;

    TEST_ASSERT_EQUAL_INT(2, image_hash(hex));
    tfwsim_tier_seed();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_handoff_parse(tfw_mock_handoff(), &tier, cdi, m,
                                                         &ver, &cause, seed));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, m, sizeof want);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, tier);
    TEST_ASSERT_EQUAL_UINT32(1u, ver);
    TEST_ASSERT_EQUAL_UINT8(TFW_RESET_POWER_ON, cause);

    memcpy(in, domain, sizeof domain - 1u);
    memcpy(in + sizeof domain - 1u, want, sizeof want);
    tfw_sha256(in, (uint16_t)sizeof in, cdi_want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cdi_want, cdi, sizeof cdi);

    /* The options before it still stand: the seed applies all of them. */
    TEST_ASSERT_FALSE(element_answers());
    TEST_ASSERT_NOT_NULL(tfw_model_deviceid_public());
    TEST_ASSERT_EQUAL_HEX8(0xDEu, tfw_mock_eeprom()[0x10]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_blank_seed_is_a_blank_part);
    RUN_TEST(test_the_usage_names_every_option);
    RUN_TEST(test_an_unknown_option_is_not_consumed);
    RUN_TEST(test_a_value_option_without_its_value_fails);
    RUN_TEST(test_a_malformed_image_hash_fails);
    RUN_TEST(test_a_missing_eeprom_file_fails);
    RUN_TEST(test_a_malformed_intel_hex_fails);
    RUN_TEST(test_a_raw_image_larger_than_the_eeprom_fails);
    RUN_TEST(test_a_raw_image_of_the_eeprom_size_is_the_eeprom);
    RUN_TEST(test_a_short_raw_image_leaves_the_rest_erased);
    RUN_TEST(test_an_intel_hex_image_covers_every_byte);
    RUN_TEST(test_a_partial_intel_hex_leaves_the_rest_erased);
    RUN_TEST(test_no_element_takes_the_part_off_the_bus);
    RUN_TEST(test_provision_element_seeds_the_test_key);
    RUN_TEST(test_image_hash_lays_tfw_boots_handoff);
    return UNITY_END();
}
