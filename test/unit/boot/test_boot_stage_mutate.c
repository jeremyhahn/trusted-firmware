/*
 * tfw-boot's read of the external SPI store under hostile contents (#75).
 *
 * The store is outside the part: anyone with the board in hand can rewrite
 * it. Each iteration stages a valid image the way test_boot_stage does, then
 * rewrites what tfw_boot_stage_take reads -- the mark's magic, length, hash,
 * signature and mode, the trailer's fields, the body, the image's tail -- and
 * the platform states it starts from, and runs the take.
 *
 * The contract, whatever the store holds:
 *   - the take returns, and reads and writes nothing outside the buffers it
 *     owns (ASan in `make test-asan` is the check);
 *   - an image is copied only when its mark, board id, floor, measurement and
 *     signature all passed on this run, and then the flash holds exactly it;
 *   - an image refused is never copied: no page is written and no state moves.
 * The clean install is test_boot_stage's
 * test_a_valid_image_copies_and_enters_its_trial.
 *
 * Deterministic: xorshift32 from a literal seed, a fixed iteration count.
 * Linked as test_boot_stage is, with the verify mock holding the one
 * signature the row accepts.
 */
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_stage.h"
#include "boot_store.h"
#include "boot_trial.h"
#include "../../model/spi_nor_model.h"
#include "boot_verify_mock.h"

#define DEST(a) ((a) - TFW_BOOT_APP_BASE + (uint32_t)TFW_OTA_STAGE_DEST)
#define STAGE   ((uint32_t)TFW_OTA_STORE_STAGE_ADDR)
#define MARK    ((uint32_t)TFW_OTA_STORE_MARK_ADDR)
#define VERSION 7u
#define ITERS   3000u
#define MAX_IMG 1536u

static uint8_t             stream[TFW_OTA_STORE_STAGE_LEN];
static uint8_t             trailer[TFW_BOOT_TRAILER_LEN];
static uint8_t             mark[TFW_STAGE_MARK_LEN];
static uint8_t             sig[TFW_BOOT_SIG_LEN + 1u];   /* the signature, then the mode */
static uint8_t             good_sig[TFW_BOOT_SIG_LEN];
static tfw_boot_platform_t plat;
static unsigned            stores;
static const uint8_t      *src;
static uint32_t            rng;

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    (void)p;
    stores++;
}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static tfw_status_t mem_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, &src[addr], len);
    return TFW_OK;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void fresh(void)
{
    tfw_boot_mock_reset();
    tfw_nor_model_reset();
    tfw_boot_mock_on_store(tfw_nor_model_xfer);
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    stores = 0u;
    memset(good_sig, 0x5A, sizeof good_sig);
    tfw_boot_verify_mock_want(good_sig);
    tfw_boot_verify_mock_pubkey(TFW_OK);
}

/* The valid image and its mark, in the shadow buffers, as test_boot_stage's
 * stage() builds them; load() puts the shadow into the part. */
static void build(uint32_t image_len, uint32_t total)
{
    uint32_t body = total - TFW_BOOT_TRAILER_LEN;
    uint8_t  digest[TFW_BOOT_MEASURE_LEN];
    uint32_t i;

    memset(stream, 0xFF, sizeof stream);
    for (i = 0u; i < body; i++) {
        stream[i] = (uint8_t)(i * 7u + 3u);
    }
    src = stream;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(mem_read, 0u, image_len, digest));

    memset(trailer, 0, sizeof trailer);
    wr32(&trailer[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    trailer[TFW_BOOT_TR_VERSION]  = (uint8_t)TFW_BOOT_TRAILER_VERSION;
    trailer[TFW_BOOT_TR_BOARD_ID] = TFW_BOARD_ID;
    wr32(&trailer[TFW_BOOT_TR_IMAGE_VERSION], VERSION);
    wr32(&trailer[TFW_BOOT_TR_IMAGE_LEN], image_len);
    memcpy(&trailer[TFW_BOOT_TR_MEASUREMENT], digest, sizeof digest);
    memset(&trailer[TFW_BOOT_TR_SIGNATURE], 0x5A, TFW_BOOT_SIG_LEN);
    memcpy(&stream[body], trailer, sizeof trailer);

    wr32(mark, TFW_STAGE_MARK_MAGIC);
    wr32(&mark[TFW_STAGE_MARK_O_LEN], total);
    memcpy(&mark[TFW_STAGE_MARK_O_HASH], digest, sizeof digest);
    memcpy(sig, good_sig, sizeof good_sig);
    sig[TFW_BOOT_SIG_LEN] = 0u;
}

static void load(void)
{
    tfw_nor_model_load(STAGE, stream, sizeof stream);
    tfw_nor_model_load(MARK, mark, sizeof mark);
    tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_SIG, sig, sizeof sig);
}

static uint32_t edge32(void)
{
    static const uint32_t e[] = {
        0u, 1u, TFW_BOOT_TRAILER_LEN, TFW_BOOT_TRAILER_LEN + 1u,
        (uint32_t)TFW_OTA_STORE_STAGE_LEN, (uint32_t)TFW_OTA_STORE_STAGE_LEN + 1u,
        TFW_BOOT_APP_SIZE, TFW_BOOT_APP_SIZE + 1u, 0x7FFFFFFFuL, 0x80000000uL, 0xFFFFFFFFuL
    };
    uint32_t r = next();

    return ((r & 1u) != 0u) ? e[(r >> 1) % (sizeof e / sizeof e[0])] : next();
}

