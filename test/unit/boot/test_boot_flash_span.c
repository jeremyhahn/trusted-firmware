/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 boot HAL's aligned read windows (boot/hal/esp32/flash_span.c,
 * #48). bootloader_flash_read refuses a source, length or destination that is
 * not 4-byte aligned; layer 0 asked it for the SUIT envelope's 2-byte length
 * after the trailer and booted nothing. Every window here is one that read
 * may be given, and the bytes copied out of the windows are the bytes asked
 * for, at every offset and length the contract can pass.
 *
 * Under seeded mutation (#163), each field of a window is held to its contract
 * at the edges a fault moves: every cap from 8 to 64, every address residue,
 * the clamp's boundary (want one under, at and one over the room left), the
 * top of the 32-bit address space, and seeded random addresses, wants and
 * caps. Every window is checked field by field against the contract in
 * flash_span.h, worked out by division rather than masks:
 *
 *   - from is addr rounded down to 4, skip the bytes between, 0..3;
 *   - take is want, or the room after skip when want does not fit;
 *   - len is skip + take rounded up to 4, never past cap.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the inputs so the case can be pinned.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "flash_span.h"

#define FLASH_LEN 1024u
#define RANDOM_RUNS 200000u

static uint8_t flash[FLASH_LEN];
static uint32_t rng;
static unsigned ran;
static unsigned clamped;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void start(uint32_t seed)
{
    rng     = seed;
    ran     = 0u;
    clamped = 0u;
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < FLASH_LEN; i++) {
        flash[i] = (uint8_t)(i * 7u + 3u);
    }
}

void tearDown(void) {}

/* As boot_hal_esp32.c reads: window by window, each one checked against
 * what bootloader_flash_read accepts, then copied out. */
static void read_through(uint32_t addr, uint8_t *out, uint32_t want, uint32_t cap)
{
    tfw_flash_span_t s;
    uint32_t         windows = 0u;

    while (want > 0u) {
        tfw_flash_span(addr, want, cap, &s);
        TEST_ASSERT_EQUAL_UINT32(0u, s.from % 4u);
        TEST_ASSERT_EQUAL_UINT32(0u, s.len % 4u);
        TEST_ASSERT_TRUE(s.len <= cap);
        TEST_ASSERT_TRUE(s.skip < 4u);
        TEST_ASSERT_EQUAL_UINT32(addr, s.from + s.skip);
        TEST_ASSERT_TRUE(s.take >= 1u && s.take <= want);
        TEST_ASSERT_TRUE(s.skip + s.take <= s.len);
        TEST_ASSERT_TRUE(s.len - (s.skip + s.take) < 4u);
        TEST_ASSERT_TRUE(s.from + s.len <= FLASH_LEN);
        memcpy(out, &flash[s.from + s.skip], s.take);
        addr += s.take;
        out  += s.take;
        want -= s.take;
        windows++;
        TEST_ASSERT_TRUE(windows <= FLASH_LEN);
    }
}

/* The read that fired: the envelope length at the trailer's end, then the
 * envelope from two bytes past it. */
