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

static void test_sb_release_writes_controls_at_base(void)
{
    tfw_mps2_sb_release(TFW_SB_SCL);
    TEST_ASSERT_EQUAL_HEX32(TFW_SB_SCL, regfile_model_peek(TFW_MPS2_I2C_BASE + 0x0u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TFW_MPS2_I2C_BASE + 0x4u));
}

static void test_sb_lines_reads_control_masked_to_scl_sda(void)
{
    regfile_model_poke(TFW_MPS2_I2C_BASE + 0x0u, 0xFFFFFFFCu | TFW_SB_SDA);
    TEST_ASSERT_EQUAL_HEX32(TFW_SB_SDA, tfw_mps2_sb_lines());
    regfile_model_poke(TFW_MPS2_I2C_BASE + 0x0u, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_HEX32(TFW_SB_SCL | TFW_SB_SDA, tfw_mps2_sb_lines());
    regfile_model_poke(TFW_MPS2_I2C_BASE + 0x4u, 0xFFFFFFFFu);
    regfile_model_poke(TFW_MPS2_I2C_BASE + 0x0u, 0u);
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mps2_sb_lines());
}

static void test_ssp_write_lands_at_spi_base_plus_off(void)
{
    tfw_mps2_ssp_write(0x08u, 0xA5u);
    tfw_mps2_ssp_write(0x10u, 0x5Au);
    TEST_ASSERT_EQUAL_HEX32(0xA5u, regfile_model_peek(TFW_MPS2_SPI_BASE + 0x08u));
    TEST_ASSERT_EQUAL_HEX32(0x5Au, regfile_model_peek(TFW_MPS2_SPI_BASE + 0x10u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TFW_MPS2_SPI_BASE + 0x0Cu));
}

static void test_ssp_read_comes_from_spi_base_plus_off(void)
{
    regfile_model_poke(TFW_MPS2_SPI_BASE + 0x0Cu, 0x12u);
    regfile_model_poke(TFW_MPS2_SPI_BASE + 0x08u, 0x34u);
    TEST_ASSERT_EQUAL_HEX32(0x12u, tfw_mps2_ssp_read(0x0Cu));
    TEST_ASSERT_EQUAL_HEX32(0x34u, tfw_mps2_ssp_read(0x08u));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mps2_ssp_read(0x00u));
}

static void test_misc_write_lands_at_fpgaio_base_plus_4c(void)
{
    tfw_mps2_misc_write(0x2u);
    TEST_ASSERT_EQUAL_HEX32(0x2u, regfile_model_peek(TFW_MPS2_FPGAIO_BASE + 0x4Cu));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TFW_MPS2_FPGAIO_BASE + 0x0u));
}

static void test_misc_read_comes_from_fpgaio_base_plus_4c(void)
{
    regfile_model_poke(TFW_MPS2_FPGAIO_BASE + 0x4Cu, 0x41u);
    regfile_model_poke(TFW_MPS2_FPGAIO_BASE + 0x48u, 0x99u);
    TEST_ASSERT_EQUAL_HEX32(0x41u, tfw_mps2_misc_read());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sb_drive_low_writes_controlc_at_base_plus_4);
    RUN_TEST(test_sb_release_writes_controls_at_base);
    RUN_TEST(test_sb_lines_reads_control_masked_to_scl_sda);
    RUN_TEST(test_ssp_write_lands_at_spi_base_plus_off);
    RUN_TEST(test_ssp_read_comes_from_spi_base_plus_off);
    RUN_TEST(test_misc_write_lands_at_fpgaio_base_plus_4c);
    RUN_TEST(test_misc_read_comes_from_fpgaio_base_plus_4c);
    return UNITY_END();
}
