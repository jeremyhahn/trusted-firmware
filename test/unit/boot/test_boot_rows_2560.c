/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATmega2560's tfw-boot rows shed OTA, never verification (item 79, CA
 * m467; docs/SPEC.md, secure-boot): every 2560 row keeps its P-256 verifier,
 * and the store and soft rows that cannot fit OTA beside it drop OTA instead.
 *
 * The rows are mk/boot.mk's BOOT_SUPPORTED, the list bootloader-check builds,
 * handed to this test as a string by its rule in mk/host.mk. Each row is
 * board:tier:measure:sig:ota:store[:ab].
 */

#include "unity.h"

#include <string.h>

#ifndef TFW_BOOT_SUPPORTED
#error "TFW_BOOT_SUPPORTED: mk/host.mk passes mk/boot.mk's BOOT_SUPPORTED"
#endif

#define FIELDS    7
#define FIELD_LEN 24

typedef struct {
    char f[FIELDS][FIELD_LEN];
    int  n;
} row_t;

enum { BOARD, TIER, MEASURE, SIG, OTA, STORE, AB };

#define MAX_ROWS 64
static row_t rows[MAX_ROWS];
static int   nrows;

/* Splits BOOT_SUPPORTED into rows and each row into its fields. */
static void parse(const char *s)
{
    nrows = 0;
    while (*s != '\0') {
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        TEST_ASSERT_TRUE_MESSAGE(nrows < MAX_ROWS, "more BOOT_SUPPORTED rows than MAX_ROWS");
        row_t *r = &rows[nrows++];
        memset(r, 0, sizeof *r);
        size_t len = 0u;
        while (*s != '\0' && *s != ' ' && *s != '\t') {
            if (*s == ':') {
                TEST_ASSERT_TRUE_MESSAGE(r->n + 1 < FIELDS, "a BOOT_SUPPORTED row with too many fields");
                r->n++;
                len = 0u;
            } else {
                TEST_ASSERT_TRUE_MESSAGE(len + 1u < FIELD_LEN, "a BOOT_SUPPORTED field too long");
                r->f[r->n][len++] = *s;
            }
            s++;
        }
        r->n++;
    }
}

static int is_2560(const row_t *r)
{
    return strcmp(r->f[BOARD], "atmega2560") == 0;
}

static const char *name(const row_t *r)
{
    static char buf[FIELDS * FIELD_LEN];
    buf[0] = '\0';
    for (int i = 0; i < r->n; i++) {
        if (i > 0) {
            strcat(buf, ":");
        }
        strcat(buf, r->f[i]);
    }
    return buf;
}

void setUp(void)    { parse(TFW_BOOT_SUPPORTED); }
void tearDown(void) {}

/* Every 2560 row verifies, with the verifier its tier carries: the element's
 * P-256 on the element tier, p256-soft on the soft tier. */
static void test_every_2560_row_verifies_with_its_p256_verifier(void)
{
    int seen = 0;
    for (int i = 0; i < nrows; i++) {
        const row_t *r = &rows[i];
        if (!is_2560(r)) {
            continue;
        }
        seen++;
        TEST_ASSERT_TRUE_MESSAGE(r->n >= STORE + 1, name(r));
        const char *want = strcmp(r->f[TIER], "soft") == 0 ? "p256-soft" : r->f[TIER];
        TEST_ASSERT_TRUE_MESSAGE(strcmp(want, "atecc608a") == 0 || strcmp(want, "p256-soft") == 0,
                                 name(r));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, r->f[SIG], name(r));
    }
    TEST_ASSERT_TRUE_MESSAGE(seen > 0, "BOOT_SUPPORTED has no atmega2560 row");
}

/* The soft rows and the store rows are the ones OTA does not fit beside the
 * verifier (mk/boot.mk's measurements): they shed OTA. */
static void test_every_2560_soft_and_store_row_sheds_ota(void)
{
    for (int i = 0; i < nrows; i++) {
        const row_t *r = &rows[i];
        if (!is_2560(r)) {
            continue;
        }
        if (strcmp(r->f[TIER], "soft") == 0 || strcmp(r->f[STORE], "1") == 0) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("0", r->f[OTA], name(r));
        }
    }
}

/* Shedding OTA leaves the soft base row a row: verification is not what goes. */
static void test_the_2560_soft_base_row_is_still_a_row(void)
{
    int found = 0;
    for (int i = 0; i < nrows; i++) {
        if (strcmp(name(&rows[i]), "atmega2560:soft:soft:p256-soft:0:0") == 0) {
            found = 1;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "atmega2560:soft:soft:p256-soft:0:0 is not in BOOT_SUPPORTED");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_2560_row_verifies_with_its_p256_verifier);
    RUN_TEST(test_every_2560_soft_and_store_row_sheds_ota);
    RUN_TEST(test_the_2560_soft_base_row_is_still_a_row);
    return UNITY_END();
}
