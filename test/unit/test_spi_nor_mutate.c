/*
 * The SPI NOR store driver (src/store/spi_nor.c, #163) under seeded mutation,
 * linked as test_spi_nor is: random reads, programs and erases at the edges
 * that matter (0, 1, the page and sector edges, the device size, the size + 1
 * and past 24 bits), against the software part answering mutated ID and
 * status bytes and failing transfers. A plain model here -- erase sets 0xFF,
 * program only clears bits -- is the answer every call is checked against.
 *
 * The part takes 24 address bits and the software part wraps them at its
 * size, so the byte a request names is (addr + i) mod the device size; the
 * driver knows no size, and bounding the address is its caller's job.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/status.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "spi_nor_hal.h"
#include "ota_store.h"

#define SEED        0x5EED0163u
#define ITERATIONS  3000u
#define PROBES      2000u
#define SIZE        TFW_NOR_MODEL_SIZE
#define PAGE        256u
#define SECTOR      4096u
#define OP_STATUS   0x05u
#define ST_WIP      0x01u

static uint32_t rng;
static uint8_t  shadow[SIZE];
static uint32_t want_violations;
static uint32_t failed;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t at(uint32_t addr, uint32_t i)
{
    return (addr + i) % SIZE;
}

/* An address at an edge, or anywhere. */
static uint32_t pick_addr(void)
{
    uint32_t page   = (next() % (SIZE / PAGE)) * PAGE;
    uint32_t sector = (next() % (SIZE / SECTOR)) * SECTOR;

    switch (next() % 14u) {
    case 0:  return 0u;
    case 1:  return 1u;
    case 2:  return page;
    case 3:  return page + PAGE - 1u;
    case 4:  return page + PAGE - (next() % TFW_STORE_CHUNK);
    case 5:  return sector;
    case 6:  return sector + SECTOR - 1u;
    case 7:  return SIZE - 1u;
    case 8:  return SIZE;
    case 9:  return SIZE + 1u;
    case 10: return 0x00FFFFFFu;
    case 11: return 0x01000000u;
    case 12: return 0xFFFFFFFFu - (next() % TFW_STORE_CHUNK);
    default: return next();
    }
}

/* A 24-bit address reaches 2^24 bytes; the driver refuses a span past them
 * rather than wrap it (#234). */
static int past(uint32_t addr, uint8_t len)
{
    return addr >= 0x01000000ul || (uint32_t)len > 0x01000000ul - addr;
}

static uint8_t pick_len(void)
{
    switch (next() % 7u) {
    case 0:  return 0u;
    case 1:  return 1u;
    case 2:  return (uint8_t)(TFW_STORE_CHUNK - 1u);
    case 3:  return (uint8_t)TFW_STORE_CHUNK;
    case 4:  return (uint8_t)(TFW_STORE_CHUNK + 1u);
    case 5:  return (uint8_t)(TFW_STORE_CHUNK + 1u + next() % (255u - TFW_STORE_CHUNK));
    default: return (uint8_t)(next() % (TFW_STORE_CHUNK + 1u));
    }
}

/* The part finishes what it started in its own time: poll it ready, as the
 * next caller's retry would find it. */
static void settle(void)
{
    uint8_t b[2];
    uint8_t n;

    tfw_nor_shim_reset();
    tfw_nor_model_fail_xfer(0u);
    tfw_nor_model_fail_erases(0u, TFW_OK);
    tfw_nor_model_set_bus(TFW_NOR_MODEL_PRESENT);
    tfw_nor_model_set_busy_polls(1u);
    tfw_nor_model_set_xfer_ms(0u);
    for (n = 0u; n < 255u; n++) {
        b[0] = OP_STATUS;
        b[1] = 0u;
        tfw_nor_model_xfer(b, 2u);
        if ((b[1] & ST_WIP) == 0u) {
            return;
        }
    }
    TEST_FAIL_MESSAGE("the part never came ready");
}

/* One fault, or none, for the next call: a failed transfer, a status byte
 * that reads busy for a while, a slow bus, or no part on the select. */
