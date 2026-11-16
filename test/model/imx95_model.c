/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 register model (imx95_model.h says how an access is seen).
 *
 * Every address and bit here is a literal, written from the block layouts
 * soc_regs.h cites, not taken from soc_regs.h: a register the SoC code moves
 * becomes an unmodelled access, and the test that drove it fails.
 *
 *   LPUART3  0x42570000: GLOBAL +0x08 (RST bit 1), BAUD +0x10, STAT +0x14
 *            (TDRE bit 23, TC 22, RDRF 21, OR 19), CTRL +0x18 (TIE bit 23,
 *            RIE 21, TE 19, RE 18), DATA +0x1C (RXEMPT bit 12)
 *   RGPIO    GPIO1 0x47400000, GPIO2 0x43810000, GPIO3 0x43820000,
 *            GPIO4 0x43840000, GPIO5 0x43850000: PSOR +0x44, PCOR +0x48,
 *            PDIR +0x50, PDDR +0x54
 *   WDOG5    0x424B0000: CS +0x0 (UPDATE bit 5, EN 7, ULK 11, RCS 10,
 *            CMD32EN 13, FLG 14), CNT +0x4 (unlock key 0xD928C520, refresh
 *            key 0xB480A602), TOVAL +0x8
 *   NVIC     ISER 0xE000E100, ICER 0xE000E180, ICPR 0xE000E280, three words
 *            (ARMv7-M ARM, B3.4)
 *   MU       at the base a test sets: GCR +0x114, a raised bit rings the
 *            System Manager, which takes it (clears it) when it answers
 *   SCMI     the shared memory area at the base a test sets, word by word;
 *            its layout is the test's to read (DEN0056)
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "imx95_model.h"

#define LPUART      0x42570000u
#define WDOG        0x424B0000u

#define STAT_TAG    0x00000001u   /* the model's: no code reads STAT bit 0 */
#define STAT_TDRE   0x00800000u
#define STAT_TC     0x00400000u
#define STAT_RDRF   0x00200000u
#define STAT_OR     0x00080000u
#define CTRL_TE     0x00080000u
#define CTRL_RE     0x00040000u
#define DATA_TAG    0x80000000u   /* the model's: no code reads DATA bit 31 */
#define DATA_RXEMPT 0x00001000u
#define GLOBAL_RST  0x00000002u
#define BAUD_RESET  0x0F000004u   /* OSR 15 (x16), SBR 4 */

#define CS_UPDATE   0x00000020u
#define CS_EN       0x00000080u
#define CS_CLK_LPO  0x00000100u
#define CS_RCS      0x00000400u
#define CS_ULK      0x00000800u
#define CS_CMD32EN  0x00002000u
#define CS_FLG      0x00004000u
#define KEY_UNLOCK  0xD928C520u
#define KEY_REFRESH 0xB480A602u

enum reg {
    R_NONE, R_GLOBAL, R_BAUD, R_STAT, R_CTRL, R_DATA,
    R_PSOR, R_PCOR, R_PDIR, R_PDDR,
    R_CS, R_CNT, R_TOVAL,
    R_ISER, R_ICER, R_ICPR,
    R_GCR, R_SHM,
    R_PAR, R_TSR, R_RSR, R_TR, R_RR,
    R_OTHER
};

static const uint32_t bank_base[TFW_IMX95_MODEL_BANKS] = {
    0x47400000u, 0x43810000u, 0x43820000u, 0x43840000u, 0x43850000u,
};

struct tfw_imx95_model tfw_imx95_model;

static volatile uint32_t cell;      /* the word the last access was given */
static uint32_t presented;          /* what it held when given */
static enum reg pending = R_NONE;   /* the register it stands for */
static uint32_t pending_idx;        /* its bank or NVIC word */

static enum reg decode(uintptr_t addr, uint32_t *idx)
{
    const struct tfw_imx95_model *m = &tfw_imx95_model;
    uint32_t i;

