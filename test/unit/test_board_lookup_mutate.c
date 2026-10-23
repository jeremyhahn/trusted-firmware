/* Board lookup under mutation (issue #163): src/core/board_lookup.c asked
 * about every pin id a wire byte can carry, 0..255, in order and in a seeded
 * random order, against the host board's table.
 *
 * What is pinned: an id at or past TFW_PIN_COUNT answers TFW_ERR_BAD_PIN from
 * every lookup that returns a status, 0 from the two that return a byte, and
 * writes nothing to the caller's buffer -- never the answer for another pin. A
 * known id answers the same every time and the same through every lookup (the
 * CAPS page agrees with tfw_board_pin, tfw_board_adc_channel and tfw_board_pwm),
 * and no two known ids resolve to the same port and bit, ADC channel or PWM
 * output -- so no id is ever read as a different known pin. test_lookup.c pins
 * the table's contents; this suite pins the edges around it.
 *
 * Deterministic: the random order comes from xorshift32 seeded with a literal,
 * and a failure prints the seed and the id.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "tfw/board.h"

#define SEED   0xB0A2D1D5u
#define ROUNDS 64u
#define PAGE   6u

static uint32_t rng;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void) {}
void tearDown(void) {}

static void fail_at(const char *what, unsigned pin)
{
    printf("seed 0x%08lx: pin %u: %s\n", (unsigned long)SEED, pin, what);
    TEST_FAIL_MESSAGE(what);
}

/* Every answer for one id, each into a buffer pre-filled with fill. */
typedef struct {
    tfw_status_t st_pin, st_adc, st_pwm, st_page;
    tfw_pinref_t ref;
    tfw_pwmref_t pwm;
    uint8_t      adc;
    uint8_t      page[PAGE];
    uint8_t      flags, pwm_state;
} answer_t;

static void ask(unsigned pin, uint8_t fill, answer_t *a)
{
    memset(a, fill, sizeof *a);
    a->st_pin    = tfw_board_pin((tfw_pin_t)pin, &a->ref);
    a->st_adc    = tfw_board_adc_channel((tfw_pin_t)pin, &a->adc);
    a->st_pwm    = tfw_board_pwm((tfw_pin_t)pin, &a->pwm);
    a->st_page   = tfw_board_pin_page((tfw_pin_t)pin, a->page);
    a->flags     = tfw_board_pin_flags((tfw_pin_t)pin);
    a->pwm_state = tfw_board_pin_pwm_state((tfw_pin_t)pin);
}

static int untouched(const void *p, size_t n, uint8_t fill)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t         i;
    for (i = 0u; i < n; i++) {
        if (b[i] != fill) { return 0; }
    }
    return 1;
}

static void check_unknown(unsigned pin, uint8_t fill)
{
    answer_t a;

    ask(pin, fill, &a);
    if (a.st_pin != TFW_ERR_BAD_PIN || a.st_adc != TFW_ERR_BAD_PIN || a.st_pwm != TFW_ERR_BAD_PIN
        || a.st_page != TFW_ERR_BAD_PIN) {
        fail_at("an unknown id did not answer BAD_PIN", pin);
    }
    if (a.flags != 0u || a.pwm_state != 0u) {
        fail_at("an unknown id answered flags or a pwm state", pin);
    }
    if (!untouched(&a.ref, sizeof a.ref, fill) || !untouched(&a.pwm, sizeof a.pwm, fill)
        || !untouched(&a.adc, sizeof a.adc, fill) || !untouched(a.page, sizeof a.page, fill)) {
        fail_at("an unknown id wrote to the caller's buffer", pin);
    }
}

static void check_known(unsigned pin, const answer_t *first)
{
    answer_t a;

    ask(pin, 0x00u, &a);
    if (a.st_page != TFW_OK || a.page[0] != pin) {
        fail_at("a known id's page is not its own", pin);
    }
    if (a.page[1] != a.flags || a.page[5] != a.pwm_state) {
        fail_at("the page's flags or pwm state disagree with the lookups", pin);
    }
    if ((a.st_pin == TFW_OK) != (a.page[2] != TFW_WIRE_PORT_NONE)
        || (a.st_pin != TFW_OK && a.st_pin != TFW_ERR_UNSUPPORTED)) {
        fail_at("tfw_board_pin disagrees with the page's port", pin);
    }
    if ((a.st_adc == TFW_OK) != (a.page[4] != TFW_ADC_NONE)
        || (a.st_adc == TFW_OK && a.adc != a.page[4])
        || (a.st_adc != TFW_OK && a.st_adc != TFW_ERR_UNSUPPORTED)) {
        fail_at("tfw_board_adc_channel disagrees with the page", pin);
    }
    if (a.st_pwm != TFW_OK && a.st_pwm != TFW_ERR_UNSUPPORTED) {
        fail_at("tfw_board_pwm answered neither OK nor UNSUPPORTED", pin);
    }
    if (first != NULL) {
        answer_t b;
        ask(pin, 0x00u, &b);
        if (memcmp(&a, &b, sizeof a) != 0 || memcmp(&a, first, sizeof a) != 0) {
            fail_at("a known id answered differently on another call", pin);
        }
    }
}