static uint8_t arm_fault(void)
{
    switch (next() % 10u) {
    case 0:
        tfw_nor_shim_fail(1u + next() % 6u, TFW_ERR_NACK);
        return 1u;
    case 1:
        tfw_nor_model_fail_xfer((uint8_t)(1u + next() % 6u));
        return 1u;
    case 2:
        tfw_nor_model_fail_erases(1u, TFW_ERR_TIMEOUT);
        return 1u;
    case 3:
        tfw_nor_model_set_busy_polls((uint8_t)(next() % 64u));
        tfw_nor_model_set_xfer_ms((uint16_t)(next() % 40u));
        return 1u;
    case 4:
        tfw_nor_model_set_bus((next() & 1u) ? TFW_NOR_MODEL_ABSENT_ECHO : TFW_NOR_MODEL_ABSENT_HIGH);
        return 1u;
    default:
        return 0u;
    }
}

static void check_all(void)
{
    TEST_ASSERT_EQUAL_MEMORY(shadow, tfw_nor_model_mem(), SIZE);
    TEST_ASSERT_EQUAL_UINT32(want_violations, tfw_nor_model_violations());
}

static void do_read(uint32_t addr, uint8_t len, uint8_t faulted)
{
    uint8_t      buf[256 + 8];
    uint32_t     i;
    tfw_status_t st;
    tfw_nor_model_bus_t bus = tfw_nor_model_bus();

    memset(buf, 0xA5, sizeof buf);
    st = tfw_store_read(addr, buf, len);
    failed += (st != TFW_OK);
    if (len > TFW_STORE_CHUNK || past(addr, len)) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
        TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, buf, sizeof buf);
        return;
    }
    for (i = len; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5u, buf[i]);      /* nothing past the request */
    }
    if (st != TFW_OK) {
        TEST_ASSERT_TRUE(faulted);
        TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, buf, sizeof buf);
        return;
    }
    for (i = 0u; i < len; i++) {
        uint8_t want = shadow[at(addr, i)];

        if (bus == TFW_NOR_MODEL_ABSENT_HIGH) {
            want = 0xFFu;
        } else if (bus == TFW_NOR_MODEL_ABSENT_ECHO) {
            want = 0x00u;                           /* the zeros sent */
        }
        TEST_ASSERT_EQUAL_HEX8(want, buf[i]);
    }
}

/* Program the first `chunks` page-bounded pieces of the request into the
 * plain model, as the driver splits it. */
static void program_shadow(uint32_t addr, const uint8_t *src, uint8_t len, uint32_t chunks)
{
    uint32_t i = 0u;

    while (i < len && chunks > 0u) {
        uint32_t room = PAGE - ((addr + i) & (PAGE - 1u));
        uint32_t end  = (i + room < len) ? i + room : len;

        for (; i < end; i++) {
            uint8_t *s = &shadow[at(addr, i)];

            if ((src[i] & (uint8_t)~*s) != 0u) {
                want_violations++;                  /* the part counts each 0 -> 1 */
            }
            *s &= src[i];
        }
        chunks--;
    }
}

static void do_write(uint32_t addr, uint8_t len, uint8_t faulted)
{
    uint8_t      src[256];
    uint32_t     i, before = tfw_nor_model_programs();
    tfw_status_t st;
    tfw_nor_model_bus_t bus = tfw_nor_model_bus();

    for (i = 0u; i < sizeof src; i++) {
        src[i] = (uint8_t)next();
    }
    st = tfw_store_write(addr, src, len);
    failed += (st != TFW_OK);
    if (len > TFW_STORE_CHUNK || past(addr, len)) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
        TEST_ASSERT_EQUAL_UINT32(before, tfw_nor_model_programs());
        check_all();                                /* a refused call changes nothing */
        return;
    }
    if (bus != TFW_NOR_MODEL_PRESENT) {
        /* No part: floating high never reads ready; an echo reads ready but
         * WEL clear after WREN (#234). */
        TEST_ASSERT_EQUAL((len == 0u) ? TFW_OK
                          : (bus == TFW_NOR_MODEL_ABSENT_HIGH) ? TFW_ERR_TIMEOUT : TFW_ERR_NO_STORE, st);
        check_all();
        return;
    }
    if (st != TFW_OK) {
        TEST_ASSERT_TRUE(faulted);
    }
    /* Each page piece the part took is programmed whole, the rest not at all,
     * and nothing outside the request moves. */
    program_shadow(addr, src, len, tfw_nor_model_programs() - before);
    if (st == TFW_OK) {
        TEST_ASSERT_EQUAL_UINT32((len == 0u) ? 0u : 1u + ((((addr & (PAGE - 1u)) + len - 1u) / PAGE)),
                                 tfw_nor_model_programs() - before);
    }
    check_all();
}