    *idx = 0u;
    switch (addr) {
    case LPUART + 0x08u: return R_GLOBAL;
    case LPUART + 0x10u: return R_BAUD;
    case LPUART + 0x14u: return R_STAT;
    case LPUART + 0x18u: return R_CTRL;
    case LPUART + 0x1Cu: return R_DATA;
    case WDOG + 0x0u:    return R_CS;
    case WDOG + 0x4u:    return R_CNT;
    case WDOG + 0x8u:    return R_TOVAL;
    default:             break;
    }
    for (i = 0u; i < 3u; i++) {
        *idx = i;
        if (addr == 0xE000E100u + 4u * i) { return R_ISER; }
        if (addr == 0xE000E180u + 4u * i) { return R_ICER; }
        if (addr == 0xE000E280u + 4u * i) { return R_ICPR; }
    }
    for (i = 0u; i < TFW_IMX95_MODEL_BANKS; i++) {
        *idx = i;
        if (addr == bank_base[i] + 0x44u) { return R_PSOR; }
        if (addr == bank_base[i] + 0x48u) { return R_PCOR; }
        if (addr == bank_base[i] + 0x50u) { return R_PDIR; }
        if (addr == bank_base[i] + 0x54u) { return R_PDDR; }
    }
    if (m->scmi.mu_base != 0u && addr == m->scmi.mu_base + 0x114u) {
        return R_GCR;
    }
    if (m->scmi.shm_base != 0u && addr >= m->scmi.shm_base && addr < m->scmi.shm_base + 4u * m->scmi.shm_words &&
        ((addr - m->scmi.shm_base) & 3u) == 0u) {
        *idx = (uint32_t)((addr - m->scmi.shm_base) / 4u);
        return R_SHM;
    }
    if (m->ele.mu_base != 0u) {
        if (addr == m->ele.mu_base + 0x004u) { return R_PAR; }
        if (addr == m->ele.mu_base + 0x124u) { return R_TSR; }
        if (addr == m->ele.mu_base + 0x12Cu) { return R_RSR; }
        for (i = 0u; i < 16u; i++) {
            *idx = i;
            if (addr == m->ele.mu_base + 0x200u + 4u * i) { return R_TR; }
            if (addr == m->ele.mu_base + 0x280u + 4u * i) { return R_RR; }
        }
        *idx = 0u;
    }
    return R_OTHER;
}

/* The register as a read sees it now. */
/* The low `n` bits: every one of `n` transmit registers empty. */
static uint32_t ele_mask(uint32_t n)
{
    return n >= 32u ? 0xFFFFFFFFu : (1u << n) - 1u;
}

/* The receive register count PAR gives, at least one so a test that leaves
 * PAR zero reads nothing as full rather than dividing by it. */
static uint32_t ele_count_rr(const struct tfw_imx95_model *m)
{
    uint32_t n = (m->ele.par >> 8) & 0xFFu;

    return n == 0u ? 1u : n;
}

static uint32_t view(enum reg r, uint32_t idx)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;
    uint32_t v;

    switch (r) {
    case R_GLOBAL: return m->lpuart.global;
    case R_BAUD:   return m->lpuart.baud;
    case R_CTRL:   return m->lpuart.ctrl;
    case R_STAT:
        v = STAT_TAG;
        if (!m->lpuart.busy)                     { v |= STAT_TDRE | STAT_TC; }
        if (m->lpuart.rx_pos < m->lpuart.rx_len) { v |= STAT_RDRF; }
        if (m->lpuart.overrun)                   { v |= STAT_OR; }
        return v;
    case R_DATA:
        if (m->lpuart.rx_pos < m->lpuart.rx_len) {
            return DATA_TAG | m->lpuart.rx[m->lpuart.rx_pos];
        }
        return DATA_TAG | DATA_RXEMPT;
    case R_PDIR:
        return (m->gpio[idx].pdor & m->gpio[idx].pddr) |
               (m->gpio[idx].pins & ~m->gpio[idx].pddr);
    case R_PDDR:   return m->gpio[idx].pddr;
    case R_CS:
        v = m->wdog.cs;
        if (m->wdog.flg)      { v |= CS_FLG; }
        if (m->wdog.unlocked) { v |= CS_ULK; }
        if (m->wdog.rcs)      { v |= CS_RCS; }
        return v;
    case R_TOVAL:  return m->wdog.toval;
    case R_ISER:   return m->nvic.iser[idx];
    case R_GCR:    return m->scmi.gcr;
    case R_SHM:    return m->scmi.shm[idx];
    case R_PAR:    return m->ele.par;
    case R_TSR:    return m->ele.tx_busy ? 0u : ele_mask(m->ele.par & 0xFFu);
    case R_RSR:
        if (m->ele.rsp_pos < m->ele.rsp_len) {
            return 1u << (m->ele.rsp_pos % ele_count_rr(m));
        }
        return 0u;
    case R_RR:
        if (m->ele.rsp_pos < m->ele.rsp_len) {
            return m->ele.rsp[m->ele.rsp_pos];
        }
        return 0u;
    default:       return 0u;   /* PSOR, PCOR, CNT, ICER, ICPR read zero */
    }
}

