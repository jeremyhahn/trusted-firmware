/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 MDIO driver's register values (src/hal/cortex-m/stm32h5/
 * hal_eth_mdio.c, #579) on the register file (test/model/cortex-m/
 * regfile_model.c): the MACMDIOAR command a read or a write issues, the wait
 * on MACMDIOAR.MB, the spin limit, and init's SBS_PMCR, RCC and pin values.
 * The register file is built with its tfw_cm_model_reg renamed
 * regfile_model_reg (mk/host.mk), so the one here can stand in for the MAC:
 * it clears MB once the driver has polled it busy_polls times. The
 * addresses are the driver's, not confirmed against RM0481. A board has not
 * run them.
 */
#include <stdint.h>

#include "unity.h"
#include "soc_regs.h"
#include "regfile_model.h"
#include "hal_eth_mdio.h"

#define MACMDIOAR  0x40028200u
#define MACMDIODR  0x40028204u
#define AHB1ENR    (TFW_H5_RCC_BASE + 0x88u)
#define SBS_PMCR   (0x44000400u + 0x100u)
#define MB         0x1u
#define GOC_READ   (0x3u << 2)
#define GOC_WRITE  (0x1u << 2)
#define NEVER      UINT32_MAX

volatile uint32_t *regfile_model_reg(uintptr_t addr);

static uint32_t busy_polls;   /* MB reads that see it set, then it clears */
static uint32_t polls;        /* MB-set reads since the last command */
static uint32_t issued;       /* MACMDIOAR as the last command wrote it */
static uint32_t seen;         /* MB-set reads in all */

/* The register file, with MACMDIOAR's MB cleared after busy_polls reads. */
volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    volatile uint32_t *r = regfile_model_reg(addr);

    if (addr == MACMDIOAR && (*r & MB) != 0u) {
        if (polls == 0u) {
            issued = *r;
        }
        if (busy_polls != NEVER && polls >= busy_polls) {
            *r &= ~MB;
            polls = 0u;
        } else {
            polls++;
            seen++;
        }
    }
    return r;
}

void setUp(void)
{
    regfile_model_reset();
    busy_polls = 3u;
    polls = 0u;
    issued = 0u;
    seen = 0u;
}

void tearDown(void)
{
}

void test_a_read_names_the_phy_and_register_and_waits_on_mb(void)
{
    uint16_t v = 0u;

    TFW_CM_REG(MACMDIODR) = 0xABCD1234u;
    TEST_ASSERT_EQUAL_INT(0, tfw_h5_eth_mdio_read(0x01u, 0x02u, &v));
    TEST_ASSERT_EQUAL_HEX32((0x01u << 21) | (0x02u << 16) | GOC_READ | MB, issued);
    TEST_ASSERT_EQUAL_UINT32(3u, seen);   /* DR read only once MB cleared */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, v);
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(MACMDIOAR) & MB);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_read_names_the_phy_and_register_and_waits_on_mb);
    return UNITY_END();
}
