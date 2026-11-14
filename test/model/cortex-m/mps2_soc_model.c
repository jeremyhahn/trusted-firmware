/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The register file mps2_soc_model.h describes. TFW_CM_REG hands out one
 * cell per access and the next access (or settle()) takes what was stored
 * in it as a write to that address. hal_soc.c only stores, so a cell left
 * at NOT_WRITTEN was read, not written. hal_soc.c is included after this
 * file's TFW_CM_REG, with cm_regs.h's guard already defined, so its
 * soc_regs.h names compile unchanged onto the file.
 */
#include <stdint.h>
#include <string.h>

#include "mps2_soc_model.h"

#define NOT_WRITTEN 0xA5A5A5A5u
#define WDOG_KEY    0x1ACCE551u

static volatile uint32_t *mps2_reg(uintptr_t addr);

#define TFW_CM_REGS_H
#define TFW_CM_REG(addr) (*mps2_reg((uintptr_t)(addr)))

#include "hal_soc.c"

struct tfw_mps2_model tfw_mps2_model;

static uint32_t  cell = NOT_WRITTEN;
static uintptr_t cell_addr;
static int       cell_live;
static uint32_t  decoded_mark;

volatile uint32_t tfw_cm_reset_mark;

tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark)
{
    decoded_mark = mark;
    return (tfw_reset_cause_t)0;
}

static void wd_write(uintptr_t addr, uint32_t val)
{
    struct tfw_mps2_model *m = &tfw_mps2_model;
    uint32_t off = (uint32_t)(addr - TFW_MPS2_WDOG_BASE);
    int ignored = 0;

    if (addr < TFW_MPS2_WDOG_BASE || addr >= TFW_MPS2_WDOG_BASE + 0x1000u) {
        m->unmodelled++;
        return;
    }
    if (off == TFW_MPS2_MODEL_LOCK) {
        m->locked = (val != WDOG_KEY);
    } else if (m->locked) {
        ignored = 1;
        m->ignored++;
    } else if (off == TFW_MPS2_MODEL_LOAD) {
        m->load  = val;
        m->value = val;
    } else if (off == TFW_MPS2_MODEL_CTRL) {
        m->ctrl = val & 0x3u;
    } else if (off == TFW_MPS2_MODEL_INTCLR) {
        m->ris   = 0;
        m->value = m->load;
    } else {
        m->unmodelled++;
    }
    if (m->n < TFW_MPS2_MODEL_LOG_MAX) {
        m->log[m->n].off     = off;
        m->log[m->n].val     = val;
        m->log[m->n].ignored = ignored;
        m->n++;
    }
}

static void settle(void)
{
    if (cell_live && cell != NOT_WRITTEN)
        wd_write(cell_addr, cell);
    cell_live = 0;
    cell      = NOT_WRITTEN;
}

static volatile uint32_t *mps2_reg(uintptr_t addr)
{
    settle();
    cell_addr = addr;
    cell_live = 1;
    return &cell;
}

void tfw_mps2_model_reset(void)
{
    memset(&tfw_mps2_model, 0, sizeof tfw_mps2_model);
    cell              = NOT_WRITTEN;
    cell_live         = 0;
    tfw_cm_reset_mark = 0;
    decoded_mark      = 0;
}

void tfw_mps2_model_clear_log(void)
{
    tfw_mps2_model.n          = 0;
    tfw_mps2_model.ignored    = 0;
    tfw_mps2_model.unmodelled = 0;
}

void tfw_mps2_model_soc_early(void)
{
    tfw_cm_soc_early();
    settle();
}

void tfw_mps2_model_watchdog_arm(void)
{
    tfw_cm_soc_watchdog_arm();
    settle();
}

void tfw_mps2_model_watchdog_pet(void)
{
    tfw_cm_soc_watchdog_pet();
    settle();
}

void tfw_mps2_model_reset_cause(uint32_t mark)
{
    (void)tfw_cm_soc_reset_cause(mark);
}

uint32_t tfw_mps2_model_reset_mark(void)
{
    return tfw_cm_reset_mark;
}

uint32_t tfw_mps2_model_decoded_mark(void)
{
    return decoded_mark;
}
