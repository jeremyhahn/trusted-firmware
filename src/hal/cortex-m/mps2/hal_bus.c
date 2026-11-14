/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 buses: I2C, bit-banged over the shield 0 SBCon (sbcon.h). The block
 * is two open-collector lines and nothing else, so this file is the I2C master:
 * START, the address and direction bit, the bytes and their acknowledges, the
 * repeated START into a read and the STOP, each half clock period timed with
 * tfw_hal_delay_us -- here the delay is the protocol. The bus is the I2C-bus
 * specification's (NXP UM10204); the line control is DUI 0224I's, whose note
 * that SDA must be set HIGH before it is read is every sda_in() below.
 *
 * The status codes are the AVR TWI driver's for the same cases
 * (src/hal/avr/hal_bus.c): an address or data byte nobody acknowledges is
 * TFW_ERR_NACK; a bus that does not come free -- SDA held low through nine
 * recovery clocks, SCL held low past the stretch limit -- is TFW_ERR_TIMEOUT.
 * Unlike the TWI driver, a data NACK also sends STOP, so the bus is left free
 * on every return that reached it.
 *
 * SPI is the shield 0 PL022 (pl022.h): a master in 8-bit Motorola frames, the
 * mode and divisor SPI_CONFIG passes mapped onto SSPCR0, the select the FPGA
 * I/O block's MISC bit 8, driven around each transfer as the AVR driver drives
 * its cs pin. The one select it accepts is that bit; no board table names it
 * yet, so the profiles say TFW_FEATURE_SPI 0.
 *
 * 1-Wire is bit-banged on GPIO block 0 bit 0 (gpio_line.h), the board table's
 * pin 3 in the ONEWIRE=1 row (TFW_CM_ONEWIRE, TFW_FEATURE_ONEWIRE 1), with the
 * AVR master's slot timings; a pin other than that line is TFW_ERR_RANGE.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "sbcon.h"
#include "pl022.h"
#include "gpio_line.h"

/* SCL held low by a device past this many half periods is a stuck bus, not
 * clock stretching: 1000 at 100 kHz is 5 ms. */
#define SB_STRETCH_LIMIT 1000u
/* The clocks that free a device left mid-byte holding SDA: eight data bits and
 * its acknowledge, after which it has released the line (UM10204, 3.1.16). */
#define SB_RECOVER_CLOCKS 9u

/* The PL022's receive FIFO, 8 locations deep (DDI 0194H, 1.1): the most a
 * transfer can find left in it. */
#define SPI_FIFO_DEPTH 8u
/* A frame not done in this many microseconds is a stopped port: a byte at the
 * slowest divisor, 128, takes 1024 SSPCLK cycles. */
#define SPI_POLL_US 2000u

/* Half an SCL period in microseconds. 5 is 100 kHz, the default until init. */
static uint16_t s_half_us = 5u;

static void half(void)
{
    tfw_hal_delay_us(s_half_us);
}

static uint8_t sda_high(void)
{
    return (uint8_t)((tfw_mps2_sb_lines() & TFW_SB_SDA) != 0u);
}

/* Release SCL and wait until it is high: a device may stretch the clock. */
static tfw_status_t scl_high(void)
{
    uint16_t n;

    tfw_mps2_sb_release(TFW_SB_SCL);
    for (n = 0u; (tfw_mps2_sb_lines() & TFW_SB_SCL) == 0u; n++) {
        if (n >= SB_STRETCH_LIMIT) {
            return TFW_ERR_TIMEOUT;
        }
        half();
    }
    return TFW_OK;
}

/* A transaction ended by a stuck bus leaves both lines released. */
static tfw_status_t sb_abandon(tfw_status_t st)
{
    tfw_mps2_sb_release(TFW_SB_SCL | TFW_SB_SDA);
    return st;
}

/* The bus is free when both lines are high. A device that lost a STOP mid-read
 * may still hold SDA low; clocking it until it lets go is the recovery. */
static tfw_status_t sb_free(void)
{
    tfw_status_t st;
    uint8_t      i;

    tfw_mps2_sb_release(TFW_SB_SDA);
    st = scl_high();
    for (i = 0u; st == TFW_OK && !sda_high() && i < SB_RECOVER_CLOCKS; i++) {
        tfw_mps2_sb_drive_low(TFW_SB_SCL);
        half();
        st = scl_high();
        half();
    }
    if (st == TFW_OK && !sda_high()) {
        st = TFW_ERR_TIMEOUT;
    }
    return st;
}

