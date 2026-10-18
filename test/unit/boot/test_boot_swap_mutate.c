/*
 * tfw-boot's swap-move journal and confirm request under hostile contents
 * (#77), on the 2560, whose geometry alone has the swap.
 *
 * Both sit in EEPROM, which anyone with the board in hand can rewrite, CRC
 * and all. Each iteration seeds the two slots as test_boot_swap does, writes
 * the journal's two copies and the confirm request -- each a valid record in
 * any phase, a valid one bit-flipped, all 0x00, all 0xFF or random bytes --
 * starts from any platform state, and runs the reset's swap step.
 *
 * The contract, whatever the records hold:
 *   - the step returns, and reads and writes nothing outside the buffers it
 *     owns (ASan in `make test-asan` is the check);
 *   - its decision is one of the four actions;
 *   - no byte of flash outside the swap's area (the two slots and the unit
 *     between them) changes.
 * After the hostile runs an erased journal swaps the slots whole, as
 * test_boot_swap pins it.
 *
 * Deterministic: xorshift32 from a literal seed, a fixed iteration count.
 * Linked as test_boot_swap is, with its evaluate, hand-over, floor and
 * platform store.
 */
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_policy.h"
#include "boot_store.h"
#include "boot_swap.h"
#include "boot_trial.h"
#include "crc16.h"

#define SLOT  ((uint32_t)TFW_SWAP_SLOT_LEN)
#define PRI   ((uint32_t)TFW_SWAP_PRIMARY_BASE)
#define SEC   ((uint32_t)TFW_SWAP_SECONDARY_BASE)
#define REC   ((uint16_t)TFW_SWAP_RECORD_ADDR)
#define UNITS ((uint32_t)(TFW_SWAP_SLOT_LEN / TFW_OTA_PAGE_MAX))
#define COPY  5u
#define ITERS 1000u

/* The journal's step byte, any value: on the 2560 UNITS is 256, so every
 * step the byte can hold names a unit of the slot. */
#define STEP_SPAN 256u

static uint8_t             img_a[TFW_SWAP_SLOT_LEN];
static uint8_t             img_b[TFW_SWAP_SLOT_LEN];
static uint8_t             before[0x40000uL];   /* the 2560's flash, as the mock sizes it */
static tfw_boot_platform_t plat;
static uint32_t            rng;

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    (void)p;
}

tfw_status_t tfw_boot_evaluate_at(uint32_t base, uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)base;
    (void)floor;
    memset(out, 0, sizeof *out);
    return TFW_ERR_CRYPTO;   /* nothing here is ever worth handing over */
}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    return tfw_boot_evaluate_at(TFW_BOOT_APP_BASE, floor, out);
}

void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    (void)v;
    TEST_FAIL_MESSAGE("handed over an image that was never evaluated good");
}

tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out)
{
    *out = p->rollback_floor;
    return TFW_OK;
}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void seed(void)
{
    uint32_t i;

    for (i = 0u; i < SLOT; i++) {
        img_a[i] = (uint8_t)((i * 7u) ^ (i >> 8));
        img_b[i] = (uint8_t)~img_a[i];
    }
    memcpy(&tfw_boot_mock_flash()[PRI], img_a, SLOT);
    memcpy(&tfw_boot_mock_flash()[SEC], img_b, SLOT);
}

static void hostile(uint8_t *c, uint32_t n, uint8_t (*valid)(uint8_t *c))
{
    uint32_t k;

    switch (next() % 6u) {
    case 0:  memset(c, 0x00, n); break;
    case 1:  memset(c, 0xFF, n); break;
    case 2:
        for (k = 0u; k < n; k++) {
            c[k] = (uint8_t)next();
        }
        break;
    case 3:
        (void)valid(c);
        c[next() % n] ^= (uint8_t)(1u << (next() % 8u));
        break;
    default: (void)valid(c); break;
    }
}

