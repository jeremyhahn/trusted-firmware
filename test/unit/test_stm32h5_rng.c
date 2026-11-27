/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 RNG read's own error recovery (hal_rng.c tfw_h5_rng_read(),
 * #599): a word after one recovered latched error is returned, an error that
 * comes straight back is reported without a second recovery, and a zero word
 * after a recovery is refused with the caller's word left as it was. A word
 * read while a seed error latches is dropped and read again after one
 * recovery; a clock error after the read keeps the word.
 */
#include <stdint.h>

#include "unity.h"

#include "soc_regs.h"

/*
 * Scripted RNG_SR. Every RNG_SR access goes through rng_sr_cell(): it first
 * commits the previous access (a cell that no longer holds what was staged
 * was written: log it, and give its unused script value to the next access),
 * then stages the next script value, tagged with the access number in bits
 * 8-23, in the SR cell and returns its index. A driver write copies an older
 * read's tag, so it never equals the staged value. The script's last value
 * repeats. RNG_CR and RNG_DR are plain cells.
 */
#define SR_CELL   4u
#define SR_TAG(n) (((n) & 0xFFFFu) << 8)
#define SR_MAX    16u
#define WR_MAX    8u

static volatile uint32_t regs[8];
static uint32_t sr_script[SR_MAX];
static uint32_t sr_len, sr_pos, sr_seq, sr_staged, sr_armed;
static uint32_t sr_writes[WR_MAX];
static uint32_t sr_nwrites;                 /* may exceed WR_MAX: overflow */

static void rng_sr_commit(void)
{
    if (sr_armed && regs[SR_CELL] != sr_staged) {
        if (sr_nwrites < WR_MAX) {
            sr_writes[sr_nwrites] = regs[SR_CELL];
        }
        sr_nwrites++;
        sr_pos--;
    }
    sr_armed = 0u;
}

static uint32_t rng_sr_cell(void)
{
    rng_sr_commit();
    sr_staged = sr_script[sr_pos < sr_len ? sr_pos : sr_len - 1u] |
                SR_TAG(++sr_seq);
    sr_pos++;
    regs[SR_CELL] = sr_staged;
    sr_armed = 1u;
    return SR_CELL;
}

#undef TFW_H5_RNG_SR_IX
#define TFW_H5_RNG_SR_IX (rng_sr_cell())
#include "hal_rng.c"

#define DRDY TFW_H5_RNG_SR_DRDY
#define CEIS TFW_H5_RNG_SR_CEIS
#define SEIS TFW_H5_RNG_SR_SEIS
#define SECS TFW_H5_RNG_SR_SECS
#define OUT0 0xA5A5A5A5u

static volatile uint32_t rcc_cr, rcc_ahb2enr;
static const tfw_h5_rng_t rng = { regs, &rcc_cr, &rcc_ahb2enr };

/* The script: the init's status read (DRDY), then the read's statuses. */
static void script(uint32_t a, uint32_t b, uint32_t c)
{
    sr_script[0] = DRDY;
    sr_script[1] = a;
    sr_script[2] = b;
    sr_script[3] = c;
    sr_len = 4u;
}

static int run_read(uint32_t dr, uint32_t *out)
{
    int rc;

    TEST_ASSERT_EQUAL_INT(0, tfw_h5_rng_init(&rng));
    regs[TFW_H5_RNG_DR_IX] = dr;
    rc = tfw_h5_rng_read(&rng, out);
    rng_sr_commit();                        /* the last access may be a write */
    return rc;
}

void setUp(void)
{
    for (uint32_t i = 0u; i < 8u; i++) {
        regs[i] = 0u;
    }
    for (uint32_t i = 0u; i < WR_MAX; i++) {
        sr_writes[i] = 0u;
    }
    sr_len = sr_pos = sr_seq = sr_staged = sr_armed = sr_nwrites = 0u;
    rcc_cr = TFW_H5_RCC_CR_HSI48RDY;
    rcc_ahb2enr = 0u;
}

void tearDown(void) {}

