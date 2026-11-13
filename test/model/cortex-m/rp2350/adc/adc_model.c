/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 register model (adc_model.h says how an access is seen).
 *
 * Every address and bit here is a literal, written from the RP2350
 * datasheet's register lists and pico-sdk 2.1.1's hardware_regs names, not
 * taken from soc_regs.h: a register the SoC code moves becomes an unmodelled
 * access, and the test that drove it fails.
 *
 *   CLOCKS   0x40010000: CLK_ADC_CTRL +0x6C (ENABLE bit 11, AUXSRC 7:5),
 *            CLK_ADC_DIV +0x70 (INT 19:16); AUXSRC 0 clksrc_pll_usb, 3 xosc
 *   RESETS   0x40020000: RESET +0x0 (ADC bit 0, PLL_USB bit 15) and its set
 *            (+0x2000) and clear (+0x3000) aliases, RESET_DONE +0x8
 *   PLL_USB  0x40058000: CS +0x0 (LOCK 31, REFDIV 5:0, reset 1), PWR +0x4
 *            (PD 0, POSTDIVPD 3, VCOPD 5, reset 0x2D) and its clear alias,
 *            FBDIV_INT +0x8 (11:0), PRIM +0xC (POSTDIV1 18:16, POSTDIV2
 *            14:12, reset 0x77000). It locks when out of reset, powered and
 *            VCO 750-1600 MHz from the 12 MHz crystal; a converter runs
 *            only on a clk_adc of exactly 48 MHz (§12.4.3).
 *   IO_BANK0 0x40028000: GPIOn_CTRL +0x004 + 8n
 *   PADS     0x40038000: GPIOn +0x004 + 4n, reset 0x116
 *   ADC      0x400A0000: CS +0x0 (EN 0, START_ONCE 2, READY 8, ERR 9,
 *            AINSEL 15:12), RESULT +0x4 (11:0)
 */
#include <stdint.h>
#include <string.h>

#include "adc_model.h"

#define CLK_ADC_CTRL 0x4001006Cu
#define CLK_ADC_DIV  0x40010070u
#define CLK_ENABLE   0x00000800u
#define RESETS       0x40020000u
#define RESET_DONE   0x40020008u
#define RESET_ADC    0x00000001u
#define RESET_PLL_USB 0x00008000u
#define PLL_CS       0x40058000u
#define PLL_PWR      0x40058004u
#define PLL_FBDIV    0x40058008u
#define PLL_PRIM     0x4005800Cu
#define PLL_LOCK     0x80000000u
#define PWR_PD       0x00000001u
#define PWR_POSTDIVPD 0x00000008u
#define PWR_VCOPD    0x00000020u
#define XOSC_HZ      12000000u
#define IO_CTRL0     0x40028004u
#define PADS0        0x40038004u
#define PAD_RESET    0x00000116u
#define ADC_CS       0x400A0000u
#define ADC_RESULT   0x400A0004u
#define CS_EN        0x00000001u
#define CS_START     0x00000004u
#define CS_READY     0x00000100u
#define CS_ERR       0x00000200u

static struct tfw_rp2350_model m;

static volatile uint32_t scratch;
static uint32_t shown;
static uintptr_t pending_addr;
static uint8_t pending;

static int gpio_of(uintptr_t a, uintptr_t base, uint32_t stride)
{
    for (uint32_t i = 0u; i < TFW_RP2350_MODEL_GPIOS; i++) {
        if (a == base + stride * (TFW_RP2350_MODEL_GPIO0 + i)) {
            return (int)i;
        }
    }
    return -1;
}

/* PLL_USB's registers at their reset values; the test's knobs and the
 * counters are kept. */
static void pll_reset(void)
{
    m.pll.refdiv = 1u;
    m.pll.fbdiv = 0u;
    m.pll.prim = 0x77000u;
    m.pll.pwr = PWR_PD | 0x4u | PWR_POSTDIVPD | PWR_VCOPD;
}

static uint64_t pll_vco_hz(void)
{
    if (m.pll.refdiv == 0u) {
        return 0u;
    }
    return (uint64_t)(XOSC_HZ / m.pll.refdiv) * m.pll.fbdiv;
}

static int pll_locked(void)
{
    uint64_t vco = pll_vco_hz();

    return (m.reset & RESET_PLL_USB) == 0u && (m.pll.pwr & (PWR_PD | PWR_VCOPD)) == 0u &&
           m.pll.fbdiv >= 16u && m.pll.fbdiv <= 320u &&
           vco >= 750000000u && vco <= 1600000000u && !m.pll.never_lock;
}

static uint32_t pll_usb_hz(void)
{
    uint32_t pd1 = (m.pll.prim >> 16) & 7u, pd2 = (m.pll.prim >> 12) & 7u;

    if (!pll_locked() || (m.pll.pwr & PWR_POSTDIVPD) != 0u || pd1 == 0u || pd2 == 0u) {
        return 0u;
    }
    return (uint32_t)(pll_vco_hz() / (pd1 * pd2));
}

static uint32_t clk_adc_hz(void)
{
    uint32_t div = (m.clk_adc_div >> 16) & 0xFu, src;

    if ((m.clk_adc_ctrl & CLK_ENABLE) == 0u) {
        return 0u;
    }
    switch ((m.clk_adc_ctrl >> 5) & 7u) {
    case 0u: src = pll_usb_hz(); break;
    case 3u: src = XOSC_HZ; break;
    default: src = 0u; break;              /* not modelled: no clock */
    }
    return src / (div == 0u ? 16u : div);
}