/* START, or a repeated START when SCL is low after an acknowledge: SDA falls
 * while SCL is high. SDA that does not rise first is held by a device. */
static tfw_status_t sb_start(void)
{
    tfw_status_t st;

    tfw_mps2_sb_release(TFW_SB_SDA);
    half();
    st = scl_high();
    if (st != TFW_OK) {
        return st;
    }
    half();
    if (!sda_high()) {
        return TFW_ERR_TIMEOUT;
    }
    tfw_mps2_sb_drive_low(TFW_SB_SDA);
    half();
    tfw_mps2_sb_drive_low(TFW_SB_SCL);
    return TFW_OK;
}

/* STOP: SDA rises while SCL is high. Entered with SCL low. */
static tfw_status_t sb_stop(void)
{
    tfw_status_t st;

    tfw_mps2_sb_drive_low(TFW_SB_SDA);
    half();
    st = scl_high();
    half();
    tfw_mps2_sb_release(TFW_SB_SDA);
    half();
    return st;
}

/* One bit out: SDA is set while SCL is low and held through its high half. */
static tfw_status_t bit_out(uint8_t one)
{
    tfw_status_t st;

    if (one) {
        tfw_mps2_sb_release(TFW_SB_SDA);
    } else {
        tfw_mps2_sb_drive_low(TFW_SB_SDA);
    }
    half();
    st = scl_high();
    if (st != TFW_OK) {
        return st;
    }
    half();
    tfw_mps2_sb_drive_low(TFW_SB_SCL);
    return TFW_OK;
}

/* One bit in: SDA released, sampled at the end of SCL's high half. */
static tfw_status_t bit_in(uint8_t *one)
{
    tfw_status_t st;

    tfw_mps2_sb_release(TFW_SB_SDA);
    half();
    st = scl_high();
    if (st != TFW_OK) {
        return st;
    }
    half();
    *one = sda_high();
    tfw_mps2_sb_drive_low(TFW_SB_SCL);
    return TFW_OK;
}

/* Eight bits, most significant first, then the receiver's acknowledge: SDA
 * low is ACK. */
static tfw_status_t byte_out(uint8_t b)
{
    tfw_status_t st;
    uint8_t      i;
    uint8_t      nack;

    for (i = 0u; i < 8u; i++) {
        st = bit_out((uint8_t)(((unsigned)b >> (7u - i)) & 1u));
        if (st != TFW_OK) {
            return st;
        }
    }
    st = bit_in(&nack);
    if (st != TFW_OK) {
        return st;
    }
    return nack ? TFW_ERR_NACK : TFW_OK;
}

/* Eight bits in, then ACK, or NACK after the last byte: the NACK is how the
 * device is told to stop driving SDA. */
static tfw_status_t byte_in(uint8_t *b, uint8_t last)
{
    uint8_t      i;
    uint8_t      bit;
    uint8_t      v = 0u;

    for (i = 0u; i < 8u; i++) {
        tfw_status_t st = bit_in(&bit);
        if (st != TFW_OK) {
            return st;
        }
        v = (uint8_t)((v << 1) | bit);
    }
    *b = v;
    return bit_out(last);
}

/* The address byte after a START; a NACK there ends the transaction. */
static tfw_status_t sb_address(uint8_t addr_rw)
{
    tfw_status_t st;

    st = sb_start();
    if (st == TFW_OK) {
        st = byte_out(addr_rw);
    }
    return st;
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    uint32_t half_us;

    /* Half a period, rounded UP, so the clock never runs faster than asked
     * (the TWI driver's rule). 400 kHz gives 2 us, 250 kHz. A rate whose half
     * period a 16-bit delay cannot hold is refused, not clamped. */
    if (hz == 0uL) {
        return TFW_ERR_RANGE;
    }
    half_us = 500000u / hz + ((500000u % hz) != 0u ? 1u : 0u);
    if (half_us > 0xFFFFuL) {
        return TFW_ERR_RANGE;
    }
    s_half_us = (uint16_t)half_us;
    tfw_mps2_sb_release(TFW_SB_SCL | TFW_SB_SDA);
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    tfw_status_t st;
    uint8_t      i;

    if (wlen == 0u && rlen == 0u) {
        return TFW_OK;
    }
    st = sb_free();
    if (st != TFW_OK) {
        return sb_abandon(st);
    }
    if (wlen) {
        st = sb_address((uint8_t)(addr7 << 1));
        for (i = 0u; st == TFW_OK && i < wlen; i++) {
            st = byte_out(wr[i]);
        }
    }
    if (st == TFW_OK && rlen) {
        st = sb_address((uint8_t)(((unsigned)addr7 << 1) | 1u));
        for (i = 0u; st == TFW_OK && i < rlen; i++) {
            st = byte_in(&rd[i], (uint8_t)(i + 1u == rlen));
        }
    }
    if (st == TFW_ERR_TIMEOUT) {
        return sb_abandon(st);
    }
    if (sb_stop() != TFW_OK) {
        return sb_abandon(TFW_ERR_TIMEOUT);
    }
    return st;
}