static void fill(uint8_t *p, uint32_t n)
{
    uint32_t r = next() % 3u;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        p[i] = (r == 0u) ? 0x00u : (r == 1u) ? 0xFFu : (uint8_t)next();
    }
}

/* One rewrite of what the take reads. */
static void mutate(uint32_t total)
{
    uint32_t body = total - TFW_BOOT_TRAILER_LEN;
    uint32_t r    = next();

    switch (r % 12u) {
    case 0:  mark[next() % TFW_STAGE_MARK_LEN] ^= (uint8_t)(1u << (next() % 8u)); break;
    case 1:  wr32(&mark[TFW_STAGE_MARK_O_LEN], edge32()); break;
    case 2:  wr32(&mark[TFW_STAGE_MARK_O_LEN], total + (next() % 64u) - 32u); break;
    case 3:  fill(mark, sizeof mark); break;
    case 4:  sig[next() % sizeof sig] ^= (uint8_t)(1u << (next() % 8u)); break;
    case 5:  fill(sig, sizeof sig); break;
    case 6:  stream[body + next() % TFW_BOOT_TRAILER_LEN] ^= (uint8_t)(1u << (next() % 8u)); break;
    case 7:  wr32(&stream[body + TFW_BOOT_TR_IMAGE_LEN], edge32()); break;
    case 8:  wr32(&stream[body + TFW_BOOT_TR_IMAGE_VERSION], edge32()); break;
    case 9:  stream[body + TFW_BOOT_TR_BOARD_ID] = (uint8_t)next(); break;
    case 10: stream[next() % body] ^= (uint8_t)(1u << (next() % 8u)); break;
    default: /* truncated: the image's tail never written */
        {
            uint32_t at = next() % total;
            memset(&stream[at], 0xFF, total - at);
        }
        break;
    }
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Every check the take must have passed, made again from the shadow. */
static void assert_was_acceptable(uint32_t floor)
{
    const uint8_t     *flash = tfw_boot_mock_flash();
    uint32_t           tot   = rd32(&mark[TFW_STAGE_MARK_O_LEN]);
    uint32_t           body;
    tfw_boot_trailer_t tr;
    uint8_t            digest[TFW_BOOT_MEASURE_LEN];

    TEST_ASSERT_EQUAL_HEX32(TFW_STAGE_MARK_MAGIC, rd32(mark));
    TEST_ASSERT_TRUE(tot > TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_TRUE(tot <= (uint32_t)TFW_OTA_STORE_STAGE_LEN);
    TEST_ASSERT_TRUE(tot <= TFW_BOOT_APP_SIZE);
    body = tot - TFW_BOOT_TRAILER_LEN;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trailer_parse(&stream[body], &tr));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOARD_ID, tr.board_id);
    TEST_ASSERT_TRUE(tr.image_version >= floor);
    TEST_ASSERT_TRUE(tr.image_len <= body);
    src = stream;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(mem_read, 0u, tr.image_len, digest));
    TEST_ASSERT_EQUAL_MEMORY(digest, tr.measurement, sizeof digest);
    TEST_ASSERT_EQUAL_MEMORY(digest, &mark[TFW_STAGE_MARK_O_HASH], sizeof digest);
    TEST_ASSERT_EQUAL_MEMORY(good_sig, sig, sizeof good_sig);

    TEST_ASSERT_EQUAL_MEMORY(stream, &flash[DEST(TFW_BOOT_APP_BASE)], body);
    TEST_ASSERT_EQUAL_MEMORY(&stream[body], &flash[DEST(TFW_BOOT_TRAILER_ADDR)], TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, plat.ota_state);
}

void setUp(void)
{
    fresh();
}

void tearDown(void) {}

static void test_a_hostile_store_never_gets_an_unchecked_image_copied(void)
{
    uint32_t i;
    unsigned taken   = 0u;
    unsigned refused = 0u;

    rng = 0x2545F491uL;
    for (i = 0u; i < ITERS; i++) {
        uint32_t     image_len = 1u + next() % MAX_IMG;
        uint32_t     total     = image_len + TFW_BOOT_TRAILER_LEN + next() % 64u;
        uint32_t     floor     = (next() % 4u == 0u) ? next() : VERSION - (next() % 2u);
        uint32_t     n         = next() % 4u;   /* 0: the valid image, unrewritten */
        uint8_t      s0;
        uint8_t      o0;
        tfw_status_t st;

        fresh();
        build(image_len, total);
        while (n-- > 0u) {
            mutate(total);
        }
        load();
        switch (next() % 8u) {
        case 0:  plat.boot_state = TFW_BOOT_STATE_STAGED; break;
        case 1:  plat.boot_state = (uint8_t)next(); break;
        case 2:  plat.ota_state  = (uint8_t)next(); break;
        default: break;
        }
        s0 = plat.boot_state;
        o0 = plat.ota_state;

        st = tfw_boot_stage_take(&plat, floor);
        TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
        if (st == TFW_OK) {
            assert_was_acceptable(floor);
            taken++;
        } else {
            TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
            TEST_ASSERT_EQUAL_UINT(0u, stores);
            TEST_ASSERT_EQUAL_UINT8(s0, plat.boot_state);
            TEST_ASSERT_EQUAL_UINT8(o0, plat.ota_state);
            refused++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(ITERS, taken + refused);
    TEST_ASSERT_TRUE(taken > 0u);
    TEST_ASSERT_TRUE(refused > 0u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_hostile_store_never_gets_an_unchecked_image_copied);
    return UNITY_END();
}
