/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 port's arithmetic and encodings: everything in the SoC files
 * that is not a register access, so a host test can pin it
 * (test/unit/test_rp2350_soc.c). Header-only and free of soc_regs.h for that
 * reason, as src/hal/avr/hal_reset_decode.h is.
 *
 *   the boot ROM's IMAGE_DEF block, which hal_soc.c places in the first 4 KiB
 *     of the image ([RP-DS], boot ROM chapter, "Blocks" and "IMAGE_DEF");
 *   the XOSC start-up delay;
 *   the PL011's baud-rate divisors;
 *   a GPIO pad's value for a tfw pin mode;
 *   the I2C master's SCL counts and spike length.
 */
#ifndef TFW_CM_RP2350_CALC_H
#define TFW_CM_RP2350_CALC_H

#include <stdint.h>

/* ---- IMAGE_DEF
 *
 * The smallest block the boot ROM accepts as a bootable Arm image: one
 * IMAGE_TYPE item and the LAST item, in a loop of one (the link is 0, to
 * itself). With no VECTOR_TABLE item the ROM takes the stack pointer and the
 * entry point from the vector table at the image's start, which is where
 * mk/arch/cortex-m/rp2350.ld puts it. No signature and no hash item: secure
 * boot is an OTP write and stays off (docs/multi-arch/adr-rp2350-pico2w-onboarding.md).
 *
 * Checked against [RP-DS] release 8: the markers and the block loop §5.9.1
 * table 487; IMAGE_TYPE and its flags §5.9.3.1 tables 492-493; LAST (0xff,
 * a 16-bit size, a pad byte) §5.9.1; the first-4 KiB search §5.1.12.
 */
#define TFW_RP_BLOCK_START        0xFFFFDED3u
#define TFW_RP_BLOCK_END          0xAB123579u
#define TFW_RP_ITEM_IMAGE_TYPE    0x42u   /* one-byte size form */
#define TFW_RP_ITEM_LAST          0xFFu

#define TFW_RP_IMAGE_TYPE_EXE     0x0001u
#define TFW_RP_IMAGE_SECURE       0x0020u /* EXE_SECURITY: 2, Secure */
#define TFW_RP_IMAGE_CPU_ARM      0x0000u
#define TFW_RP_IMAGE_CHIP_RP2350  0x1000u

#define TFW_RP_IMAGE_TYPE_FLAGS \
    (TFW_RP_IMAGE_TYPE_EXE | TFW_RP_IMAGE_SECURE | TFW_RP_IMAGE_CPU_ARM | \
     TFW_RP_IMAGE_CHIP_RP2350)

/* An item's first word: its type, its size in words, then the item's data. */
#define TFW_RP_ITEM_WORD(type, words, data) \
    ((uint32_t)(type) | ((uint32_t)(words) << 8) | ((uint32_t)(data) << 16))

#define TFW_RP_IMAGE_DEF_WORDS 5u
#define TFW_RP_IMAGE_DEF_INIT { \
    TFW_RP_BLOCK_START, \
    TFW_RP_ITEM_WORD(TFW_RP_ITEM_IMAGE_TYPE, 1u, TFW_RP_IMAGE_TYPE_FLAGS), \
    TFW_RP_ITEM_WORD(TFW_RP_ITEM_LAST, 1u, 0u), \
    0u, \
    TFW_RP_BLOCK_END }

/* ---- XOSC
 *
 * STARTUP.DELAY counts in 256-cycle units of the crystal: about 1 ms of
 * them, times 64 for the margin a slow crystal on a cold board needs. The
 * field is 14 bits.
 *
 * [RP-DS] release 8 §8.2.4 gives the base, (f * 1 ms) / 256 = 47 at 12 MHz,
 * and §8.2.8 table 634 the 14-bit field. The datasheet rounds the base up
 * where this rounds to nearest; both give 47 at 12 MHz. The x64 is this
 * port's own margin and has no primary source: the datasheet names no
 * multiplier, and pico-sdk 2.1.1's default is 6
 * (src/rp2_common/hardware_xosc/include/hardware/xosc.h line 18,
 * PICO_XOSC_STARTUP_DELAY_MULTIPLIER; neither pico2.h nor pico2_w.h sets it).
 * It is safe because it only waits longer: 47 * 64 = 3008 fits the 14-bit
 * field and the SDK's own 8192 bound (xosc.c line 25), and costs about 64 ms
 * once at reset.
 */