static void check_one_recovery_returns_word(uint32_t latched)
{
    uint32_t out = OUT0;

    setUp();
    script(latched | DRDY, DRDY, DRDY);
    TEST_ASSERT_EQUAL_INT(0, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(0x5eed1234u, out);
    TEST_ASSERT_EQUAL_UINT32(1u, sr_nwrites);
    TEST_ASSERT_EQUAL_HEX32(0u, sr_writes[0] & (CEIS | SEIS));
    TEST_ASSERT_EQUAL_HEX32(DRDY, sr_writes[0] & DRDY);
    TEST_ASSERT_EQUAL_HEX32(0u, regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RNG_CR_RNGEN,
                            regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN);
}

static void test_a_read_recovers_one_latched_error_and_returns_the_word(void)
{
    check_one_recovery_returns_word(CEIS);
    check_one_recovery_returns_word(SEIS);  /* with the CONDRST pulse */
}

static void
test_an_error_that_comes_back_after_a_recovery_is_reported_not_recovered_again(void)
{
    uint32_t out = OUT0;

    script(CEIS | DRDY, DRDY, CEIS | DRDY);  /* the last one repeats */
    TEST_ASSERT_EQUAL_INT(-1, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(OUT0, out);
    TEST_ASSERT_EQUAL_UINT32(1u, sr_nwrites);  /* one recovery, no more */
}

static void test_a_zero_word_after_a_recovery_is_refused_and_leaves_out(void)
{
    uint32_t out = OUT0;

    script(CEIS | DRDY, DRDY, DRDY);
    TEST_ASSERT_EQUAL_INT(-1, run_read(0u, &out));
    TEST_ASSERT_EQUAL_HEX32(OUT0, out);
}

/*
 * After the DR read: RNG_SR is looked at again, and a word taken while a seed
 * error latches is dropped and read again after one recovery, as ST's HAL
 * does. A clock error there keeps the word.
 */
static void check_seed_error_after_read_drops_word(uint32_t after)
{
    uint32_t out = OUT0;

    setUp();
    script(DRDY, after | DRDY, DRDY);
    TEST_ASSERT_EQUAL_INT(0, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(0x5eed1234u, out);
    TEST_ASSERT_EQUAL_UINT32(1u, sr_nwrites);  /* one recovery */
    TEST_ASSERT_EQUAL_HEX32(0u, sr_writes[0] & SEIS);
    TEST_ASSERT_EQUAL_HEX32(0u, regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RNG_CR_RNGEN,
                            regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN);
}

static void
test_a_seed_error_latched_by_the_read_drops_the_word_and_recovers_once(void)
{
    check_seed_error_after_read_drops_word(SEIS);
}

static void
test_a_seed_error_after_the_read_that_stays_is_reported_and_leaves_out(void)
{
    uint32_t out = OUT0;

    script(DRDY, SEIS | DRDY, SEIS | DRDY);  /* the last one repeats */
    TEST_ASSERT_EQUAL_INT(-1, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(OUT0, out);
}

static void test_a_seed_error_now_after_the_read_drops_the_word(void)
{
    /* SECS alone would be written back unchanged, which is not logged. */
    check_seed_error_after_read_drops_word(SECS | SEIS);
}

static void test_a_clock_error_after_the_read_keeps_the_word(void)
{
    uint32_t out = OUT0;

    script(DRDY, CEIS | DRDY, CEIS | DRDY);
    TEST_ASSERT_EQUAL_INT(0, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(0x5eed1234u, out);
    TEST_ASSERT_EQUAL_UINT32(0u, sr_nwrites);
}

/* The word's one recovery was spent at the top (a clock error before the
 * word was ready): a seed error the read then latches is reported, not
 * recovered a second time and the next word returned. */
static void
test_a_seed_error_after_the_read_once_an_error_was_recovered_is_reported(void)
{
    uint32_t out = OUT0;

    sr_script[0] = DRDY;                    /* init */
    sr_script[1] = CEIS | DRDY;             /* top-branch recovery */
    sr_script[2] = DRDY;                    /* its poll */
    sr_script[3] = DRDY;                    /* word ready, DR read */
    sr_script[4] = SEIS | DRDY;             /* seed error after the read */
    sr_script[5] = DRDY;
    sr_script[6] = DRDY;
    sr_len = 7u;
    TEST_ASSERT_EQUAL_INT(-1, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(OUT0, out);
    TEST_ASSERT_EQUAL_UINT32(1u, sr_nwrites);  /* one recovery, no more */
}

/* The word's one recovery was spent on a seed error after the read: the
 * same error after the next read is reported, not recovered again. */
static void
test_a_second_seed_error_after_a_read_is_reported_not_recovered_again(void)
{
    uint32_t out = OUT0;

    sr_script[0] = DRDY;                    /* init */
    sr_script[1] = DRDY;                    /* word ready, DR read */
    sr_script[2] = SEIS | DRDY;             /* seed error: recovery 1 */
    sr_script[3] = DRDY;                    /* its poll */
    sr_script[4] = DRDY;                    /* word ready again */
    sr_script[5] = SEIS | DRDY;             /* seed error again */
    sr_script[6] = DRDY;
    sr_script[7] = DRDY;
    sr_len = 8u;
    TEST_ASSERT_EQUAL_INT(-1, run_read(0x5eed1234u, &out));
    TEST_ASSERT_EQUAL_HEX32(OUT0, out);
    TEST_ASSERT_EQUAL_UINT32(1u, sr_nwrites);  /* one recovery, no more */
    TEST_ASSERT_EQUAL_HEX32(0u, regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RNG_CR_RNGEN,
                            regs[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_read_recovers_one_latched_error_and_returns_the_word);
    RUN_TEST(test_an_error_that_comes_back_after_a_recovery_is_reported_not_recovered_again);
    RUN_TEST(test_a_zero_word_after_a_recovery_is_refused_and_leaves_out);
    RUN_TEST(test_a_seed_error_latched_by_the_read_drops_the_word_and_recovers_once);
    RUN_TEST(test_a_seed_error_after_the_read_that_stays_is_reported_and_leaves_out);
    RUN_TEST(test_a_seed_error_now_after_the_read_drops_the_word);
    RUN_TEST(test_a_clock_error_after_the_read_keeps_the_word);
    RUN_TEST(test_a_seed_error_after_the_read_once_an_error_was_recovered_is_reported);
    RUN_TEST(test_a_second_seed_error_after_a_read_is_reported_not_recovered_again);
    return UNITY_END();
}
