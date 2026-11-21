/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2 SoC's watchdog (hal_soc.c) over a register model, once under each
 * MPS2 profile (mk/boot.mk): boot/hal/cortex-m/rp2350/test/cm_regs.h comes
 * ahead of src/hal/cortex-m, so every TFW_CM_REG lands in tfw_cm_model_reg
 * below. The model logs each access in order, the value the register holds
 * after it, so a test reads both what was written and when: a write outside
 * the unlock is one the hardware ignores.
 *
 * TEST_WDOG_BASE is the profile's watchdog by literal (the AN505's Secure
 * one, the AN500's APB one), set by the make rule, not read from the
 * profile, so a moved base fails here; TEST_SPI_BASE, TEST_I2C_BASE and
 * TEST_FPGAIO_BASE likewise for the real pl022.c and sbcon.c accessors.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/config.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "pl022.h"
#include "sbcon.h"

#if !defined(TEST_WDOG_BASE) || !defined(TEST_SPI_BASE) || \
    !defined(TEST_I2C_BASE) || !defined(TEST_FPGAIO_BASE)
#error "TEST_*_BASE: the make rule states the profile's bases"
#endif

#define LOG_MAX   32u
#define MODEL_MAX 16u

#define WDOG_LOAD   (TEST_WDOG_BASE + 0x000u)
#define WDOG_CTRL   (TEST_WDOG_BASE + 0x008u)
#define WDOG_INTCLR (TEST_WDOG_BASE + 0x00Cu)
#define WDOG_LOCK   (TEST_WDOG_BASE + 0xC00u)

/* What hal_early.c defines on the part: the marker word and its decode. */
volatile uint32_t tfw_cm_reset_mark;

tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark)
{
    return tfw_cm_reset_decode(mark);
}

/* Each access in order: the register, and its value after the access. */
static uint32_t log_addr[LOG_MAX];
static uint32_t log_val[LOG_MAX];
static uint32_t log_mark[LOG_MAX];
static uint32_t log_used;

/* The registers' values between accesses. */
static uint32_t model_addr[MODEL_MAX];
static uint32_t model_val[MODEL_MAX];
static uint32_t model_used;

static uint32_t *model_slot(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_MAX) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used] = 0u;
    return &model_val[model_used++];
}

/* The last access is done once the next one starts: keep what it left. */
static void log_settle(void)
{
    if (log_used > 0u) {
        *model_slot(log_addr[log_used - 1u]) = log_val[log_used - 1u];
    }
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    log_settle();
    if (log_used == LOG_MAX) {
        TEST_FAIL_MESSAGE("access log full");
    }
    log_addr[log_used] = addr;
    log_val[log_used] = *model_slot(addr);
    log_mark[log_used] = tfw_cm_reset_mark;
    return &log_val[log_used++];
}

static uint32_t accesses(void)
{
    log_settle();
    return log_used;
}

static void expect_access(uint32_t i, uint32_t addr, uint32_t val)
{
    TEST_ASSERT_LESS_THAN_UINT32(accesses(), i);
    TEST_ASSERT_EQUAL_HEX32(addr, log_addr[i]);
    TEST_ASSERT_EQUAL_HEX32(val, log_val[i]);
}

void setUp(void)
{
    log_used = 0u;
    model_used = 0u;
    tfw_cm_reset_mark = 0u;
}

void tearDown(void) {}

static void test_early_disarms_inside_the_unlock(void)
{
    tfw_cm_soc_early();
    TEST_ASSERT_EQUAL_UINT32(4u, accesses());
    expect_access(0u, WDOG_LOCK, TFW_MPS2_WDOG_UNLOCK);
    expect_access(1u, WDOG_CTRL, 0u);
    expect_access(2u, WDOG_INTCLR, 1u);
    expect_access(3u, WDOG_LOCK, 0u);
}

/* The marker first, so a bite reads as the watchdog's; then the four-second
 * period, the interrupt cleared, the reset enabled, all inside the unlock.
 * The marker is checked as it stood at the first register access: a store
 * after CTRL would leave a window where a bite carries the wrong cause. */
