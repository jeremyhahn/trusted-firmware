/*
 * A mutation test for the soft DICE layer 0, cdi_soft.c (#163).
 *
 * tfw_boot_cdi_derive takes two inputs: the UDS in its flash slot and the
 * measurement. Every one-bit change of either, all 512 of them, changes the
 * CDI, and to a CDI no other change gave; the same inputs give the same CDI;
 * no CDI equals either input. The link is test_boot_cdi_soft_uds's object,
 * its slot patched as the jig patches it, with uds_slot made global by
 * objcopy so the test can flip the slot's bits in place (mprotect on its
 * page): the source under test is the one that ships. No randomness: every
 * bit is flipped, and a failure names the input and the bit.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_hal_mock.h"
#include "tfw/types.h"

#define LEN   TFW_BOOT_MEASURE_LEN
#define FLIPS (2u * LEN * 8u)

extern const uint8_t uds_slot[LEN];

static uint8_t meas[LEN];
static uint8_t base[LEN];
static uint8_t seen[FLIPS][LEN];
static char    why[64];

static uint8_t *slot(void)
{
    return (uint8_t *)(uintptr_t)uds_slot;
}

static void slot_prot(int prot)
{
    long      page = sysconf(_SC_PAGESIZE);
    uintptr_t lo   = (uintptr_t)uds_slot & ~(uintptr_t)(page - 1);
    uintptr_t hi   = ((uintptr_t)uds_slot + LEN + (uintptr_t)page - 1u) & ~(uintptr_t)(page - 1);

    TEST_ASSERT_EQUAL_INT(0, mprotect((void *)lo, (size_t)(hi - lo), prot));
}

void setUp(void)
{
    uint8_t i;

    for (i = 0u; i < LEN; i++) {
        meas[i] = (uint8_t)(0xA0u + i);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(meas, base));
    slot_prot(PROT_READ | PROT_WRITE);
}

void tearDown(void)
{
    slot_prot(PROT_READ);
}

static void derive(uint8_t cdi[LEN])
{
    memset(cdi, 0, LEN);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_boot_cdi_derive(meas, cdi), why);
}

static void neither_input(const uint8_t cdi[LEN])
{
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cdi, meas, LEN) == 0, why);
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cdi, uds_slot, LEN) == 0, why);
}

static void test_the_same_inputs_give_the_same_cdi(void)
{
    uint8_t again[LEN];
    uint8_t i;

    snprintf(why, sizeof why, "repeat");
    for (i = 0u; i < 4u; i++) {
        derive(again);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(base, again, LEN, why);
    }
    neither_input(base);
}

/* Every bit of the UDS, then every bit of the measurement: 512 CDIs, each
 * unlike the base, unlike either input and unlike every other one. */
static void test_every_one_bit_change_gives_its_own_cdi(void)
{
    uint16_t f, g;

    for (f = 0u; f < FLIPS; f++) {
        uint8_t *in  = f < LEN * 8u ? slot() : meas;
        uint16_t bit = (uint16_t)(f % (LEN * 8u));

        snprintf(why, sizeof why, "%s bit %u", f < LEN * 8u ? "uds" : "measurement",
                 (unsigned)bit);
        in[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        derive(seen[f]);
        TEST_ASSERT_FALSE_MESSAGE(memcmp(seen[f], base, LEN) == 0, why);
        neither_input(seen[f]);
        in[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
    for (f = 0u; f < FLIPS; f++) {
        for (g = (uint16_t)(f + 1u); g < FLIPS; g++) {
            snprintf(why, sizeof why, "flip %u and flip %u collide", (unsigned)f, (unsigned)g);
            TEST_ASSERT_FALSE_MESSAGE(memcmp(seen[f], seen[g], LEN) == 0, why);
        }
    }
    /* and with every bit back, the base again */
    snprintf(why, sizeof why, "after the flips");
    derive(seen[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(base, seen[0], LEN, why);
}

/* The slot erased but one bit is provisioned, and derives; fully erased is not. */
static void test_one_bit_from_erased_is_provisioned(void)
{
    uint8_t saved[LEN], cdi[LEN];

    memcpy(saved, slot(), LEN);
    memset(slot(), 0xFF, LEN);
    memset(cdi, 0x5A, LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_boot_cdi_derive(meas, cdi));
    slot()[LEN - 1u] = 0xFEu;
    snprintf(why, sizeof why, "erased but bit 0 of byte %u", (unsigned)(LEN - 1u));
    derive(cdi);
    neither_input(cdi);
    TEST_ASSERT_FALSE_MESSAGE(memcmp(cdi, base, LEN) == 0, why);
    memcpy(slot(), saved, LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_same_inputs_give_the_same_cdi);
    RUN_TEST(test_every_one_bit_change_gives_its_own_cdi);
    RUN_TEST(test_one_bit_from_erased_is_provisioned);
    return UNITY_END();
}
