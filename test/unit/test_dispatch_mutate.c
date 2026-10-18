/* Seeded mutation test for the command dispatcher's payload decoding.
 *
 * tfw_dispatch is the first parser past the frame decoder: every handler in
 * src/core/commands.def decodes its arguments from a reader over bytes the
 * host chose. This feeds every command this build knows, and every id it does
 * not, payloads made by mutating a seed (bit flips, truncation at every
 * length, each byte and each u16 set to the lengths around what follows it,
 * appended bytes) plus random payloads of every length up to one past the
 * frame maximum. Each payload sits in a heap buffer of exactly its length and
 * each reply in one of exactly TFW_MAX_PAYLOAD, so under make test-asan a
 * read or write one byte past either is a failure, not a quiet pass.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the command id and the payload, so the case can be pinned. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "sched.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/status.h"
#include "vectors.h"

#define RANDOM_MUTANTS   400u /* per known command */
#define RANDOM_PER_LEN   4u   /* pure random payloads per length, per known command */
#define UNKNOWN_PER_ID   3u   /* random payloads per unknown id */
#define PAYLOAD_LIMIT    (TFW_MAX_PAYLOAD + 1u)

static uint32_t rng;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

typedef struct {
    uint8_t       cmd;
    uint8_t       len;
    const uint8_t *bytes;
} seed_t;

static const uint8_t tone_seed[3]  = { TFW_TONE_PIN, 0xB8u, 0x01u };       /* 440 Hz */
static const uint8_t timer_seed[6] = { 0x0Du, 0x01u, 0x64u, 0u, 0u, 0u };  /* pin 13 high, 100 ms */

/* Valid requests where a zero-filled one would be refused before its decoding
 * got interesting. Any command not listed is seeded with zeros at the shortest
 * length the dispatcher lets through. */
static const seed_t seeds[] = {
    { TFW_CMD_PING,          5u, (const uint8_t *)"hello" },
    { TFW_CMD_CAPS,          1u, (const uint8_t *)"\x0d" },
    { TFW_CMD_RESET,         1u, (const uint8_t *)"\x00" },
    { TFW_CMD_PIN_MODE,      2u, (const uint8_t *)"\x0d\x01" },
    { TFW_CMD_DIGITAL_READ,  1u, (const uint8_t *)"\x0d" },
    { TFW_CMD_DIGITAL_WRITE, 2u, (const uint8_t *)"\x0d\x01" },
    { TFW_CMD_TIMER_SET,     6u, timer_seed },
    { TFW_CMD_TIMER_CANCEL,  2u, (const uint8_t *)"\x0d\x01" },
    { TFW_CMD_TONE,          3u, tone_seed },
    { TFW_CMD_EEPROM_READ,   3u, (const uint8_t *)"\x10\x00\x04" },
    { TFW_CMD_EEPROM_WRITE,  6u, (const uint8_t *)"\x10\x00\x01\x02\x03\x04" },
};

/* Commands that cannot answer TFW_OK against the mock whatever the payload:
 * the pulse pair wait for an edge the mock pin never makes, ATTEST needs a
 * provisioned identity and ATTEST_CERT an asymmetric key this build lacks.
 * Their decoding is still reached -- each must still get past the arity check. */
static const uint8_t never_ok_on_the_mock[] = {
    TFW_CMD_PULSE_IN, TFW_CMD_PULSE_CAPTURE, TFW_CMD_ATTEST, TFW_CMD_ATTEST_CERT,
};

typedef struct {
    unsigned runs;
    unsigned ok;
    unsigned refused;
    uint32_t seen; /* bit s set once status s came back */
} tally_t;

static tally_t tally[256];
static uint8_t cur_cmd;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_sched_init();
}
void tearDown(void) {}