#define TFW_RP_XOSC_DELAY_MULT 64u
#define TFW_RP_XOSC_DELAY_MAX  0x3FFFu

static inline uint32_t tfw_rp_xosc_startup(uint32_t xosc_hz)
{
    return (((xosc_hz / 1000u) + 128u) / 256u) * TFW_RP_XOSC_DELAY_MULT;
}

/* ---- PL011 baud
 *
 * The divisor is clk / (16 * baud) as a 16.6 fixed-point number, rounded to
 * nearest: 64 * clk / (16 * baud) = 4 * clk / baud. The integer part must be
 * 1..65535; 0 means the clock is too slow for the rate. Returns 0 for a rate
 * the part cannot make, 1 with both divisors set otherwise. 4 * clk fits 32
 * bits for any clk_peri up to 1 GHz. [RP-DS] release 8 §12.1.7.1: its worked
 * example, 125 MHz at 115200 baud, is BRDI 67 and m 52, as this gives; the
 * divisors take effect on the LCR_H write after them.
 */
static inline uint8_t tfw_rp_uart_divisors(uint32_t clk_hz, uint32_t baud,
                                           uint32_t *ibrd, uint32_t *fbrd)
{
    uint32_t div;

    if (baud == 0u) {
        return 0u;
    }
    div = (4u * clk_hz + baud / 2u) / baud;
    if ((div >> 6) == 0u || (div >> 6) > 0xFFFFu) {
        return 0u;
    }
    *ibrd = div >> 6;
    *fbrd = div & 0x3Fu;
    return 1u;
}

/* ---- GPIO pads
 *
 * A pad comes out of reset isolated, input disabled, pulled down, with a
 * 4 mA drive and the Schmitt trigger on. A tfw pin keeps the drive and the
 * trigger, enables its input (so an output can be read back), and takes a
 * pull-up only in TFW_PIN_INPUT_PULLUP; plain input floats, as on AVR. ISO
 * is clear in every mode, which is what hands the pad to the GPIO block.
 * Fields and reset values: [RP-DS] release 8 §9.11.3 table 885.
 */
#define TFW_RP_PADV_IE       0x40u
#define TFW_RP_PADV_DRIVE_4MA 0x10u
#define TFW_RP_PADV_PUE      0x08u
#define TFW_RP_PADV_SCHMITT  0x02u

static inline uint32_t tfw_rp_pad_value(uint8_t pullup)
{
    uint32_t v = TFW_RP_PADV_IE | TFW_RP_PADV_DRIVE_4MA | TFW_RP_PADV_SCHMITT;

    if (pullup) {
        v |= TFW_RP_PADV_PUE;
    }
    return v;
}

/* ---- DW_apb_i2c SCL timing
 *
 * The SCL counts for a bus rate, from ic_clk, which is clk_sys. Up to
 * 100 kHz the controller runs in standard mode (IC_CON SPEED 1) from
 * IC_SS_SCL_HCNT/LCNT; above it, fast mode (SPEED 2 covers fast and fast
 * plus, §12.2.17 table 1109) from IC_FS_SCL_HCNT/LCNT. The counts are the
 * same arithmetic in both modes. All of [RP-DS] release 8 §12.2:
 *
 *   spike length  IC_FS_SPKLEN counts ic_clk cycles of the longest spike
 *                 filtered; the I2C-bus limit is 50 ns in SS and FS, and the
 *                 smallest value is 1 (§12.2.11). Rounded up: ceil(clk / 20 MHz).
 *   low period    (LCNT + 1) cycles; LCNT must exceed SPKLEN + 7 (§12.2.14.1).
 *   high period   (HCNT + SPKLEN + 7) cycles; HCNT must exceed SPKLEN + 5
 *                 (§12.2.14.1, figure 94).
 *
 * Table 1105 (§12.2.14) times the bus conditions from the selected mode's
 * counts: tLOW, tBUF and, in standard mode, the repeated-START setup
 * tSU;STA from LCNT; tHIGH, tHD;STA, tSU;STO and, in fast mode, tSU;STA
 * from HCNT. So a low period of at least the mode's tLOW minimum (4.7 us
 * SS, 1.3 us FS, 0.5 us FM+) and a high period of at least its tHIGH
 * minimum (4.0 us, 0.6 us, 0.26 us) meet every one of them: each other
 * minimum on the same count is no larger (I2C-bus spec UM10204 table 10).
 *
 * The period is ceil(clk / hz) cycles, so the bus never runs faster than
 * asked (the rule the MPS2 and TWI drivers keep). Low takes 3/5 of it,
 * rounded up, then each half is raised to its mode's minimum where the
 * split falls short of it. Where a minimum count wins, the bus runs slower,
 * never faster: from a 12 MHz clk_sys 400 kHz comes out at 12 MHz / 33,
 * about 364 kHz. A rate of 0, above fast plus's 1 MHz (table 1109), or
 * whose counts overflow the 16-bit registers (tables 1115-1116) is
 * refused: returns 0. */