static void test_the_suit_length_and_envelope_reads_are_aligned(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x90090u, 2u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x90090u, s.from);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    TEST_ASSERT_EQUAL_UINT32(0u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(2u, s.take);

    tfw_flash_span(0x90092u, 128u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x90090u, s.from);
    TEST_ASSERT_EQUAL_UINT32(132u, s.len);
    TEST_ASSERT_EQUAL_UINT32(2u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(128u, s.take);
}

/* An aligned read is one window of exactly itself. */
static void test_an_aligned_read_is_its_own_window(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x20000u, 72u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x20000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(72u, s.len);
    TEST_ASSERT_EQUAL_UINT32(0u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(72u, s.take);
}

/* Longer than the bounce buffer: the first window fills it, no more. */
static void test_a_read_past_the_buffer_is_chunked(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x1001u, 4096u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(256u, s.len);
    TEST_ASSERT_EQUAL_UINT32(1u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(255u, s.take);
}

/* Every offset and length the contract's uint8_t length allows, through the
 * HAL's buffer and through the smallest one the header permits: the bytes
 * out are the bytes asked for, and nothing around them is written. */
static void test_every_offset_and_length_reads_its_bytes(void)
{
    static const uint32_t caps[] = { 256u, 8u };
    uint8_t  out[260];
    uint32_t c;
    uint32_t addr;
    uint32_t want;

    for (c = 0u; c < sizeof caps / sizeof caps[0]; c++) {
        for (addr = 0u; addr < 8u; addr++) {
            for (want = 1u; want <= 255u; want++) {
                memset(out, 0xA5, sizeof out);
                read_through(addr + 16u, &out[2], want, caps[c]);
                TEST_ASSERT_EQUAL_MEMORY(&flash[addr + 16u], &out[2], want);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[0]);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[1]);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[2u + want]);
            }
        }
    }
}

/* One window against the contract; want is not 0, cap a multiple of 4, >= 8. */
static void check(uint32_t addr, uint32_t want, uint32_t cap)
{
    tfw_flash_span_t s;
    const uint32_t   skip = addr % 4u;
    const uint32_t   room = cap - skip;
    const uint32_t   take = want < room ? want : room;
    const uint32_t   len  = ((skip + take + 3u) / 4u) * 4u;
    const char      *fail = NULL;

    s.from = 0xDEADBEEFu;
    s.len  = 0xDEADBEEFu;
    s.skip = 0xDEADBEEFu;
    s.take = 0xDEADBEEFu;
    tfw_flash_span(addr, want, cap, &s);

    if (s.from != addr - skip) {
        fail = "from is not addr rounded down to 4";
    } else if (s.skip != skip) {
        fail = "skip is not the bytes before addr";
    } else if (s.take != take) {
        fail = "take is not want, clamped to the room after skip";
    } else if (s.len != len) {
        fail = "len is not skip + take rounded up to 4";
    } else if (s.len > cap) {
        fail = "len runs past the bounce buffer";
    } else if (s.take == 0u) {
        fail = "a window that carries no byte";
    }
    if (fail) {
        printf("addr 0x%08x want %u cap %u: from 0x%08x len %u skip %u take %u\n",
               (unsigned)addr, (unsigned)want, (unsigned)cap, (unsigned)s.from,
               (unsigned)s.len, (unsigned)s.skip, (unsigned)s.take);
        TEST_FAIL_MESSAGE(fail);
    }
    ran++;
    if (take != want) {
        clamped++;
    }
}

/* The clamp's boundary: want one under, at and one over the room after skip,
 * for every cap up to 64 and every residue. */
static void test_every_cap_and_residue_at_the_clamp(void)
{
    uint32_t cap;
    uint32_t r;
    uint32_t d;

    start(0u);
    for (cap = 8u; cap <= 64u; cap += 4u) {
        for (r = 0u; r < 8u; r++) {
            const uint32_t addr = 0x10000u + r;
            const uint32_t room = cap - (r % 4u);
            for (d = 0u; d < 3u; d++) {
                check(addr, room - 1u + d, cap);
            }
            check(addr, 1u, cap);
            check(addr, 2u, cap);
            check(addr, 3u, cap);
            check(addr, 4u, cap);
            check(addr, 5u, cap);
            check(addr, 0xFFFFFFFFu, cap);
        }
    }
    TEST_ASSERT_EQUAL_UINT(15u * 8u * 9u, ran);
    TEST_ASSERT_TRUE(clamped > 0u);
    printf("clamp edges: %u windows, %u clamped\n", ran, clamped);
}

