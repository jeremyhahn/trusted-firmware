/* Seeded mutation of the state the application reads at start-up and did not
 * write itself (#75, #77): the RAM handoff tfw-boot leaves (src/core/handoff.c),
 * the platform block in EEPROM (src/core/platform_id.c, the floor reader
 * src/core/ota_floor_eeprom.c) and the update request it leaves in the handoff
 * region (src/core/update_request.c). Linked as test_ota_mutate is.
 *
 * Deterministic: xorshift32 from a literal seed, fixed counts. Each iteration
 * starts from the valid record the existing tests build and mutates it: bit
 * flips, all 0x00, all 0xFF, random bytes, and hostile fields under a valid CRC.
 *
 * What it pins, for every input:
 *   - a handoff that fails its magic, version or CRC gives no measurement, no
 *     tier, no reset cause and no CDI, and the caller's buffer is untouched;
 *     the raw block is zero after init whatever it held;
 *   - a platform block that fails its checks gives a zero serial and a zero
 *     operator key; one that passes gives exactly its bytes;
 *   - the floor reported is exactly the floor of the record read, never lower;
 *   - the update request armed over any block decodes as a request.
 * After the run the clean records give what the existing tests pin. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "boot_platform.h"
#include "boot_measure.h"
#include "boot_request.h"
#include "crc16.h"
#include "handoff.h"
#include "platform_id.h"
#include "update_request.h"
#include "ota_floor.h"

#define HANDOFF_RUNS  4000u
#define PLATFORM_RUNS 3000u
#define REQUEST_RUNS  1000u

static uint32_t rng;
static unsigned ran;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

static int all_zero(const uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        if (p[i] != 0u) { return 0; }
    }
    return 1;
}

static void dump(const char *what, const uint8_t *p, unsigned n)
{
    unsigned i;
    printf("%s[%u]:", what, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* One of the mutations the brief names, over n bytes; the CRC at crc_at is
 * recomputed over [0, crc_at) for the hostile-fields-under-a-valid-CRC case. */
