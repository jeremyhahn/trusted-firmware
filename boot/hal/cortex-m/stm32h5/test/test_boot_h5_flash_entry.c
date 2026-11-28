/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's STM32H5 flash entry points (boot/hal/cortex-m/stm32h5/boot_flash.c):
 * the address check and translation in front of h5_flash.c's driver, and the
 * wait the driver leaves to the part. test_boot_stm32h5_flash covers the
 * driver with a wait of its own; here the driver is the suite's, recording
 * what it is handed.
 *
 * The suite includes boot_flash.c itself, after setting NSSR's offset to a
 * call into the suite's model: each poll the wait makes reads the next status
 * word of a script, so the wait is counted in polls, with no clock and no
 * thread.
 */
#include <stdint.h>
#include <stddef.h>

#include "unity.h"
#include "stm32h5_ee_port_stubs.h"
#include "stm32h5/soc_regs.h"
#include "../h5_flash.h"

#define NSSR_WAIT (TFW_H5_FLASH_SR_BSY | TFW_H5_FLASH_SR_WBNE | TFW_H5_FLASH_SR_DBNE)

/* The model's interface: the word the wait reads is the last; the script
 * puts each poll's status there. Past the script's end NSSR reads 0, so a
 * broken wait fails on the count rather than hanging. */
#define MODEL_WORDS 16u
#define MODEL_NSSR  (MODEL_WORDS - 1u)
static uint32_t        model_regs[MODEL_WORDS];
static const uint32_t *model_script;
static uint32_t        model_script_len;
static uint32_t        model_polls;

static uint32_t model_nssr_poll(void)
{
    model_regs[MODEL_NSSR] = model_polls < model_script_len ? model_script[model_polls] : 0u;
    model_polls++;
    return MODEL_NSSR;
}

#undef TFW_H5_FLASH_NSSR_OFF
#define TFW_H5_FLASH_NSSR_OFF (model_nssr_poll() * 4u)
#include "../boot_flash.c"

/* The driver's stand-in: it records its call and returns what the case set. */
static uint32_t                   update_calls;
static const struct tfw_h5_flash *update_f;
static uint32_t                   update_off;
static const uint8_t             *update_data;
static uint32_t                   update_len;
static tfw_status_t               update_ret;
static unsigned                   update_dsbs; /* DSBs issued before the update */

/* stm32h5_ee_port_stubs.h's model: PRIMASK, and the DSB and irq hook counts. */
uint32_t stub_primask;
unsigned stub_dsbs;
unsigned stub_saves;
unsigned stub_restores;
unsigned stub_dsbs_at_restore;

tfw_status_t tfw_h5_flash_update(const struct tfw_h5_flash *f, uint32_t off,
                                 const uint8_t *data, uint32_t len)
{
    update_calls++;
    update_dsbs = stub_dsbs;
    update_f    = f;
    update_off  = off;
    update_data = data;
    update_len  = len;
    return update_ret;
}

static void run_wait(const uint32_t *script, uint32_t len)
{
    static const struct tfw_h5_flash f = { model_regs, NULL };

    model_script     = script;
    model_script_len = len;
    tfw_h5_flash_wait(&f);
}

void setUp(void)
{
    update_calls = 0u;
    update_f     = NULL;
    update_off   = 0u;
    update_data  = NULL;
    update_len   = 0u;
    update_ret   = TFW_OK;
    model_polls  = 0u;
    update_dsbs  = 0u;
    stub_primask = 0u;
    stub_dsbs    = 0u;
    stub_saves   = 0u;
    stub_restores        = 0u;
    stub_dsbs_at_restore = 0u;
}

void tearDown(void)
{
}

/* An address below the flash is refused before the driver is reached. */
static void test_an_address_below_the_flash_is_out_of_range(void)
{
    static const uint8_t data[16] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_cm_boot_flash_write(TFW_H5_FLASH_MEM - 1u, data, sizeof data));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_cm_boot_flash_write(0u, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT32(0u, update_calls);
}

/* The driver gets the address from the flash's first byte, the part's flash
 * interface and flash, the caller's data and length, and its status back. */