static void test_arm_marks_then_loads_and_enables_inside_the_unlock(void)
{
    tfw_cm_soc_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
    TEST_ASSERT_EQUAL_UINT32(5u, accesses());
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, log_mark[0]);
    expect_access(0u, WDOG_LOCK, TFW_MPS2_WDOG_UNLOCK);
    expect_access(1u, WDOG_LOAD, (uint32_t)(TFW_CM_CPU_HZ * 4u));
    expect_access(2u, WDOG_INTCLR, 1u);
    expect_access(3u, WDOG_CTRL, TFW_MPS2_WDOG_CTRL_INTEN | TFW_MPS2_WDOG_CTRL_RESEN);
    expect_access(4u, WDOG_LOCK, 0u);
}

/* A pet reloads through INTCLR alone: a LOAD write would change the period.
 * The watchdog is marked as armed first, and the pet must keep that marker
 * so a later timeout still reads as the watchdog's. */
static void test_pet_clears_inside_the_unlock_and_nothing_else(void)
{
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    tfw_cm_soc_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT32(3u, accesses());
    expect_access(0u, WDOG_LOCK, TFW_MPS2_WDOG_UNLOCK);
    expect_access(1u, WDOG_INTCLR, 1u);
    expect_access(2u, WDOG_LOCK, 0u);
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
}

static void test_reset_cause_decodes_each_mark(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, tfw_cm_soc_reset_cause(TFW_CM_MARK_WATCHDOG));
    TEST_ASSERT_EQUAL_INT(TFW_RESET_SOFTWARE, tfw_cm_soc_reset_cause(TFW_CM_MARK_RESETREQ));
    TEST_ASSERT_EQUAL_INT(TFW_RESET_PANIC, tfw_cm_soc_reset_cause(TFW_CM_MARK_PANIC));
    TEST_ASSERT_EQUAL_INT(TFW_RESET_POWER_ON, tfw_cm_soc_reset_cause(0u));
    TEST_ASSERT_EQUAL_INT(TFW_RESET_POWER_ON, tfw_cm_soc_reset_cause(0xDEADBEEFu));
    TEST_ASSERT_EQUAL_UINT32(0u, accesses());
}

/* A register's value before the call, as the part would hold it. */
static void preset(uint32_t addr, uint32_t val)
{
    *model_slot(addr) = val;
}

/* The PL022's registers at their documented offsets (DDI 0194): a wrong
 * offset in pl022.h or a wrong base reaches another register here. */
static void test_ssp_offsets_are_the_pl022s(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00u, TFW_SSP_CR0);
    TEST_ASSERT_EQUAL_HEX32(0x04u, TFW_SSP_CR1);
    TEST_ASSERT_EQUAL_HEX32(0x08u, TFW_SSP_DR);
    TEST_ASSERT_EQUAL_HEX32(0x0Cu, TFW_SSP_SR);
    TEST_ASSERT_EQUAL_HEX32(0x10u, TFW_SSP_CPSR);
}

static void test_ssp_read_returns_the_register_at_its_offset(void)
{
    preset(TEST_SPI_BASE + 0x0Cu, TFW_SSP_SR_TFE | TFW_SSP_SR_TNF);
    preset(TEST_SPI_BASE + 0x08u, 0xA5u);
    TEST_ASSERT_EQUAL_HEX32(TFW_SSP_SR_TFE | TFW_SSP_SR_TNF, tfw_mps2_ssp_read(TFW_SSP_SR));
    TEST_ASSERT_EQUAL_HEX32(0xA5u, tfw_mps2_ssp_read(TFW_SSP_DR));
    TEST_ASSERT_EQUAL_UINT32(2u, accesses());
    expect_access(0u, TEST_SPI_BASE + 0x0Cu, TFW_SSP_SR_TFE | TFW_SSP_SR_TNF);
    expect_access(1u, TEST_SPI_BASE + 0x08u, 0xA5u);
}