static void mutate(uint8_t *p, unsigned n, unsigned crc_at, uint16_t (*crc)(const uint8_t *))
{
    switch (rnd() % 6u) {
    case 0u: memset(p, 0x00, n); break;
    case 1u: memset(p, 0xFF, n); break;
    case 2u: random_fill(p, n); break;
    default: {
        unsigned flips = 1u + rnd() % 8u;
        while (flips--) {
            const uint32_t at = rnd() % (n * 8u);
            p[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        break;
    }
    }
    if (rnd() % 2u) {
        const uint16_t c = crc(p);
        p[crc_at]      = (uint8_t)c;
        p[crc_at + 1u] = (uint8_t)(c >> 8);
    }
}

/* ------------------------------------------------------------------ handoff */

static uint16_t handoff_crc(const uint8_t *b) { return tfw_crc16(b, TFW_BOOT_HO_CRC); }

static void handoff_valid(uint8_t *b)
{
    uint8_t cdi[TFW_BOOT_MEASURE_LEN], m[TFW_BOOT_MEASURE_LEN];
    memset(cdi, 0x5Au, sizeof cdi);
    memset(m, 0x3Cu, sizeof m);
    tfw_boot_handoff_build(b, 1u, cdi, m, 3u, TFW_RESET_WATCHDOG);
}

static void handoff_check(const uint8_t *in)
{
    uint8_t        out[TFW_BOOT_MEASURE_LEN];
    uint8_t        rc = 0xEEu;
    const uint8_t *meas, *tier;
    const int      good = handoff_crc(in) == (uint16_t)(in[TFW_BOOT_HO_CRC] | in[TFW_BOOT_HO_CRC + 1u] << 8)
                          && in[0] == 0x41u && in[1] == 0x43u && in[2] == 0x44u && in[3] == 0x49u
                          && in[TFW_BOOT_HO_VERSION] == TFW_BOOT_HANDOFF_VERSION;
    const char    *broke = NULL;

    memcpy(tfw_mock_handoff(), in, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
    meas = tfw_boot_handoff_measurement();
    tier = tfw_boot_handoff_tier();
    memset(out, 0xA5u, sizeof out);

    if (!all_zero(tfw_mock_handoff(), TFW_BOOT_HANDOFF_LEN)) {
        broke = "the raw handoff block outlived init";
    } else if (!good) {
        if (meas != NULL || tier != NULL || tfw_boot_handoff_reset_cause(&rc) != 0u) {
            broke = "a refused handoff gave a measurement, a tier or a reset cause";
        } else if (!all_zero(tfw_boot_handoff_fw_hash(), TFW_BOOT_MEASURE_LEN)) {
            broke = "a refused handoff left a non-zero FWHash";
        } else if (tfw_boot_handoff_take_cdi(out) != 0u || out[0] != 0xA5u
                   || out[TFW_BOOT_MEASURE_LEN - 1u] != 0xA5u) {
            broke = "a refused handoff gave a CDI or touched the caller's buffer";
        }
    } else {
        const int zero_cdi = all_zero(&in[TFW_BOOT_HO_CDI], TFW_BOOT_MEASURE_LEN);
        if (meas == NULL || memcmp(meas, &in[TFW_BOOT_HO_MEASUREMENT], TFW_BOOT_MEASURE_LEN) != 0) {
            broke = "an accepted handoff's measurement is not its bytes";
        } else if (tier == NULL || *tier != in[TFW_BOOT_HO_TIER]) {
            broke = "an accepted handoff's tier is not its byte";
        } else if (tfw_boot_handoff_reset_cause(&rc) != 1u || rc != in[TFW_BOOT_HO_RESET_CAUSE]) {
            broke = "an accepted handoff's reset cause is not its byte";
        } else if (tfw_boot_handoff_take_cdi(out) != (uint8_t)!zero_cdi) {
            broke = "an all-zero CDI was offered, or a real one withheld";
        } else if (!zero_cdi && memcmp(out, &in[TFW_BOOT_HO_CDI], TFW_BOOT_MEASURE_LEN) != 0) {
            broke = "the CDI taken is not the handoff's";
        } else if (tfw_boot_handoff_take_cdi(out) != 0u) {
            broke = "the CDI could be taken twice";
        }
    }
    if (broke) {
        dump("handoff", in, TFW_BOOT_HANDOFF_LEN);
        TEST_FAIL_MESSAGE(broke);
    }
    ran++;
}

static void test_mutated_handoffs_give_nothing_or_exactly_their_bytes(void)
{
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    uint8_t  out[TFW_BOOT_MEASURE_LEN];
    unsigned i, v;
    clock_t  t0 = clock();

    rng = 0x1D0FF0A7u;
    ran = 0u;
    tfw_mock_reset();
    for (i = 0u; i < HANDOFF_RUNS; i++) {
        handoff_valid(b);
        mutate(b, sizeof b, TFW_BOOT_HO_CRC, handoff_crc);
        handoff_check(b);
    }
    /* every tier, version and reset-cause byte under a valid CRC */
    for (v = 0u; v < 256u; v++) {
        static const unsigned at[3] = { TFW_BOOT_HO_TIER, TFW_BOOT_HO_VERSION, TFW_BOOT_HO_RESET_CAUSE };
        unsigned k;
        for (k = 0u; k < 3u; k++) {
            uint16_t c;
            handoff_valid(b);
            b[at[k]] = (uint8_t)v;
            c = handoff_crc(b);
            b[TFW_BOOT_HO_CRC] = (uint8_t)c;
            b[TFW_BOOT_HO_CRC + 1u] = (uint8_t)(c >> 8);
            handoff_check(b);
        }
    }
    /* a stale CDI: a valid handoff, then a bad one, takes nothing */
    handoff_valid(b);
    memcpy(tfw_mock_handoff(), b, sizeof b);
    tfw_boot_handoff_init();
    b[0] ^= 1u;
    handoff_check(b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(out));

    printf("handoff: %u runs, %.3f s\n", ran, (double)(clock() - t0) / CLOCKS_PER_SEC);
    TEST_ASSERT_EQUAL_UINT(HANDOFF_RUNS + 3u * 256u + 1u, ran);
}

/* ----------------------------------------------------------------- platform */

static void platform_valid(uint8_t *raw)
{
    tfw_boot_platform_t p;
    unsigned            i;

    memset(&p, 0, sizeof p);
    memset(raw, 0, TFW_BOOT_EE_LEN);
    for (i = 0u; i < TFW_BOOT_SERIAL_LEN; i++) { raw[TFW_BOOT_EE_O_SERIAL + i] = (uint8_t)(0x10u + i); }
    for (i = 0u; i < TFW_BOOT_OPERATOR_PUBKEY_LEN; i++) { raw[TFW_BOOT_EE_O_OPERATOR_PUBKEY + i] = (uint8_t)(0x80u + i); }
    p.rollback_floor = 7u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_serialise(&p, raw);
}

static void platform_check(const uint8_t *raw)
{
    tfw_boot_platform_t p;
    uint8_t             key[TFW_BOOT_OPERATOR_PUBKEY_LEN];
    uint32_t            floor = 0xA5A5A5A5u;
    const int           good  = tfw_boot_rd_u32(&raw[TFW_BOOT_EE_O_MAGIC]) == TFW_BOOT_EE_MAGIC
                                && raw[TFW_BOOT_EE_O_LAYOUT] == TFW_BOOT_EE_VERSION
                                && tfw_boot_platform_crc(raw) == tfw_boot_rd_u16(&raw[TFW_BOOT_EE_O_CRC]);
    const char         *broke = NULL;

    memcpy(tfw_mock_eeprom() + TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN);
    tfw_platform_id_init();
    tfw_platform_id_operator_pubkey(key);

    if (!good) {
        if (tfw_boot_platform_parse(raw, &p) == TFW_OK) {
            broke = "a block failing its magic, layout or CRC parsed";
        } else if (!all_zero(tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN)) {
            broke = "a refused block gave a serial";
        } else if (!all_zero(key, sizeof key)) {
            broke = "a refused block gave an operator key";
        }
    } else if (tfw_boot_platform_parse(raw, &p) != TFW_OK) {
        broke = "a block passing its checks was refused";
    } else if (memcmp(tfw_platform_id_serial(), &raw[TFW_BOOT_EE_O_SERIAL], TFW_BOOT_SERIAL_LEN) != 0) {
        broke = "an accepted block's serial is not its bytes";
    } else if (memcmp(key, &raw[TFW_BOOT_EE_O_OPERATOR_PUBKEY], sizeof key) != 0) {
        broke = "an accepted block's operator key is not its bytes";
    } else if (tfw_otactl_floor(&p, &floor) != TFW_OK
               || floor != tfw_boot_rd_u32(&raw[TFW_BOOT_EE_O_ROLLBACK_FLOOR])) {
        broke = "the floor reported is not the floor of the record read";
    } else if (p.boot_state != raw[TFW_BOOT_EE_O_BOOT_STATE]) {
        broke = "an accepted block's boot state is not its byte";
    }
    if (broke) {
        dump("platform", raw, TFW_BOOT_EE_LEN);
        TEST_FAIL_MESSAGE(broke);
    }
    ran++;
}

static void test_mutated_platform_blocks_give_zero_or_exactly_their_bytes(void)
{
    static const uint32_t floors[] = { 0u, 1u, 6u, 7u, 8u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu };
    uint8_t  raw[TFW_BOOT_EE_LEN];
    unsigned i, v;
    clock_t  t0 = clock();

    rng = 0x5EED7E57u;
    ran = 0u;
    tfw_mock_reset();
    for (i = 0u; i < PLATFORM_RUNS; i++) {
        platform_valid(raw);
        mutate(raw, sizeof raw, TFW_BOOT_EE_O_CRC, tfw_boot_platform_crc);
        platform_check(raw);
    }
    /* every state, layout, flags and fail-count byte, and the floor at its
     * edges, under a valid CRC */
    for (v = 0u; v < 256u; v++) {
        static const unsigned at[5] = { TFW_BOOT_EE_O_BOOT_STATE, TFW_BOOT_EE_O_OTA_STATE, TFW_BOOT_EE_O_LAYOUT,
                                        TFW_BOOT_EE_O_FLAGS, TFW_BOOT_EE_O_FAIL_COUNT };
        unsigned k;
        for (k = 0u; k < 5u; k++) {
            platform_valid(raw);
            raw[at[k]] = (uint8_t)v;
            tfw_boot_wr_u16(&raw[TFW_BOOT_EE_O_CRC], tfw_boot_platform_crc(raw));
            platform_check(raw);
        }
    }
    for (i = 0u; i < sizeof floors / sizeof floors[0]; i++) {
        platform_valid(raw);
        tfw_boot_wr_u32(&raw[TFW_BOOT_EE_O_ROLLBACK_FLOOR], floors[i]);
        tfw_boot_wr_u16(&raw[TFW_BOOT_EE_O_CRC], tfw_boot_platform_crc(raw));
        platform_check(raw);
    }
    printf("platform: %u runs, %.3f s\n", ran, (double)(clock() - t0) / CLOCKS_PER_SEC);
    TEST_ASSERT_EQUAL_UINT(PLATFORM_RUNS + 5u * 256u + sizeof floors / sizeof floors[0], ran);
}

/* ------------------------------------------------------------------ request */

static void test_the_request_armed_over_any_block_is_a_request(void)
{
    unsigned i;
    clock_t  t0 = clock();

    rng = 0x0A11CE55u;
    ran = 0u;
    tfw_mock_reset();
    for (i = 0u; i < REQUEST_RUNS; i++) {
        uint8_t b[TFW_BOOT_HANDOFF_LEN];
        handoff_valid(b);
        mutate(b, sizeof b, TFW_BOOT_HO_CRC, handoff_crc);
        memcpy(tfw_mock_handoff(), b, sizeof b);
        tfw_update_request_arm();
        if (tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG) != 1u
            || tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_POWER_ON) != 0u) {
            dump("handoff before arm", b, sizeof b);
            TEST_FAIL_MESSAGE("the armed request did not decode as one, or did after a power-on");
        }
        ran++;
    }
    printf("request: %u runs, %.3f s\n", ran, (double)(clock() - t0) / CLOCKS_PER_SEC);
    TEST_ASSERT_EQUAL_UINT(REQUEST_RUNS, ran);
}

/* ------------------------------------------------------------------- after */

static void test_clean_records_still_give_what_the_existing_tests_pin(void)
{
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    uint8_t  raw[TFW_BOOT_EE_LEN];
    uint8_t  cdi[TFW_BOOT_MEASURE_LEN];
    uint8_t  rc = 0u;
    uint32_t floor = 0u;
    tfw_boot_platform_t p;

    tfw_mock_reset();
    handoff_valid(b);
    memcpy(tfw_mock_handoff(), b, sizeof b);
    tfw_boot_handoff_init();
    TEST_ASSERT_NOT_NULL(tfw_boot_handoff_measurement());
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, tfw_boot_handoff_measurement()[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, *tfw_boot_handoff_tier());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_handoff_reset_cause(&rc));
    TEST_ASSERT_EQUAL_UINT8(TFW_RESET_WATCHDOG, rc);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_handoff_take_cdi(cdi));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, cdi[0]);

    platform_valid(raw);
    memcpy(tfw_mock_eeprom() + TFW_BOOT_EE_BASE, raw, sizeof raw);
    tfw_platform_id_init();
    TEST_ASSERT_EQUAL_MEMORY(&raw[TFW_BOOT_EE_O_SERIAL], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(raw, &p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(&p, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);

    tfw_mock_reset();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG));
    tfw_update_request_arm();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG));
}

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) { }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_handoffs_give_nothing_or_exactly_their_bytes);
    RUN_TEST(test_mutated_platform_blocks_give_zero_or_exactly_their_bytes);
    RUN_TEST(test_the_request_armed_over_any_block_is_a_request);
    RUN_TEST(test_clean_records_still_give_what_the_existing_tests_pin);
    return UNITY_END();
}