static void dump(const uint8_t *p, unsigned n)
{
    unsigned i;
    printf("cmd 0x%02x payload[%u]:", cur_cmd, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* One request, in exactly-sized heap buffers, under the contract every call
 * must keep whatever the bytes were. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *payload, unsigned n)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, payload, n); }
    setUp();
    if (cmd == TFW_CMD_TIMER_CANCEL) {
        /* something to cancel: the TIMER_SET seed's slot on pin 13 */
        tfw_reader_init(&r, timer_seed, (uint8_t)sizeof timer_seed);
        tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_TIMER_SET, &r, &w));
    }
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);
    cur_cmd = cmd;

    st = tfw_dispatch(cmd, &r, &w);

    if ((unsigned)st > (unsigned)TFW_ERR_BAD_STATE
        || tfw_writer_len(&w) > TFW_MAX_PAYLOAD
        || (!tfw_dispatch_knows(cmd) && st != TFW_ERR_UNKNOWN_CMD)
        || (tfw_dispatch_knows(cmd) && st == TFW_ERR_UNKNOWN_CMD)
        || (n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC && st != TFW_ERR_UNKNOWN_CMD)) {
        printf("status %u, writer %u\n", (unsigned)st, (unsigned)tfw_writer_len(&w));
        dump(payload, n);
        TEST_FAIL_MESSAGE("dispatch broke its contract");
    }
    TEST_ASSERT_EQUAL_UINT8(cmd, tfw_dispatch_last());

    tally[cmd].runs++;
    tally[cmd].seen |= (uint32_t)1u << (unsigned)st;
    if (st == TFW_OK) {
        tally[cmd].ok++;
    } else {
        tally[cmd].refused++;
    }
    free(in);
    free(out);
    return st;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

/* The seed for a known command: the table's, or zeros at the shortest length
 * the dispatcher's arity check lets through. */
static unsigned seed_for(uint8_t cmd, uint8_t *buf)
{
    unsigned i;
    unsigned n;
    for (i = 0u; i < sizeof seeds / sizeof seeds[0]; i++) {
        if (seeds[i].cmd == cmd) {
            memcpy(buf, seeds[i].bytes, seeds[i].len);
            return seeds[i].len;
        }
    }
    memset(buf, 0, PAYLOAD_LIMIT);
    for (n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
        if (feed(cmd, buf, n) != TFW_ERR_BAD_ARGC) {
            return n;
        }
    }
    return 0u;
}