static void lpuart_reset(void)
{
    tfw_imx95_model.lpuart.baud    = BAUD_RESET;
    tfw_imx95_model.lpuart.ctrl    = 0u;
    tfw_imx95_model.lpuart.overrun = 0u;
}

static void on_read(enum reg r, uint32_t idx)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;

    if (r == R_SHM && idx == 1u) {
        m->scmi.status_reads++;   /* channel_status, +0x04 */
    }

    if (r == R_DATA && m->lpuart.rx_pos < m->lpuart.rx_len) {
        m->lpuart.rx_pos++;
    }

    if (r == R_RR) {
        if (m->ele.rsp_pos < m->ele.rsp_len && idx == m->ele.rsp_pos % ele_count_rr(m)) {
            m->ele.rsp_pos++;
        } else {
            m->ele.out_of_turn++;
        }
    }
}

static void wdog_write_cs(uint32_t v)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;

    m->wdog.cs_written = v;
    if (!m->wdog.unlocked) {
        m->wdog.refused++;
        return;
    }
    if ((v & CS_FLG) != 0u) {
        m->wdog.flg = 0u;
    }
    m->wdog.cs       = v & ~(CS_FLG | CS_ULK | CS_RCS);
    m->wdog.unlocked = 0u;
    m->wdog.rcs      = 1u;
    if ((v & CS_UPDATE) == 0u) {
        m->wdog.fixed = 1u;
    }
    if (m->primask != 0u) {
        m->wdog.masked_configs++;
    }
}

static void wdog_write_cnt(uint32_t v)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;

    if (v == KEY_UNLOCK && !m->wdog.fixed) {
        m->wdog.unlocked = 1u;
        m->wdog.rcs      = 0u;
        m->wdog.unlocks++;
    } else if (v == KEY_REFRESH && (m->wdog.cs & CS_CMD32EN) != 0u) {
        m->wdog.refreshes++;
    } else if (v != KEY_UNLOCK) {
        m->wdog.bad_keys++;
    }
}

static void mu_write_gcr(uint32_t v)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;
    uint32_t raised = v & ~m->scmi.gcr;

    m->scmi.gcr = v;
    if (raised == 0u) {
        return;
    }
    m->scmi.rings++;
    m->scmi.rung = raised;
    if (m->scmi.platform != NULL) {
        m->scmi.platform(m);
        m->scmi.gcr &= ~raised;   /* taken */
    }
}

/* A word through TR `idx`: it must be the next word's register. Once the
 * header's size is in, the enclave answers. */
static void ele_write_tr(uint32_t idx, uint32_t v)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;
    uint32_t ntr = m->ele.par & 0xFFu;
    uint32_t size;

    if (ntr == 0u || idx != m->ele.req_len % ntr || m->ele.req_len >= TFW_IMX95_MODEL_ELE_WORDS) {
        m->ele.out_of_turn++;
        return;
    }
    m->ele.req[m->ele.req_len++] = v;
    size = (m->ele.req[0] >> 8) & 0xFFu;
    if (m->ele.req_len == size && m->ele.enclave != NULL) {
        m->ele.requests++;
        m->ele.enclave(m);
    }
}