/* The divisor AVR's SPR1:SPR0 picks, 4, 16, 64 or 128 (src/caps/bus.c passes
 * SPI_CONFIG's clkdiv as those two bits), made here from SSPCPSR's smallest
 * prescale, 2, and 1 + SCR: the bit rate is F_SSPCLK / (CPSDVSR * (1 + SCR))
 * (DDI 0194H, 3.3.1, Table 3-2). */
static const uint8_t k_spi_scr[4] = { 1u, 7u, 31u, 63u };

static uint8_t s_spi_lsb_first;

/* The PL022 shifts most significant bit first and has no other order, so a
 * least-significant-first bus is the same frames with each byte reversed. */
static uint8_t spi_order(uint8_t b)
{
    uint8_t r = 0u;
    uint8_t i;

    if (!s_spi_lsb_first) {
        return b;
    }
    for (i = 0u; i < 8u; i++) {
        r = (uint8_t)(((unsigned)r << 1) | (((unsigned)b >> i) & 1u));
    }
    return r;
}

/* Poll SSPSR for any of mask, a microsecond apart, up to SPI_POLL_US. */
static tfw_status_t spi_wait(uint32_t mask)
{
    uint16_t n;

    for (n = 0u; (tfw_mps2_ssp_read(TFW_SSP_SR) & mask) == 0u; n++) {
        if (n >= SPI_POLL_US) {
            return TFW_ERR_TIMEOUT;
        }
        tfw_hal_delay_us(1u);
    }
    return TFW_OK;
}

static void spi_release(void)
{
    tfw_mps2_misc_write(tfw_mps2_misc_read() | TFW_MPS2_MISC_SHIELD0_NCS);
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    /* Deselected first, as on AVR: this runs at boot (tfw_bus_init) and must
     * not select the shield device with no transfer under way. Then the port
     * is stopped while its frame is set -- MS may change only with SSE 0 --
     * and started: master, Motorola SPI, 8-bit frames, mode bit 1 the clock
     * polarity and bit 0 its phase (the AVR SPCR's CPOL:CPHA). */
    spi_release();
    tfw_mps2_ssp_write(TFW_SSP_CR1, 0u);
    tfw_mps2_ssp_write(TFW_SSP_CPSR, 2u);
    tfw_mps2_ssp_write(TFW_SSP_CR0,
                       ((uint32_t)k_spi_scr[clkdiv & 0x03u] << TFW_SSP_CR0_SCR_SHIFT)
                       | ((mode & 0x01u) ? TFW_SSP_CR0_SPH : 0u)
                       | ((mode & 0x02u) ? TFW_SSP_CR0_SPO : 0u)
                       | TFW_SSP_CR0_DSS_8);
    tfw_mps2_ssp_write(TFW_SSP_CR1, TFW_SSP_CR1_SSE);
    s_spi_lsb_first = (uint8_t)(lsb_first != 0u);
    return TFW_OK;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    uint8_t i;
    uint8_t n;

    /* The one select the image routes to the shield: anything else would
     * clock bytes with no device selected. */
    if (cs->port != TFW_MPS2_PORT_MISC || cs->bit != TFW_MPS2_SPI_CS_BIT) {
        return TFW_ERR_BAD_PIN;
    }
    /* A byte left in the receive FIFO would be read as the first answer. */
    for (n = 0u; n < SPI_FIFO_DEPTH
                 && (tfw_mps2_ssp_read(TFW_SSP_SR) & TFW_SSP_SR_RNE) != 0u; n++) {
        (void)tfw_mps2_ssp_read(TFW_SSP_DR);
    }

    tfw_mps2_misc_write(tfw_mps2_misc_read() & ~(uint32_t)TFW_MPS2_MISC_SHIELD0_NCS);
    for (i = 0u; i < len; i++) {
        /* One frame in flight at a time: its answer is read before the next
         * byte is written, so rd may be wr. */
        if (spi_wait(TFW_SSP_SR_TNF) != TFW_OK) {
            spi_release();
            return TFW_ERR_TIMEOUT;
        }
        tfw_mps2_ssp_write(TFW_SSP_DR, spi_order(wr[i]));
        if (spi_wait(TFW_SSP_SR_RNE) != TFW_OK) {
            /* Released on the failure path too, as on AVR. */
            spi_release();
            return TFW_ERR_TIMEOUT;
        }
        rd[i] = spi_order((uint8_t)tfw_mps2_ssp_read(TFW_SSP_DR));
    }
    spi_release();
    return TFW_OK;
}

