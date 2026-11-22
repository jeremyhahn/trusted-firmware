/*
 * tfw-boot's flash write on the FRDM-MCXN947, boot/hal/cortex-m/mcxn94x/
 * boot_flash.c, compiled unchanged over a recorder in place of
 * tfw_mcxn_flash_update (mcxn_flash.c, tested on its own in test_mcxn_flash)
 * and the model's interrupt mask (test/model/cortex-m/cm_regs.h). The ROM's
 * API tree is a page mapped at its address, its flash word pointing at a fake
 * driver in the same page: the suite checks the glue picks the tree's driver,
 * the flash span and the caller's bytes, holds the mask across the update and
 * gives it back, and returns the update's status.
 */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "tfw/config.h"
#include "boot_soc.h"
#include "mcxn_flash.h"

#define ROM_PAGE      0x1303F000u
#define ROM_PAGE_SIZE 0x1000u
#define SAVE_SENTINEL 0xA5u

static volatile uint32_t *tree;
static tfw_mcxn_flash_api_t *api_a;
static tfw_mcxn_flash_api_t *api_b;

static int masked;
static unsigned saves;
static unsigned restores;
static uint32_t restored;
static unsigned seq;
static unsigned update_seq;
static unsigned restore_seq;

static unsigned updates;
static const tfw_mcxn_flash_api_t *got_api;
static const uint8_t *got_flash;
static uint32_t got_end;
static uint32_t got_addr;
static const uint8_t *got_data;
static uint32_t got_len;
static int got_masked;
static tfw_status_t update_status;

tfw_status_t tfw_mcxn_flash_update(const tfw_mcxn_flash_api_t *api, const uint8_t *flash,
                                   uint32_t flash_end, uint32_t addr,
                                   const uint8_t *data, uint32_t len)
{
    updates++;
    update_seq = ++seq;
    got_api = api;
    got_flash = flash;
    got_end = flash_end;
    got_addr = addr;
    got_data = data;
    got_len = len;
    got_masked = masked;
    return update_status;
}

uint32_t tfw_imx95_model_irq_save(void)
{
    saves++;
    masked = 1;
    return SAVE_SENTINEL;
}

void tfw_imx95_model_irq_restore(uint32_t primask)
{
    restores++;
    restore_seq = ++seq;
    restored = primask;
    masked = 0;
}

void tfw_imx95_model_irq_mask(uint32_t m)
{
    (void)m;
    TEST_FAIL_MESSAGE("the boot flash write sets the mask outright");
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("the boot flash write touches a register");
    return NULL;
}

void setUp(void)
{
    tree[TFW_MCXN_ROM_TREE_FLASH] = (uint32_t)(uintptr_t)api_a;
    masked = 0;
    saves = restores = updates = 0;
    restored = 0;
    seq = update_seq = restore_seq = 0;
    got_api = NULL;
    got_flash = NULL;
    got_data = NULL;
    got_end = got_addr = got_len = 0;
    got_masked = 0;
    update_status = TFW_OK;
}

void tearDown(void) {}

static const uint8_t payload[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

/* (1) The tree's driver, the whole flash span and the caller's bytes. */
static void test_write_passes_tree_driver_span_and_caller_bytes(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(0x10012340u, payload, sizeof payload));
    TEST_ASSERT_EQUAL_UINT(1u, updates);
    TEST_ASSERT_EQUAL_PTR(api_a, got_api);
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)(uintptr_t)TFW_MCXN_FLASH_BASE, got_flash);
    TEST_ASSERT_EQUAL_HEX32(0x10200000u, got_end);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_FLASH_TOTAL, got_end);
    TEST_ASSERT_EQUAL_HEX32(0x10012340u, got_addr);
    TEST_ASSERT_EQUAL_PTR(payload, got_data);
    TEST_ASSERT_EQUAL_UINT32(sizeof payload, got_len);
}

/* (2) The update runs masked; restore gets save's value once, after it. */
static void test_update_runs_masked_and_mask_given_back_after(void)
{
    (void)tfw_cm_boot_flash_write(0x10000000u, payload, 4u);
    TEST_ASSERT_EQUAL_UINT(1u, saves);
    TEST_ASSERT_TRUE(got_masked);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, restores, "the mask is given back once");
    TEST_ASSERT_EQUAL_HEX32(SAVE_SENTINEL, restored);
    TEST_ASSERT_GREATER_THAN_UINT(update_seq, restore_seq);
    TEST_ASSERT_FALSE(masked);
}

/* (3) The update's status comes back unchanged, the mask given back on each. */
static void test_status_returned_unchanged_mask_given_back(void)
{
    static const tfw_status_t sts[] = { TFW_OK, TFW_ERR_RANGE, TFW_ERR_INTERNAL };

    for (unsigned i = 0; i < sizeof sts / sizeof sts[0]; i++) {
        setUp();
        update_status = sts[i];
        TEST_ASSERT_EQUAL_INT(sts[i], tfw_cm_boot_flash_write(0x10100000u, payload, 8u));
        TEST_ASSERT_EQUAL_UINT(1u, restores);
        TEST_ASSERT_EQUAL_HEX32(SAVE_SENTINEL, restored);
        TEST_ASSERT_FALSE(masked);
    }
}

/* (4) The driver is read from the tree on each call, not cached. */
static void test_driver_read_per_call(void)
{
    (void)tfw_cm_boot_flash_write(0x10000000u, payload, 4u);
    TEST_ASSERT_EQUAL_PTR(api_a, got_api);
    tree[TFW_MCXN_ROM_TREE_FLASH] = (uint32_t)(uintptr_t)api_b;
    (void)tfw_cm_boot_flash_write(0x10000000u, payload, 4u);
    TEST_ASSERT_EQUAL_UINT(2u, updates);
    TEST_ASSERT_EQUAL_PTR(api_b, got_api);
}

int main(void)
{
    uint8_t *page = mmap((void *)(uintptr_t)ROM_PAGE, ROM_PAGE_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (page != (uint8_t *)(uintptr_t)ROM_PAGE) {
        return 1;   /* the ROM's window is not free in this process */
    }
    tree = (volatile uint32_t *)(uintptr_t)TFW_MCXN_ROM_API_TREE;
    /* Two drivers below the tree, in the same page, so their addresses fit
     * the tree's 32-bit word. */
    api_a = (tfw_mcxn_flash_api_t *)(void *)page;
    api_b = (tfw_mcxn_flash_api_t *)(void *)(page + 0x400u);
    memset(page, 0, 0x800u);

    UNITY_BEGIN();
    RUN_TEST(test_write_passes_tree_driver_span_and_caller_bytes);
    RUN_TEST(test_update_runs_masked_and_mask_given_back_after);
    RUN_TEST(test_status_returned_unchanged_mask_given_back);
    RUN_TEST(test_driver_read_per_call);
    return UNITY_END();
}