static uint32_t view(uintptr_t a)
{
    int g;

    switch (a) {
    case CLK_ADC_CTRL:      return m.clk_adc_ctrl;
    case CLK_ADC_DIV:       return m.clk_adc_div;
    case RESETS:            return m.reset;
    case RESETS + 0x2000u:  return 0u;
    case RESETS + 0x3000u:  return 0u;
    case RESET_DONE:        return ~m.reset;
    case PLL_CS:
        if ((m.pll.pwr & (PWR_PD | PWR_VCOPD)) == 0u) {
            m.pll.lock_polls++;
        }
        return (pll_locked() ? PLL_LOCK : 0u) | m.pll.refdiv;
    case PLL_PWR:           return m.pll.pwr;
    case PLL_PWR + 0x3000u: return 0u;
    case PLL_FBDIV:         return m.pll.fbdiv;
    case PLL_PRIM:          return m.pll.prim;
    case ADC_CS:
        return (m.adc.en ? CS_EN : 0u) | (m.adc.ready ? CS_READY : 0u) |
               (m.adc.err ? CS_ERR : 0u) | (m.adc.ainsel << 12);
    case ADC_RESULT:        return m.adc.result;
    default: break;
    }
    if ((g = gpio_of(a, IO_CTRL0, 8u)) >= 0) {
        return m.io_ctrl[g];
    }
    if ((g = gpio_of(a, PADS0, 4u)) >= 0) {
        return m.pad[g];
    }
    m.unmodelled++;
    m.unmodelled_addr = a;
    return 0u;
}

static void write_cs(uint32_t v)
{
    if ((m.reset & RESET_ADC) != 0u || (m.clk_adc_ctrl & CLK_ENABLE) == 0u ||
        clk_adc_hz() != 48000000u) {
        m.adc.ignored++;
        return;
    }
    m.adc.en = (v & CS_EN) != 0u;
    m.adc.ainsel = (v >> 12) & 0xFu;
    m.adc.ready = m.adc.en && !m.adc.stuck;
    m.adc.err = 0u;
    if (m.adc.en && (v & CS_START) != 0u && m.adc.ainsel < TFW_RP2350_MODEL_INPUTS) {
        uint32_t g = m.adc.ainsel;

        m.adc.conversions++;
        if (g < TFW_RP2350_MODEL_GPIOS) {
            m.adc.pad_at_start[g] = m.pad[g];
            m.adc.io_at_start[g] = m.io_ctrl[g];
        }
        if (!m.adc.stuck) {
            m.adc.err = m.adc.fail;
            m.adc.result = m.adc.level[g] & 0xFFFu;
        }
    }
}

static void write(uintptr_t a, uint32_t v)
{
    int g;

    switch (a) {
    case CLK_ADC_CTRL:
        if ((m.clk_adc_ctrl & CLK_ENABLE) != 0u && ((m.clk_adc_ctrl ^ v) & 0xE0u) != 0u) {
            m.auxsrc_while_enabled++;
        }
        m.clk_adc_ctrl = v;
        return;
    case PLL_CS:
        if ((m.reset & RESET_PLL_USB) == 0u) {
            m.pll.refdiv = v & 0x3Fu;
        }
        return;
    case PLL_PWR + 0x3000u:
        if ((m.reset & RESET_PLL_USB) == 0u) {
            if ((v & (PWR_PD | PWR_VCOPD)) != 0u && m.pll.fbdiv == 0u) {
                m.pll.out_of_order++;              /* powered before FBDIV */
            }
            m.pll.pwr &= ~v;
        }
        return;
    case PLL_FBDIV:
        if ((m.reset & RESET_PLL_USB) == 0u) {
            m.pll.fbdiv = v & 0xFFFu;
        }
        return;
    case PLL_PRIM:
        if ((m.reset & RESET_PLL_USB) == 0u) {
            if (!pll_locked()) {
                m.pll.out_of_order++;              /* post dividers before lock */
            }
            m.pll.prim = v & 0x77000u;
        }
        return;
    case CLK_ADC_DIV:       m.clk_adc_div = v; return;
    case RESETS:            m.reset = v; return;
    case RESETS + 0x2000u:
        m.reset |= v;
        if ((v & RESET_PLL_USB) != 0u) {
            pll_reset();
        }
        return;
    case RESETS + 0x3000u:  m.reset &= ~v; return;
    case ADC_CS:            write_cs(v); return;
    default: break;
    }
    if ((g = gpio_of(a, IO_CTRL0, 8u)) >= 0) {
        m.io_ctrl[g] = v;
        return;
    }
    if ((g = gpio_of(a, PADS0, 4u)) >= 0) {
        m.pad[g] = v;
        return;
    }
    m.unmodelled++;
    m.unmodelled_addr = a;
}

static void settle(void)
{
    if (pending) {
        pending = 0u;
        if (scratch != shown) {
            write(pending_addr, scratch);
        }
    }
}

void tfw_rp2350_model_reset(void)
{
    pending = 0u;
    memset(&m, 0, sizeof(m));
    m.reset = 0x1FFFFFFFu;
    pll_reset();
    for (uint32_t i = 0u; i < TFW_RP2350_MODEL_GPIOS; i++) {
        m.pad[i] = PAD_RESET;
        m.io_ctrl[i] = 0x1Fu;
    }
}

struct tfw_rp2350_model *tfw_rp2350_model_get(void)
{
    settle();
    return &m;
}

uint32_t tfw_rp2350_model_clk_adc_hz(void)
{
    settle();
    return clk_adc_hz();
}

volatile uint32_t *tfw_rp2350_model_reg(uintptr_t addr)
{
    settle();
    shown = view(addr);
    scratch = shown;
    pending_addr = addr;
    pending = 1u;
    return &scratch;
}
