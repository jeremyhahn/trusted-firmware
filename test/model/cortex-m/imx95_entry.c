/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 SoC code's entry points behind the model's names
 * (imx95_model.h), so the tests outside this directory name nothing of the
 * core. This directory is the register header's substitute on the host, and
 * the arch-leak gate exempts it for that reason (mk/host.mk, lint-arch-leak).
 */
#include <stdint.h>

#include "hal_early.h"
#include "imx95_model.h"

void tfw_cm_irq64(void);

/* hal_early.c's, which the host link leaves out: it is the reset handler. */
volatile uint32_t tfw_cm_reset_mark;

static uint32_t decoded_mark;

/* hal_early.c's too: a stand-in that records the mark, so the test sees what
 * the SoC passed without a second copy of the decode. */
tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark)
{
    decoded_mark = mark;
    return TFW_RESET_SOFTWARE;
}

void tfw_imx95_model_uart_interrupt(void)
{
    tfw_cm_irq64();
}

void tfw_imx95_model_soc_early(void)
{
    tfw_cm_soc_early();
}

void tfw_imx95_model_watchdog_arm(void)
{
    tfw_cm_soc_watchdog_arm();
}

void tfw_imx95_model_watchdog_pet(void)
{
    tfw_cm_soc_watchdog_pet();
}

uint32_t tfw_imx95_model_reset_mark(void)
{
    return tfw_cm_reset_mark;
}

void tfw_imx95_model_reset_mark_clear(void)
{
    tfw_cm_reset_mark = 0u;
}

int tfw_imx95_model_reset_cause(uint32_t mark)
{
    return (int)tfw_cm_soc_reset_cause(mark);
}

uint32_t tfw_imx95_model_decoded_mark(void)
{
    return decoded_mark;
}
