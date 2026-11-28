/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MAC stand-in for the STM32H5 MDIO register test (test/unit/
 * test_h5_eth_mdio_regs.c). The register file is built with its
 * tfw_cm_model_reg renamed regfile_model_reg (mk/host.mk), so the one here
 * stands in for the MAC: it clears MACMDIOAR.MB once the driver has polled
 * it busy_polls times. Include it once, from that test, after
 * regfile_model.h; the test reads and sets the state below.
 */
#ifndef TFW_TEST_H5_MDIO_MODEL_H
#define TFW_TEST_H5_MDIO_MODEL_H

#include <stdint.h>

#define MACMDIOAR  0x40028200u
#define MACMDIODR  0x40028204u
#define MB         0x1u
#define GOC_READ   (0x3u << 2)
#define NEVER      UINT32_MAX

volatile uint32_t *regfile_model_reg(uintptr_t addr);

static uint32_t busy_polls;   /* MB reads that see it set, then it clears */
static uint32_t polls;        /* MB-set reads since the last command */
static uint32_t issued;       /* MACMDIOAR as the last command wrote it */
static uint32_t issued_dr;    /* MACMDIODR as the last command found it */
static uint32_t seen;         /* MB-set reads in all */
static uint32_t reply;        /* MACMDIODR a read's completion publishes */
static uint32_t captured;     /* the command in flight has been captured */
static uint32_t commands;     /* commands captured in all */
static uint32_t first_pmcr, first_ahb1enr, first_moder_a, first_moder_c;   /* at the first command */

/*
 * The register file, with MACMDIOAR's MB cleared after busy_polls reads.
 * A read's result reaches MACMDIODR only as MB clears, so a driver that
 * reads the data register before its wait gets the stale value. A command
 * and its data are captured at the first register access after MB is set,
 * whatever register that access names, as the MAC starts the frame at once:
 * a driver that loads MACMDIODR after issuing a write sends the stale value.
 * The first command captured also snapshots SBS_PMCR, RCC_AHB1ENR and the
 * GPIOA and GPIOC MODER registers, so a test sees the init state the MAC
 * had when the first MDIO frame went out, not only after the driver returns.
 */
volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    volatile uint32_t *ar = regfile_model_reg(MACMDIOAR);
    volatile uint32_t *r = regfile_model_reg(addr);

    if ((*ar & MB) != 0u && captured == 0u) {
        issued = *ar;
        issued_dr = *regfile_model_reg(MACMDIODR);
        if (commands++ == 0u) {
            first_pmcr = *regfile_model_reg(0x44000500u);   /* SBS_PMCR */
            first_ahb1enr = *regfile_model_reg(TFW_H5_RCC_BASE + 0x88u);
            first_moder_a = *regfile_model_reg(TFW_H5_GPIOA_BASE);
            first_moder_c = *regfile_model_reg(TFW_H5_GPIOA_BASE + 2u * TFW_H5_GPIO_STRIDE);
        }
        captured = 1u;
    }
    if (addr == MACMDIOAR && (*r & MB) != 0u) {
        if (busy_polls != NEVER && polls >= busy_polls) {
            *r &= ~MB;
            if ((issued & (0x3u << 2)) == GOC_READ) {
                *regfile_model_reg(MACMDIODR) = reply;
            }
            polls = 0u;
            captured = 0u;
        } else {
            polls++;
            seen++;
        }
    }
    return r;
}

/* A store and a load as the driver makes them, through the model, so a test
 * can play a driver's access order by hand. */
static inline void mdio_model_store(uintptr_t addr, uint32_t v)
{
    *tfw_cm_model_reg(addr) = v;
}

static inline uint32_t mdio_model_load(uintptr_t addr)
{
    return *tfw_cm_model_reg(addr);
}

#endif /* TFW_TEST_H5_MDIO_MODEL_H */