static void test_ssp_write_stores_the_value_at_its_offset(void)
{
    tfw_mps2_ssp_write(TFW_SSP_CR1, TFW_SSP_CR1_SSE);
    tfw_mps2_ssp_write(TFW_SSP_CPSR, 0x02u);
    TEST_ASSERT_EQUAL_UINT32(2u, accesses());
    expect_access(0u, TEST_SPI_BASE + 0x04u, TFW_SSP_CR1_SSE);
    expect_access(1u, TEST_SPI_BASE + 0x10u, 0x02u);
}

/* The chip select rides the FPGA I/O block's MISC register, at 0x4C. */
static void test_misc_reads_and_writes_the_fpgaio_misc_register(void)
{
    preset(TEST_FPGAIO_BASE + 0x4Cu, 0x123u);
    TEST_ASSERT_EQUAL_HEX32(0x123u, tfw_mps2_misc_read());
    tfw_mps2_misc_write(0x023u);
    TEST_ASSERT_EQUAL_UINT32(2u, accesses());
    expect_access(0u, TEST_FPGAIO_BASE + 0x4Cu, 0x123u);
    expect_access(1u, TEST_FPGAIO_BASE + 0x4Cu, 0x023u);
}

/* SB_CONTROL reads the lines at 0x0; only SCL and SDA are the lines. */
static void test_sb_lines_reads_control_and_keeps_only_the_lines(void)
{
    preset(TEST_I2C_BASE + 0x0u, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_HEX32(0x3u, tfw_mps2_sb_lines());
    TEST_ASSERT_EQUAL_UINT32(1u, accesses());
    expect_access(0u, TEST_I2C_BASE + 0x0u, 0xFFFFFFFFu);
}

/* A low SCL reads low: the line is the register's, not a constant. */
static void test_sb_lines_reads_a_low_scl(void)
{
    preset(TEST_I2C_BASE + 0x0u, 0xFFFFFFFEu);
    TEST_ASSERT_EQUAL_HEX32(TFW_SB_SDA, tfw_mps2_sb_lines());
    TEST_ASSERT_EQUAL_UINT32(1u, accesses());
}

/* Release sets through SB_CONTROLS (0x0), drive-low clears through
 * SB_CONTROLC (0x4), each with the caller's mask as it came. */
static void test_sb_release_and_drive_low_write_set_and_clear(void)
{
    tfw_mps2_sb_release(TFW_SB_SCL);
    tfw_mps2_sb_drive_low(TFW_SB_SDA);
    tfw_mps2_sb_release(TFW_SB_SCL | TFW_SB_SDA);
    tfw_mps2_sb_drive_low(TFW_SB_SCL | TFW_SB_SDA);
    TEST_ASSERT_EQUAL_UINT32(4u, accesses());
    expect_access(0u, TEST_I2C_BASE + 0x0u, 0x1u);
    expect_access(1u, TEST_I2C_BASE + 0x4u, 0x2u);
    expect_access(2u, TEST_I2C_BASE + 0x0u, 0x3u);
    expect_access(3u, TEST_I2C_BASE + 0x4u, 0x3u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_disarms_inside_the_unlock);
    RUN_TEST(test_arm_marks_then_loads_and_enables_inside_the_unlock);
    RUN_TEST(test_pet_clears_inside_the_unlock_and_nothing_else);
    RUN_TEST(test_reset_cause_decodes_each_mark);
    RUN_TEST(test_ssp_offsets_are_the_pl022s);
    RUN_TEST(test_ssp_read_returns_the_register_at_its_offset);
    RUN_TEST(test_ssp_write_stores_the_value_at_its_offset);
    RUN_TEST(test_misc_reads_and_writes_the_fpgaio_misc_register);
    RUN_TEST(test_sb_lines_reads_control_and_keeps_only_the_lines);
    RUN_TEST(test_sb_lines_reads_a_low_scl);
    RUN_TEST(test_sb_release_and_drive_low_write_set_and_clear);
    return UNITY_END();
}
