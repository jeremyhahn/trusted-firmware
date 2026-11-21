/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What boot_hal_cortex-m.c calls, for its host test (#628): the SoC's flash
 * write (boot_soc.h), the factory copy and the early latch's reset cause. The
 * write and the order of the two reset-cause calls are logged here for
 * test_boot_hal_cortex-m.c; the reset and the watchdog are only linked.
 *
 * This file names the Cortex-M layer (tfw_cm_*), which lint-arch-leak allows
 * only under the Cortex-M HAL directories; no image links it, because the
 * SoC's *.c wildcard does not descend into boot/hal/cortex-m/test/.
 */
#include <stdint.h>
#include <stdlib.h>

#include "tfw/status.h"
#include "tfw/types.h"
#include "boot_soc.h"
#include "hal_early.h"

#define STUB_WRITES 8u

uint32_t       cm_stub_write_addr[STUB_WRITES];
uint32_t       cm_stub_write_len[STUB_WRITES];
const uint8_t *cm_stub_write_data[STUB_WRITES];
unsigned       cm_stub_nwrite;
tfw_status_t   cm_stub_write_status;

char              cm_stub_order[8];
unsigned          cm_stub_norder;
tfw_reset_cause_t cm_stub_cause;

static void order(char c)
{
    if (cm_stub_norder < sizeof cm_stub_order - 1u) {
        cm_stub_order[cm_stub_norder++] = c;
    }
}

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (cm_stub_nwrite < STUB_WRITES) {
        cm_stub_write_addr[cm_stub_nwrite] = addr;
        cm_stub_write_len[cm_stub_nwrite]  = len;
        cm_stub_write_data[cm_stub_nwrite] = data;
    }
    cm_stub_nwrite++;
    return cm_stub_write_status;
}

void tfw_cm_boot_factory(void)
{
    order('F');
}

tfw_reset_cause_t tfw_hal_early_reset_cause(void)
{
    order('C');
    return cm_stub_cause;
}

_Noreturn void tfw_cm_system_reset(void)
{
    abort();        /* no case resets */
}

void tfw_cm_soc_watchdog_arm(void)
{
}
