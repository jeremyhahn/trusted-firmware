/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 register model (tone_model.h).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "tone_model.h"

#define MAX_REGS   128
#define MAX_WRITES 512

void tfw_cm_irq4(void);

struct reg {
    uintptr_t addr;
    uint32_t  value;
};

struct write {
    uintptr_t addr;
    uint32_t  value;
};

static struct reg   regs[MAX_REGS];
static int          nregs;
static struct write log_[MAX_WRITES];
static int          nlog;

static uint32_t now;
static uint32_t sio_out;
static uint32_t sio_oe;
static uint32_t t1_armed;
static uint32_t t1_intr;

/* The slot handed out last, and the value it held then. */
static volatile uint32_t pending_slot;
static uintptr_t         pending_addr;
static uint32_t          pending_seen;
static int               pending;

static uint32_t *state(uintptr_t addr)
{
    int i;

    for (i = 0; i < nregs; i++) {
        if (regs[i].addr == addr) {
            return &regs[i].value;
        }
    }
    if (nregs == MAX_REGS) {
        abort();
    }
    regs[nregs].addr  = addr;
    regs[nregs].value = 0u;
    return &regs[nregs++].value;
}

/* A peripheral register's atomic alias: 1 XOR, 2 SET, 3 CLR, 0 none. */
static unsigned alias(uintptr_t addr)
{
    if ((addr & 0xF0000000u) != 0x40000000u) {
        return 0u;
    }
    return (unsigned)((addr >> 12) & 0x3u);
}

static uint32_t read_value(uintptr_t addr)
{
    if (alias(addr) != 0u) {
        return 0u;
    }
    switch (addr) {
    case RP_MODEL_RESETS_DONE:  return ~*state(RP_MODEL_RESETS_RESET);
    case RP_MODEL_T1_TIMELR:    return now;
    case RP_MODEL_SIO_OUT:      return sio_out;
    case RP_MODEL_SIO_OE:       return sio_oe;
    case RP_MODEL_SIO_OUT_SET:
    case RP_MODEL_SIO_OUT_CLR:
    case RP_MODEL_SIO_OUT_XOR:
    case RP_MODEL_SIO_OE_SET:
    case RP_MODEL_SIO_OE_CLR:
    case RP_MODEL_T1_ARMED:
    case RP_MODEL_T1_INTR:
    case 0xE000E100u:
    case 0xE000E180u:
    case 0xE000E280u:
        return 0u;              /* write-to-act: a write of any 1 shows */
    default:
        return *state(addr);
    }
}

static void apply(uintptr_t addr, uint32_t v)
{
    unsigned a = alias(addr);

    if (nlog < MAX_WRITES) {
        log_[nlog].addr  = addr;
        log_[nlog].value = v;
        nlog++;
    }
    if (a != 0u) {
        uint32_t *s = state(addr & ~(uintptr_t)0x3000u);

        if (a == 1u) {
            *s ^= v;
        } else if (a == 2u) {
            *s |= v;
        } else {
            *s &= ~v;
        }
        return;
    }
    switch (addr) {
    case RP_MODEL_SIO_OUT:      sio_out = v;   break;
    case RP_MODEL_SIO_OUT_SET:  sio_out |= v;  break;
    case RP_MODEL_SIO_OUT_CLR:  sio_out &= ~v; break;
    case RP_MODEL_SIO_OUT_XOR:  sio_out ^= v;  break;
    case RP_MODEL_SIO_OE:       sio_oe = v;    break;
    case RP_MODEL_SIO_OE_SET:   sio_oe |= v;   break;
    case RP_MODEL_SIO_OE_CLR:   sio_oe &= ~v;  break;
    case RP_MODEL_T1_ARMED:     t1_armed &= ~v; break;
    case RP_MODEL_T1_INTR:      t1_intr &= ~v;  break;
    case RP_MODEL_T1_ALARM0:
        *state(addr) = v;
        t1_armed |= 1u;
        break;
    case RP_MODEL_NVIC_ISER0:   *state(addr) |= v; break;
    case RP_MODEL_RESETS_DONE:
    case RP_MODEL_T1_TIMELR:
        break;                  /* read-only */
    default:
        *state(addr) = v;
        break;
    }
}

static void commit(void)
{
    if (pending && pending_slot != pending_seen) {
        pending = 0;
        apply(pending_addr, pending_slot);
    }
    pending = 0;
}

volatile uint32_t *tfw_rp_model_reg(uintptr_t addr)
{
    commit();
    pending_addr = addr;
    pending_seen = read_value(addr);
    pending_slot = pending_seen;
    pending      = 1;
    return &pending_slot;
}

void tfw_rp_model_reset(void)
{
    nregs    = 0;
    nlog     = 0;
    now      = 0u;
    sio_out  = 0u;
    sio_oe   = 0u;
    t1_armed = 0u;
    t1_intr  = 0u;
    pending  = 0;
    *state(RP_MODEL_RESETS_RESET) = 0x1FFFFFFFu;
}

void tfw_rp_model_set_time(uint32_t ticks)
{
    commit();
    now = ticks;
}

uint32_t tfw_rp_model_read(uintptr_t addr)
{
    commit();
    if (addr == RP_MODEL_T1_ARMED) {
        return t1_armed;
    }
    if (addr == RP_MODEL_T1_INTR) {
        return t1_intr;
    }
    if (addr == RP_MODEL_NVIC_ISER0) {
        return *state(addr);
    }
    return read_value(addr);
}

int tfw_rp_model_find(uintptr_t addr, uint32_t value, int from)
{
    int i;

    commit();
    for (i = from < 0 ? 0 : from; i < nlog; i++) {
        if (log_[i].addr == addr && log_[i].value == value) {
            return i;
        }
    }
    return -1;
}

int tfw_rp_model_writes(void)
{
    commit();
    return nlog;
}

int tfw_rp_model_fire(void)
{
    commit();
    if ((t1_armed & 1u) == 0u || (*state(RP_MODEL_T1_INTE) & 1u) == 0u) {
        return 0;
    }
    t1_armed &= ~1u;
    t1_intr  |= 1u;
    tfw_cm_irq4();
    commit();
    return 1;
}
