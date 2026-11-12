/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 register model (rp2350_model.h says how an access is seen).
 *
 * Every address and bit here is a literal, written from the RP2350
 * datasheet's register lists and pico-sdk 2.1.1's hardware_regs names, not
 * taken from soc_regs.h: a register the SoC code moves becomes an unmodelled
 * access, and the test that drove it fails.
 *
 *   CLOCKS   0x40010000: CLK_ADC_CTRL +0x6C (ENABLE bit 11, AUXSRC 7:5),
 *            CLK_ADC_DIV +0x70
 *   RESETS   0x40020000: RESET +0x0 (ADC bit 0) and its set (+0x2000) and
 *            clear (+0x3000) aliases, RESET_DONE +0x8
 *   IO_BANK0 0x40028000: GPIOn_CTRL +0x004 + 8n
 *   PADS     0x40038000: GPIOn +0x004 + 4n, reset 0x116
 *   ADC      0x400A0000: CS +0x0 (EN 0, START_ONCE 2, READY 8, ERR 9,
 *            AINSEL 15:12), RESULT +0x4 (11:0)
 */
#include <stdint.h>
#include <string.h>

#include "rp2350_model.h"

#define CLK_ADC_CTRL 0x4001006Cu
#define CLK_ADC_DIV  0x40010070u
#define CLK_ENABLE   0x00000800u
#define RESETS       0x40020000u
#define RESET_DONE   0x40020008u
#define RESET_ADC    0x00000001u
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
    if ((m.reset & RESET_ADC) != 0u || (m.clk_adc_ctrl & CLK_ENABLE) == 0u) {
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
    case CLK_ADC_CTRL:      m.clk_adc_ctrl = v; return;
    case CLK_ADC_DIV:       m.clk_adc_div = v; return;
    case RESETS:            m.reset = v; return;
    case RESETS + 0x2000u:  m.reset |= v; return;
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

volatile uint32_t *tfw_rp2350_model_reg(uintptr_t addr)
{
    settle();
    shown = view(addr);
    scratch = shown;
    pending_addr = addr;
    pending = 1u;
    return &scratch;
}