#define TFW_RP_I2C_SS_HZ_MAX 100000u
#define TFW_RP_I2C_FS_HZ_MAX 400000u
#define TFW_RP_I2C_HZ_MAX   1000000u
#define TFW_RP_I2C_CNT_MAX  0xFFFFu
#define TFW_RP_I2C_SPK_MAX  0xFFu

/* ic_clk cycles in at least t10 * 10 ns: ceil(ceil(clk / 1 kHz) * t10 /
 * 100000), in 32 bits for any clk and t10 <= 470. */
static inline uint32_t tfw_rp_i2c_cycles(uint32_t clk_hz, uint32_t t10)
{
    uint32_t khz = clk_hz / 1000u + ((clk_hz % 1000u) != 0u ? 1u : 0u);
    uint32_t n   = khz * t10;

    return n / 100000u + ((n % 100000u) != 0u ? 1u : 0u);
}

typedef struct {
    uint32_t hcnt;
    uint32_t lcnt;
    uint32_t spklen;
} tfw_rp_i2c_timing_t;

static inline uint8_t tfw_rp_i2c_timing(uint32_t clk_hz, uint32_t hz,
                                        tfw_rp_i2c_timing_t *t)
{
    uint32_t period;
    uint32_t low;
    uint32_t high;
    uint32_t spk;
    uint32_t low_min;
    uint32_t high_min;

    if (hz == 0u || hz > TFW_RP_I2C_HZ_MAX || clk_hz == 0u) {
        return 0u;
    }
    period = clk_hz / hz + ((clk_hz % hz) != 0u ? 1u : 0u);
    if (period > 2u * (TFW_RP_I2C_CNT_MAX + 1u)) {
        return 0u;
    }
    if (hz <= TFW_RP_I2C_SS_HZ_MAX) {
        low_min  = tfw_rp_i2c_cycles(clk_hz, 470u);
        high_min = tfw_rp_i2c_cycles(clk_hz, 400u);
    } else if (hz <= TFW_RP_I2C_FS_HZ_MAX) {
        low_min  = tfw_rp_i2c_cycles(clk_hz, 130u);
        high_min = tfw_rp_i2c_cycles(clk_hz, 60u);
    } else {
        low_min  = tfw_rp_i2c_cycles(clk_hz, 50u);
        high_min = tfw_rp_i2c_cycles(clk_hz, 26u);
    }
    low  = (period * 3u + 4u) / 5u;
    high = period - low;
    if (low < low_min) {
        low = low_min;
    }
    if (high < high_min) {
        high = high_min;
    }
    spk  = clk_hz / 20000000u + ((clk_hz % 20000000u) != 0u ? 1u : 0u);
    if (spk > TFW_RP_I2C_SPK_MAX) {
        return 0u;
    }
    t->spklen = spk;
    t->lcnt   = low - 1u;
    if (t->lcnt < spk + 8u) {
        t->lcnt = spk + 8u;
    }
    t->hcnt = high > spk + 7u ? high - spk - 7u : 0u;
    if (t->hcnt < spk + 6u) {
        t->hcnt = spk + 6u;
    }
    if (t->hcnt > TFW_RP_I2C_CNT_MAX || t->lcnt > TFW_RP_I2C_CNT_MAX) {
        return 0u;
    }
    return 1u;
}

#endif /* TFW_CM_RP2350_CALC_H */
