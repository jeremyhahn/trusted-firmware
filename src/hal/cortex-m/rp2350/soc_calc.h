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
 *   a GPIO pad's value for a tfw pin mode.
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

#endif /* TFW_CM_RP2350_CALC_H */