/* ---------------------------------------------------------------- 1-Wire
 *
 * Bit-banged on GPIO block 0's 1-Wire bit (gpio_line.h), with the AVR's slot
 * timings (src/hal/avr/hal_bus.c): open drain, the line pulled low as an
 * output or let float as an input, never driven high. Each slot, the reset
 * and every bit, runs with interrupts masked, so a UART interrupt cannot
 * stretch a low into a different bit; between slots they are taken. */

static uint8_t ow_pin_ok(const tfw_pinref_t *p)
{
    return (uint8_t)(p->port == TFW_MPS2_PORT_GPIO0 && p->bit == TFW_MPS2_OW_BIT);
}

static uint8_t ow_level(void)
{
    return (uint8_t)((tfw_mps2_gpio_lines() & (1u << TFW_MPS2_OW_BIT)) != 0u);
}

static void ow_write_bit(uint8_t bit)
{
    uint32_t saved = tfw_mps2_gpio_irq_save();

    tfw_mps2_gpio_drive_low(1u << TFW_MPS2_OW_BIT);
    if (bit) { tfw_hal_delay_us(6u);  tfw_mps2_gpio_release(1u << TFW_MPS2_OW_BIT); tfw_hal_delay_us(64u); }
    else     { tfw_hal_delay_us(60u); tfw_mps2_gpio_release(1u << TFW_MPS2_OW_BIT); tfw_hal_delay_us(10u); }
    tfw_mps2_gpio_irq_restore(saved);
}

static uint8_t ow_read_bit(void)
{
    uint32_t saved = tfw_mps2_gpio_irq_save();
    uint8_t  v;

    tfw_mps2_gpio_drive_low(1u << TFW_MPS2_OW_BIT);
    tfw_hal_delay_us(6u);
    tfw_mps2_gpio_release(1u << TFW_MPS2_OW_BIT);
    tfw_hal_delay_us(9u);
    v = ow_level();
    tfw_hal_delay_us(55u);
    tfw_mps2_gpio_irq_restore(saved);
    return v;
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    uint32_t saved;

    if (!ow_pin_ok(p)) {
        return TFW_ERR_RANGE;
    }
    saved = tfw_mps2_gpio_irq_save();
    tfw_mps2_gpio_drive_low(1u << TFW_MPS2_OW_BIT);
    tfw_hal_delay_us(480u);
    tfw_mps2_gpio_release(1u << TFW_MPS2_OW_BIT);
    tfw_hal_delay_us(70u);
    /* A device answers by pulling the line low; the line reading high here
     * means nobody is there. */
    *out_presence = (uint8_t)(ow_level() == 0u);
    tfw_hal_delay_us(410u);
    tfw_mps2_gpio_irq_restore(saved);
    return TFW_OK;
}

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t b;

    if (!ow_pin_ok(p)) {
        return TFW_ERR_RANGE;
    }
    for (i = 0u; i < len; i++) {
        for (b = 0u; b < 8u; b++) {
            ow_write_bit((uint8_t)(((unsigned)buf[i] >> b) & 1u));   /* LSB first */
        }
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t b;

    if (!ow_pin_ok(p)) {
        return TFW_ERR_RANGE;
    }
    for (i = 0u; i < len; i++) {
        uint8_t v = 0u;

        for (b = 0u; b < 8u; b++) {
            if (ow_read_bit()) { v = (uint8_t)(v | (1u << b)); }
        }
        buf[i] = v;
    }
    return TFW_OK;
}