static void mutate_known(uint8_t cmd)
{
    uint8_t  seed[PAYLOAD_LIMIT];
    uint8_t  buf[PAYLOAD_LIMIT];
    unsigned sn;
    unsigned n;
    unsigned i;
    unsigned k;

    sn = seed_for(cmd, seed);
    feed(cmd, seed, sn);

    /* truncation at every length, then the seed extended to every length */
    for (n = 0u; n < sn; n++) {
        feed(cmd, seed, n);
    }
    for (n = sn + 1u; n <= PAYLOAD_LIMIT; n++) {
        memcpy(buf, seed, sn);
        random_fill(buf + sn, n - sn);
        feed(cmd, buf, n);
    }

    /* every byte and every little-endian u16 set to the values a length field
     * goes wrong at: 0, the bytes that follow it, one either side, all ones */
    for (i = 0u; i < sn; i++) {
        const unsigned rest = sn - i - 1u;
        const unsigned vals[6] = { 0u, rest, rest + 1u, rest - 1u, 0xFFu, 0xFFFFu };
        for (k = 0u; k < 6u; k++) {
            memcpy(buf, seed, sn);
            buf[i] = (uint8_t)vals[k];
            feed(cmd, buf, sn);
            if (i + 1u < sn) {
                buf[i + 1u] = (uint8_t)(vals[k] >> 8);
                feed(cmd, buf, sn);
            }
        }
    }

    /* bit flips, then a random cut or a random tail */
    for (k = 0u; k < RANDOM_MUTANTS; k++) {
        unsigned flips = 1u + rnd() % 4u;
        n = sn;
        memcpy(buf, seed, sn);
        while (flips-- && n) {
            const uint32_t at = rnd() % (n * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        switch (rnd() % 3u) {
        case 0u:
            n = n ? rnd() % (n + 1u) : 0u;
            break;
        case 1u: {
            const unsigned add = 1u + rnd() % (PAYLOAD_LIMIT - n + (n == PAYLOAD_LIMIT));
            if (n + add <= PAYLOAD_LIMIT) {
                random_fill(buf + n, add);
                n += add;
            }
            break;
        }
        default:
            break;
        }
        feed(cmd, buf, n);
    }

    /* pure random payloads of every length */
    for (n = 0u; n <= PAYLOAD_LIMIT; n++) {
        for (k = 0u; k < RANDOM_PER_LEN; k++) {
            random_fill(buf, n);
            feed(cmd, buf, n);
        }
    }
}

static void test_mutated_payloads_never_overrun_or_misreport(void)
{
    uint8_t  buf[PAYLOAD_LIMIT];
    unsigned id;
    unsigned k;
    unsigned known = 0u;
    unsigned total = 0u;

    rng = 0x2545F491u;
    memset(tally, 0, sizeof tally);

    for (id = 0u; id < 256u; id++) {
        if (tfw_dispatch_knows((uint8_t)id)) {
            known++;
            mutate_known((uint8_t)id);
        } else {
            feed((uint8_t)id, buf, 0u);
            for (k = 0u; k < UNKNOWN_PER_ID; k++) {
                const unsigned n = rnd() % (PAYLOAD_LIMIT + 1u);
                random_fill(buf, n);
                TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, feed((uint8_t)id, buf, n));
            }
        }
    }

    /* the loop ran, and every known command both took and refused something */
    TEST_ASSERT_TRUE(known > 0u);
    for (id = 0u; id < 256u; id++) {
        total += tally[id].runs;
        if (tfw_dispatch_knows((uint8_t)id)) {
            printf("cmd 0x%02x: %u runs, %u ok, %u refused, statuses 0x%05lx\n", id,
                   tally[id].runs, tally[id].ok, tally[id].refused,
                   (unsigned long)tally[id].seen);
            TEST_ASSERT_TRUE_MESSAGE((tally[id].seen & ~((uint32_t)1u << TFW_ERR_BAD_ARGC)) != 0u,
                                     "a known command never got past the arity check");
            if (!memchr(never_ok_on_the_mock, (int)id, sizeof never_ok_on_the_mock)) {
                TEST_ASSERT_TRUE_MESSAGE(tally[id].ok > 0u, "a known command never accepted a payload");
            }
            TEST_ASSERT_TRUE_MESSAGE(tally[id].refused > 0u, "a known command never refused a payload");
        } else {
            TEST_ASSERT_EQUAL_UINT(1u + UNKNOWN_PER_ID, tally[id].runs);
        }
    }
    printf("%u known commands, %u requests\n", known, total);
}

/* After all that, the simple commands still answer as the vectors pin them. */
static void test_the_dispatcher_still_answers_the_vectors_after_the_run(void)
{
    uint8_t      in[5] = { 'h', 'e', 'l', 'l', 'o' };
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, 5u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_PING, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(5u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 5u);

    tfw_reader_init(&r, in, 0u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_CAPS, &r, &w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_CAPS_PROTO_REPLY, out,
                                  TFWV_CAPS_PROTO_REPLY_LEN);

    tfw_reader_init(&r, (const uint8_t *)"\x0d", 1u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_CAPS, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(TFWV_CAPS_PIN13_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_CAPS_PIN13_REPLY, out,
                                  TFWV_CAPS_PIN13_REPLY_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_payloads_never_overrun_or_misreport);
    RUN_TEST(test_the_dispatcher_still_answers_the_vectors_after_the_run);
    return UNITY_END();
}