/* The windows at the literal edges, pinned field by field. */
static void test_the_edges_pinned(void)
{
    tfw_flash_span_t s;

    /* want exactly the room: no clamp, a full window */
    tfw_flash_span(0x1003u, 5u, 8u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(3u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(5u, s.take);
    TEST_ASSERT_EQUAL_UINT32(8u, s.len);

    /* one over the room: clamped to it */
    tfw_flash_span(0x1003u, 6u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(5u, s.take);
    TEST_ASSERT_EQUAL_UINT32(8u, s.len);

    /* one under the room: the window still rounds up to 8 */
    tfw_flash_span(0x1003u, 4u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(4u, s.take);
    TEST_ASSERT_EQUAL_UINT32(8u, s.len);

    /* a single byte at each residue is a 4-byte window */
    tfw_flash_span(0x1000u, 1u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    tfw_flash_span(0x1001u, 1u, 8u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(1u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    tfw_flash_span(0x1002u, 1u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(2u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    tfw_flash_span(0x1003u, 1u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(3u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    tfw_flash_span(0x1003u, 2u, 8u, &s);
    TEST_ASSERT_EQUAL_UINT32(8u, s.len);

    /* an address with bit 2 set stays at its own word, not the 8-byte one */
    tfw_flash_span(0x1006u, 2u, 8u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x1004u, s.from);
    TEST_ASSERT_EQUAL_UINT32(2u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);

    /* the top of the address space: no wrap of from */
    tfw_flash_span(0xFFFFFFFFu, 1u, 8u, &s);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFCu, s.from);
    TEST_ASSERT_EQUAL_UINT32(3u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(1u, s.take);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);

    /* the largest want through the smallest buffer, aligned */
    tfw_flash_span(0x2000u, 0xFFFFFFFFu, 8u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x2000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(0u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(8u, s.take);
    TEST_ASSERT_EQUAL_UINT32(8u, s.len);
}

/* Seeded random addresses, wants and caps over the whole 32-bit range. */
static void test_random_windows(void)
{
    unsigned k;

    start(0x9E3779B9u);
    for (k = 0u; k < RANDOM_RUNS; k++) {
        const uint32_t addr = rnd();
        const uint32_t cap  = 8u + (rnd() % 1023u) * 4u;
        uint32_t       want;

        switch (rnd() % 3u) {
        case 0u:
            want = 1u + rnd() % (cap + 8u);
            break;
        case 1u:
            want = rnd() | 1u;
            break;
        default:
            want = cap - (addr % 4u) + (rnd() % 3u) - 1u;
            break;
        }
        check(addr, want, cap);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, ran);
    TEST_ASSERT_TRUE(clamped > 0u && clamped < ran);
    printf("random windows: %u windows, %u clamped\n", ran, clamped);
}

/* A whole read advanced window by window covers each asked-for byte once. */
static void test_a_chunked_read_covers_every_byte_once(void)
{
    tfw_flash_span_t s;
    uint32_t         addr = 0x7FFFFFF1u;
    uint32_t         want = 1000u;
    uint32_t         next = addr;
    unsigned         windows = 0u;

    while (want > 0u) {
        tfw_flash_span(addr, want, 12u, &s);
        TEST_ASSERT_EQUAL_HEX32(next, s.from + s.skip);
        addr += s.take;
        next += s.take;
        want -= s.take;
        windows++;
        TEST_ASSERT_TRUE(windows <= 1000u);
    }
    /* 11 bytes in the first window, then 12 per window */
    TEST_ASSERT_EQUAL_UINT(1u + (989u + 11u) / 12u, windows);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_suit_length_and_envelope_reads_are_aligned);
    RUN_TEST(test_an_aligned_read_is_its_own_window);
    RUN_TEST(test_a_read_past_the_buffer_is_chunked);
    RUN_TEST(test_every_offset_and_length_reads_its_bytes);
    RUN_TEST(test_every_cap_and_residue_at_the_clamp);
    RUN_TEST(test_the_edges_pinned);
    RUN_TEST(test_random_windows);
    RUN_TEST(test_a_chunked_read_covers_every_byte_once);
    return UNITY_END();
}
