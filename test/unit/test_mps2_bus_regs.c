/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 bus register drivers the image links (src/hal/cortex-m/mps2/sbcon.c
 * and pl022.c, mk/arch/cortex-m.mk ARCH_BUS_SRCS), unchanged, on the register
 * file (test/model/cortex-m/regfile_model.c): each access lands at the offset
 * from the base mps2-an505's profile states. test_mps2_bus and
 * test_mps2_spi_row model these drivers in place, so only this suite checks
 * their offsets on the host.
 */
#include <stdint.h>

#include "unity.h"
#include "soc_regs.h"
#include "sbcon.h"
#include "pl022.h"
#include "regfile_model.h"

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void)
{
}

static void test_sb_drive_low_writes_controlc_at_base_plus_4(void)
{
    tfw_mps2_sb_drive_low(TFW_SB_SDA);
    TEST_ASSERT_EQUAL_HEX32(TFW_SB_SDA, regfile_model_peek(TFW_MPS2_I2C_BASE + 0x4u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TFW_MPS2_I2C_BASE + 0x0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sb_drive_low_writes_controlc_at_base_plus_4);
    return UNITY_END();
}
