/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/cortex-m/stm32h5/ee_flash_port.c on the host, on the
 * stm32h573i-dk's profile: the part's flash interface and flash it hands
 * ee_flash.c. test_stm32h5_eeprom.c links its register model in place of this
 * file, so this is the file's own test. The port is included unchanged behind
 * stm32h5_ee_port_stubs.h, which stands in for cm_regs.h.
 */
#include <stdint.h>

#include "unity.h"
#include "stm32h5_ee_port_stubs.h"
#include "stm32h5/soc_regs.h"

#include "../../src/hal/cortex-m/stm32h5/ee_flash_port.c"

uint32_t stub_primask;
unsigned stub_dsbs;
unsigned stub_saves;
unsigned stub_restores;
unsigned stub_dsbs_at_restore;

void setUp(void)
{
    stub_primask = 0u;
    stub_dsbs = 0u;
    stub_saves = 0u;
    stub_restores = 0u;
    stub_dsbs_at_restore = 0u;
}

void tearDown(void) {}

/* RM0481: the FLASH interface at 0x40022000, bank 1's first byte at
 * 0x08000000 (non-secure). */
static void test_the_port_names_the_flash_registers_and_the_flash(void)
{
    const struct tfw_h5_flash *f = tfw_h5_ee_flash();

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_HEX32(0x40022000u, TFW_H5_FLASH_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x08000000u, TFW_H5_FLASH_MEM);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)0x40022000u, (void *)(uintptr_t)f->regs);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)0x08000000u, f->mem);
    TEST_ASSERT_EQUAL_PTR(f, tfw_h5_ee_flash());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_port_names_the_flash_registers_and_the_flash);
    return UNITY_END();
}