static void test_the_update_gets_the_address_relative_to_the_flash(void)
{
    static const uint8_t data[32] = { 0xA5u };

    update_ret = TFW_ERR_INTERNAL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
                          tfw_cm_boot_flash_write(TFW_H5_FLASH_MEM + 0x4000u, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT32(1u, update_calls);
    TEST_ASSERT_EQUAL_HEX32(0x4000u, update_off);
    TEST_ASSERT_EQUAL_PTR(data, update_data);
    TEST_ASSERT_EQUAL_UINT32(sizeof data, update_len);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)TFW_H5_FLASH_BASE, (void *)(uintptr_t)update_f->regs);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)TFW_H5_FLASH_MEM, update_f->mem);
}

/* The flash's first byte is offset 0. */
static void test_the_flash_start_is_offset_0(void)
{
    static const uint8_t data[16] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(TFW_H5_FLASH_MEM, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT32(1u, update_calls);
    TEST_ASSERT_EQUAL_HEX32(0u, update_off);
}

/* The wait spins while any of BSY, WBNE and DBNE is set, each alone or all
 * together, and returns on the first poll that reads them all clear. */
static void test_the_wait_spins_while_bsy_wbne_or_dbne_is_set(void)
{
    static const uint32_t script[] = {
        TFW_H5_FLASH_SR_BSY, TFW_H5_FLASH_SR_WBNE, TFW_H5_FLASH_SR_DBNE, NSSR_WAIT,
        0u, TFW_H5_FLASH_SR_BSY,
    };

    run_wait(script, 6u);
    TEST_ASSERT_EQUAL_UINT32(5u, model_polls);
}

/* NSSR's other bits (errors, EOP) do not hold the wait. */
static void test_the_wait_returns_at_once_on_other_bits(void)
{
    static const uint32_t script[] = { ~NSSR_WAIT, TFW_H5_FLASH_SR_BSY };

    run_wait(script, 2u);
    TEST_ASSERT_EQUAL_UINT32(1u, model_polls);
}

/* The write fences once after the driver's update returns, so the programmed
 * words are out before the caller goes on; a refused address issues none. */
static void test_the_write_issues_one_dsb_after_the_update(void)
{
    static const uint8_t data[16] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(TFW_H5_FLASH_MEM + 0x4000u, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(0u, update_dsbs);
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs);

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_cm_boot_flash_write(TFW_H5_FLASH_MEM - 1u, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs);
    TEST_ASSERT_EQUAL_UINT32(1u, update_calls);
}

/* The driver's irq hooks mask PRIMASK and hand back the mask they found, and
 * the restore puts back exactly the mask it is given, so a nested save keeps
 * interrupts masked until the outer restore. */
static void test_the_irq_hooks_mask_and_put_back_the_found_mask(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_flash_irq_save());
    TEST_ASSERT_EQUAL_UINT32(1u, stub_primask);
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_h5_flash_irq_save());
    TEST_ASSERT_EQUAL_UINT32(1u, stub_primask);

    tfw_h5_flash_irq_restore(1u);
    TEST_ASSERT_EQUAL_UINT32(1u, stub_primask);
    tfw_h5_flash_irq_restore(0u);
    TEST_ASSERT_EQUAL_UINT32(0u, stub_primask);

    TEST_ASSERT_EQUAL_UINT(2u, stub_saves);
    TEST_ASSERT_EQUAL_UINT(2u, stub_restores);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_address_below_the_flash_is_out_of_range);
    RUN_TEST(test_the_update_gets_the_address_relative_to_the_flash);
    RUN_TEST(test_the_flash_start_is_offset_0);
    RUN_TEST(test_the_wait_spins_while_bsy_wbne_or_dbne_is_set);
    RUN_TEST(test_the_wait_returns_at_once_on_other_bits);
    RUN_TEST(test_the_write_issues_one_dsb_after_the_update);
    RUN_TEST(test_the_irq_hooks_mask_and_put_back_the_found_mask);
    return UNITY_END();
}