static uint8_t journal_copy(uint8_t *c)
{
    static const uint8_t ph[] = { 0x4Du, 0x58u, 0x59u, 0x6Du, 0x78u, 0x79u, 0x44u, 0xFFu };
    uint16_t crc;
    uint32_t r = next();

    c[0] = ((r & 7u) != 7u) ? ph[(r >> 3) % sizeof ph] : (uint8_t)next();
    c[1] = (uint8_t)(next() % STEP_SPAN);
    c[2] = (uint8_t)next();
    crc  = tfw_crc16(c, 3u);
    c[3] = (uint8_t)crc;
    c[4] = (uint8_t)(crc >> 8);
    return 1u;
}

static uint8_t confirm_record(uint8_t *c)
{
    uint16_t crc;

    c[0] = TFW_BOOT_CONFIRM_MAGIC;
    tfw_boot_flash_read(TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT,
                        &c[TFW_BOOT_CONFIRM_O_MEAS], TFW_BOOT_CONFIRM_MEAS_LEN);
    crc = tfw_crc16(c, TFW_BOOT_CONFIRM_O_CRC);
    c[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)crc;
    c[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(crc >> 8);
    return 1u;
}

void setUp(void)
{
    tfw_boot_mock_reset();
    seed();
}

void tearDown(void) {}

static void assert_outside_unchanged(const uint8_t *was, size_t n)
{
    const uint8_t *flash = tfw_boot_mock_flash();

    size_t         i;

    /* The swap's area runs from the primary to the secondary's end: the
     * primary slides up one unit into the gap between them. Byte by byte:
     * either span outside it may be empty, and Unity's memory compare
     * refuses a length of zero. */
    for (i = 0u; i < n; i++) {
        if (i == PRI) {
            i = SEC + SLOT;
        }
        if (i < n && was[i] != flash[i]) {
            TEST_FAIL_MESSAGE("flash outside the two slots changed");
        }
    }
}

static void test_a_hostile_journal_never_moves_flash_outside_the_slots(void)
{
    size_t   n      = tfw_boot_mock_flash_size();
    uint32_t i;
    unsigned moved  = 0u;

    TEST_ASSERT_TRUE(n <= sizeof before);
    TEST_ASSERT_TRUE(PRI + SLOT <= SEC);
    TEST_ASSERT_TRUE(SEC + SLOT <= n);
    rng = 0xB5297A4DuL;
    for (i = 0u; i < ITERS; i++) {
        uint8_t                 jr[2u * COPY];
        uint8_t                 conf[TFW_BOOT_CONFIRM_LEN];
        tfw_boot_reset_action_t a;
        unsigned                writes;

        setUp();
        hostile(jr, COPY, journal_copy);
        hostile(&jr[COPY], COPY, journal_copy);
        hostile(conf, sizeof conf, confirm_record);
        memcpy(&tfw_boot_mock_nvm()[REC], jr, sizeof jr);
        memcpy(&tfw_boot_mock_nvm()[TFW_BOOT_CONFIRM_ADDR], conf, sizeof conf);
        memset(&plat, 0, sizeof plat);
        plat.boot_state = (uint8_t)(next() % 6u);
        plat.ota_state  = (uint8_t)(next() % 6u);
        memcpy(before, tfw_boot_mock_flash(), n);
        writes = tfw_boot_mock_page_writes();

        a = tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
        TEST_ASSERT_TRUE(a == TFW_BOOT_RESET_FAIL || a == TFW_BOOT_RESET_SETTLE ||
                         a == TFW_BOOT_RESET_TRY || a == TFW_BOOT_RESET_BOOT);
        assert_outside_unchanged(before, n);
        moved += (unsigned)(tfw_boot_mock_page_writes() != writes);
    }
    TEST_ASSERT_TRUE(moved > 0u);
    TEST_ASSERT_TRUE(moved < ITERS);
}

/* After the hostile runs, an erased journal swaps the slots whole. */
static void test_an_erased_journal_after_the_hostile_runs_swaps_whole(void)
{
    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    TEST_ASSERT_EQUAL_MEMORY(img_b, &tfw_boot_mock_flash()[PRI], SLOT);
    TEST_ASSERT_EQUAL_MEMORY(img_a, &tfw_boot_mock_flash()[SEC], SLOT);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_swap_in_progress());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_hostile_journal_never_moves_flash_outside_the_slots);
    RUN_TEST(test_an_erased_journal_after_the_hostile_runs_swaps_whole);
    return UNITY_END();
}