static void do_erase(uint32_t addr, uint8_t faulted)
{
    uint32_t     base = (addr % SIZE) & ~(SECTOR - 1u);
    uint32_t     before = tfw_nor_model_erases();
    tfw_status_t st;
    tfw_nor_model_bus_t bus = tfw_nor_model_bus();

    st = tfw_store_erase(addr);
    failed += (st != TFW_OK);
    if (past(addr, 1u)) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
        TEST_ASSERT_EQUAL_UINT32(before, tfw_nor_model_erases());
        check_all();
        return;
    }
    if (bus != TFW_NOR_MODEL_PRESENT) {
        TEST_ASSERT_EQUAL((bus == TFW_NOR_MODEL_ABSENT_HIGH) ? TFW_ERR_TIMEOUT : TFW_ERR_NO_STORE, st);
        check_all();
        return;
    }
    if (st != TFW_OK) {
        TEST_ASSERT_TRUE(faulted);
    } else {
        TEST_ASSERT_EQUAL_UINT32(before + 1u, tfw_nor_model_erases());
    }
    /* The sector is erased whole or not at all, and nothing else. */
    if (tfw_nor_model_erases() != before) {
        TEST_ASSERT_EQUAL_UINT32(before + 1u, tfw_nor_model_erases());
        memset(&shadow[base], 0xFF, SECTOR);
    }
    check_all();
}

void setUp(void)
{
    tfw_mock_reset();
    tfw_nor_model_reset();
    tfw_nor_shim_reset();
    tfw_nor_model_fail_erases(0u, TFW_OK);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    memset(shadow, 0xFF, sizeof shadow);
    want_violations = 0u;
    failed = 0u;
    rng = SEED;
}
void tearDown(void) {}

static void test_random_reads_programs_and_erases_match_the_plain_model(void)
{
    uint32_t n, reads = 0u, writes = 0u, erases = 0u, faults = 0u;

    for (n = 0u; n < ITERATIONS; n++) {
        uint32_t addr    = pick_addr();
        uint8_t  len     = pick_len();
        uint8_t  faulted = arm_fault();

        faults += faulted;
        switch (next() % 3u) {
        case 0:  do_read(addr, len, faulted);  reads++;  break;
        case 1:  do_write(addr, len, faulted); writes++; break;
        default: do_erase(addr, faulted);      erases++; break;
        }
        settle();
        check_all();
    }
    TEST_ASSERT_EQUAL_UINT32(ITERATIONS, reads + writes + erases);
    TEST_ASSERT_TRUE(reads > ITERATIONS / 4u);
    TEST_ASSERT_TRUE(writes > ITERATIONS / 4u);
    TEST_ASSERT_TRUE(erases > ITERATIONS / 4u);
    TEST_ASSERT_TRUE(faults > ITERATIONS / 4u);
    TEST_ASSERT_TRUE(failed > ITERATIONS / 4u);     /* the edges and faults were refused */

    /* A clean sequence afterwards reads back what it wrote. */
    {
        uint8_t src[TFW_STORE_CHUNK], back[TFW_STORE_CHUNK];

        memset(src, 0x3Cu, sizeof src);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(SIZE - SECTOR));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(SIZE - PAGE / 2u, src, (uint8_t)sizeof src));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_read(SIZE - PAGE / 2u, back, (uint8_t)sizeof back));
        TEST_ASSERT_EQUAL_MEMORY(src, back, sizeof src);
    }
}

/* A mutated manufacturer byte is a part unless JEP106 assigns it to nobody. */
static void test_a_mutated_id_byte_is_a_part_unless_it_is_0x00_or_0xff(void)
{
    uint32_t n, parts = 0u, empties = 0u;

    for (n = 0u; n < PROBES; n++) {
        uint8_t      m = (uint8_t)next();
        tfw_status_t st;

        if ((n % 16u) == 0u) {
            m = (n & 16u) ? 0x00u : 0xFFu;
        }
        tfw_nor_model_set_maker(m);
        st = tfw_store_probe();
        if (m == 0x00u || m == 0xFFu) {
            TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, st);
            empties++;
        } else {
            TEST_ASSERT_EQUAL(TFW_OK, st);
            parts++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(PROBES, parts + empties);
    TEST_ASSERT_TRUE(empties >= PROBES / 16u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
    check_all();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_random_reads_programs_and_erases_match_the_plain_model);
    RUN_TEST(test_a_mutated_id_byte_is_a_part_unless_it_is_0x00_or_0xff);
    return UNITY_END();
}