static void on_write(enum reg r, uint32_t idx, uint32_t v)
{
    struct tfw_imx95_model *m = &tfw_imx95_model;

    switch (r) {
    case R_GLOBAL:
        m->lpuart.global = v;
        if ((v & GLOBAL_RST) != 0u) {
            lpuart_reset();
            m->lpuart.resets++;
        }
        break;
    case R_BAUD:
        if ((m->lpuart.ctrl & (CTRL_TE | CTRL_RE)) != 0u) {
            m->lpuart.baud_while_enabled++;
        }
        m->lpuart.baud = v;
        break;
    case R_STAT:
        if ((v & STAT_OR) != 0u) {
            m->lpuart.overrun = 0u;
        }
        break;
    case R_CTRL:
        if (m->primask == 0u) {
            m->lpuart.ctrl_unmasked++;
        }
        m->lpuart.ctrl = v;
        break;
    case R_DATA:
        if ((m->lpuart.ctrl & CTRL_TE) == 0u || m->lpuart.tx_len >= TFW_IMX95_MODEL_TX_MAX) {
            m->lpuart.tx_dropped++;
        } else {
            m->lpuart.tx[m->lpuart.tx_len++] = (uint8_t)(v & 0xFFu);
        }
        break;
    case R_PSOR: m->gpio[idx].pdor |= v;  break;
    case R_PCOR: m->gpio[idx].pdor &= ~v; break;
    case R_PDDR:
        if (m->primask == 0u) {
            m->gpio[idx].pddr_unmasked++;
        }
        m->gpio[idx].pddr = v;
        break;
    case R_CS:    wdog_write_cs(v);  break;
    case R_CNT:   wdog_write_cnt(v); break;
    case R_TOVAL:
        if (m->wdog.unlocked) {
            m->wdog.toval = v;
        } else {
            m->wdog.refused++;
        }
        break;
    case R_ISER: m->nvic.iser[idx] |= v;  break;
    case R_ICER: m->nvic.iser[idx] &= ~v; break;
    case R_ICPR: m->nvic.icpr[idx] |= v;  break;
    case R_SHM:  m->scmi.shm[idx] = v;   break;
    case R_GCR:  mu_write_gcr(v);        break;
    case R_TR:   ele_write_tr(idx, v);   break;
    default: break;
    }
}

static void settle(void)
{
    uint32_t now = cell;

    if (pending == R_NONE || pending == R_OTHER) {
        pending = R_NONE;
        return;
    }
    if (pending == R_TR || (pending != R_RR && now != presented)) {
        on_write(pending, pending_idx, now);
    } else {
        on_read(pending, pending_idx);
    }
    pending = R_NONE;
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    settle();
    pending = decode(addr, &pending_idx);
    if (pending == R_OTHER) {
        tfw_imx95_model.unmodelled++;
        tfw_imx95_model.unmodelled_addr = addr;
    }
    presented = view(pending, pending_idx);
    cell      = presented;
    return &cell;
}

void tfw_imx95_model_reset(void)
{
    memset(&tfw_imx95_model, 0, sizeof tfw_imx95_model);
    lpuart_reset();
    tfw_imx95_model.wdog.cs    = CS_UPDATE | CS_EN | CS_CLK_LPO | CS_CMD32EN;
    tfw_imx95_model.wdog.toval = 0xFFFFu;
    pending = R_NONE;
}

struct tfw_imx95_model *tfw_imx95_model_get(void)
{
    settle();
    return &tfw_imx95_model;
}

void tfw_imx95_model_line_rx(const uint8_t *bytes, uint32_t n)
{
    struct tfw_imx95_model *m = tfw_imx95_model_get();
    uint32_t i;

    for (i = 0u; i < n && m->lpuart.rx_len < TFW_IMX95_MODEL_RX_MAX; i++) {
        m->lpuart.rx[m->lpuart.rx_len++] = bytes[i];
    }
}

uint32_t tfw_imx95_model_irq_save(void)
{
    uint32_t was = tfw_imx95_model.primask;

    settle();
    tfw_imx95_model.primask = 1u;
    return was;
}

void tfw_imx95_model_irq_restore(uint32_t primask)
{
    settle();
    tfw_imx95_model.primask = primask;
}

void tfw_imx95_model_irq_mask(uint32_t masked)
{
    settle();
    tfw_imx95_model.primask = masked;
}