/* ------------------------------------------------------------------ cases */

static void test_board_lookup_mutate_every_unknown_id_is_bad_pin_and_writes_nothing(void)
{
    static const uint8_t fills[3] = { 0x00u, 0xA5u, 0xFFu };
    unsigned pin, f;

    TEST_ASSERT_TRUE(tfw_board_pin_count() > 0u);
    for (pin = tfw_board_pin_count(); pin < 256u; pin++) {
        for (f = 0u; f < 3u; f++) {
            check_unknown(pin, fills[f]);
        }
    }
}

static void test_board_lookup_mutate_every_known_id_agrees_with_itself(void)
{
    unsigned pin;

    for (pin = 0u; pin < tfw_board_pin_count(); pin++) {
        check_known(pin, NULL);
    }
}

/* No two known ids resolve to the same port and bit, ADC channel or PWM. */
static void test_board_lookup_mutate_no_two_ids_resolve_to_the_same_thing(void)
{
    const unsigned n = tfw_board_pin_count();
    answer_t      *all = (answer_t *)malloc(sizeof(answer_t) * n);
    unsigned       i, j;

    TEST_ASSERT_NOT_NULL(all);
    for (i = 0u; i < n; i++) {
        ask(i, 0x00u, &all[i]);
    }
    for (i = 0u; i < n; i++) {
        for (j = i + 1u; j < n; j++) {
            if (all[i].st_pin == TFW_OK && all[j].st_pin == TFW_OK
                && all[i].page[2] == all[j].page[2] && all[i].page[3] == all[j].page[3]) {
                fail_at("two ids share a port and bit", j);
            }
            if (all[i].st_pin == TFW_OK && all[j].st_pin == TFW_OK
                && memcmp(&all[i].ref, &all[j].ref, sizeof all[i].ref) == 0) {
                fail_at("two ids resolve to one pin reference", j);
            }
            if (all[i].st_adc == TFW_OK && all[j].st_adc == TFW_OK && all[i].adc == all[j].adc) {
                fail_at("two ids share an ADC channel", j);
            }
            if (all[i].st_pwm == TFW_OK && all[j].st_pwm == TFW_OK
                && memcmp(&all[i].pwm, &all[j].pwm, sizeof all[i].pwm) == 0) {
                fail_at("two ids share a PWM output", j);
            }
        }
    }
    free(all);
}

/* Every id in a seeded random order, ROUNDS times: a known id answers what it
 * answered first, an unknown one BAD_PIN, whatever was asked before it. */
static void test_board_lookup_mutate_random_order_changes_no_answer(void)
{
    const unsigned n = tfw_board_pin_count();
    answer_t      *first = (answer_t *)malloc(sizeof(answer_t) * n);
    unsigned       r, k, pin;

    TEST_ASSERT_NOT_NULL(first);
    for (pin = 0u; pin < n; pin++) {
        ask(pin, 0x00u, &first[pin]);
    }
    rng = SEED;
    for (r = 0u; r < ROUNDS; r++) {
        for (k = 0u; k < 256u; k++) {
            pin = (unsigned)(rnd() & 0xFFu);
            if (pin < n) {
                check_known(pin, &first[pin]);
            } else {
                check_unknown(pin, (uint8_t)rnd());
            }
        }
    }
    free(first);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_board_lookup_mutate_every_unknown_id_is_bad_pin_and_writes_nothing);
    RUN_TEST(test_board_lookup_mutate_every_known_id_agrees_with_itself);
    RUN_TEST(test_board_lookup_mutate_no_two_ids_resolve_to_the_same_thing);
    RUN_TEST(test_board_lookup_mutate_random_order_changes_no_answer);
    return UNITY_END();
}
