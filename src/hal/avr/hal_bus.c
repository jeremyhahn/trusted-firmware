/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <util/delay.h>
#include "ag/hal.h"
#include "ag/config.h"

#define PIN_REG(p)  _SFR_MEM8((p)->port)
#define DDR_REG(p)  _SFR_MEM8((p)->port + 1u)
#define PORT_REG(p) _SFR_MEM8((p)->port + 2u)

/* ------------------------------------------------------------------- I2C */

/* How long to wait for one TWI state transition before giving up. At 100 kHz a
 * byte takes about 90 us, so this is generous by orders of magnitude -- it is a
 * bound, not a timing parameter. */
#define TWI_SPIN_LIMIT 60000u

static uint8_t twi_wait(void)
{
    /* BOUNDED, and that is the whole point. A bus held low by a stuck slave
     * never raises TWINT, and an unbounded wait here would hang the entire
     * device on one bad peripheral -- the failure mode where a five-dollar
     * sensor takes the controller down with it and the symptom is a board that
     * stopped answering for no visible reason.
     *
     * Written as an explicit limit rather than relying on a uint16_t wrapping
     * to zero: the wrap works, and nobody reading it a year from now would be
     * sure it was deliberate. */
    for (uint16_t spin = 0u; spin < TWI_SPIN_LIMIT; spin++) {
        if (TWCR & _BV(TWINT)) {
            return 1u;
        }
    }
    return 0u;
}

ag_status_t ag_hal_i2c_init(uint32_t hz)
{
    uint32_t divisor;

    /*
     * TWBR is an 8-bit register and SCL = F_CPU / (16 + 2 * TWBR) at this
     * prescaler of 1, so not every hz the cap layer's AG_I2C_HZ_MIN..MAX lets
     * through is one THIS BOARD can actually produce -- that interval is a
     * generic sanity check, and F_CPU is a per-board fact this file is one of
     * the few allowed to know.
     *
     * Both ends are checked rather than only clamped, because a value this
     * board cannot hit is not the same request as the nearest one it can:
     * silently substituting one would configure a DIFFERENT rate than the one
     * asked for, and on the low end that difference is not small. At 16 MHz,
     * asking for 1 kHz -- inside the generic interval -- computes a divisor
     * of 7992, which does not fit a byte; truncating it (as this function did
     * before this check existed) silently produced roughly 125 kHz, 125 times
     * the requested rate, with no error and nothing to show for it until a
     * sensor timed on that clock behaved as though its datasheet were wrong.
     *
     * divisor is the CEILING of (F_CPU/hz - 16) / 2, computed by
     * cross-multiplying rather than dividing F_CPU by hz first. A caller's hz
     * rarely divides F_CPU evenly, and rounding that first division before
     * subtracting 16 and halving can round the FINAL clock UP past what was
     * asked for -- 350 kHz at 16 MHz used to compute TWBR=14, which is
     * actually 363 636 Hz, faster than requested and past what a peripheral
     * rated for exactly 350 kHz is specified to tolerate. The ceiling always
     * chooses the larger TWBR of the two candidates, which is the SLOWER
     * actual clock, so the real rate never exceeds the one asked for. */
    if (hz == 0uL) {
        return AG_ERR_RANGE;
    }
    if (F_CPU <= 16uL * hz) {
        divisor = 0uL;                 /* at or past F_CPU/16, the fastest TWBR=0 gives */
    } else {
        uint32_t numer = F_CPU - 16uL * hz;
        uint32_t denom = 2uL * hz;
        divisor = (numer + denom - 1uL) / denom;
    }
    if (divisor > 255UL) {
        return AG_ERR_RANGE;          /* below what an 8-bit TWBR can reach */
    }
    TWSR = 0u;                                   /* prescaler 1 */
    TWBR = (uint8_t)divisor;
    TWCR = (uint8_t)_BV(TWEN);
    return AG_OK;
}

static ag_status_t twi_start(uint8_t addr_rw)
{
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWSTA) | _BV(TWEN));
    if (!twi_wait()) { return AG_ERR_TIMEOUT; }
    TWDR = addr_rw;
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN));
    if (!twi_wait()) { return AG_ERR_TIMEOUT; }
    /* 0x18 = address+W acked, 0x40 = address+R acked. Anything else means no
     * device answered, which is a NACK and not a timeout -- the distinction
     * tells a host "nothing there" rather than "the bus is broken". */
    if ((TWSR & 0xF8u) != 0x18u && (TWSR & 0xF8u) != 0x40u) {
        TWCR = (uint8_t)(_BV(TWINT) | _BV(TWSTO) | _BV(TWEN));
        return AG_ERR_NACK;
    }
    return AG_OK;
}

ag_status_t ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    ag_status_t st;
    uint8_t     i;

    if (wlen) {
        st = twi_start((uint8_t)(addr7 << 1));
        if (st != AG_OK) { return st; }
        for (i = 0u; i < wlen; i++) {
            TWDR = wr[i];
            TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN));
            if (!twi_wait()) { return AG_ERR_TIMEOUT; }
            if ((TWSR & 0xF8u) != 0x28u) { return AG_ERR_NACK; }
        }
    }
    if (rlen) {
        st = twi_start((uint8_t)((addr7 << 1) | 1u));
        if (st != AG_OK) { return st; }
        for (i = 0u; i < rlen; i++) {
            /* ACK every byte but the last: the final NACK is how the slave is
             * told to stop driving the bus. */
            TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN) |
                             ((i + 1u < rlen) ? _BV(TWEA) : 0u));
            if (!twi_wait()) { return AG_ERR_TIMEOUT; }
            rd[i] = TWDR;
        }
    }
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWSTO) | _BV(TWEN));
    return AG_OK;
}

/* ------------------------------------------------------------------- SPI */

/* Mirrors TWI_SPIN_LIMIT above, for the same reason: generous by orders of
 * magnitude against a real transfer, so it is a BOUND rather than a timing
 * parameter, and it is what turns a clock that never toggles -- a shorted
 * line, a slave holding MISO, SPE cleared because nothing ever called
 * ag_hal_spi_init() -- into AG_ERR_TIMEOUT instead of a device that never
 * answers another command again. */
#define SPI_SPIN_LIMIT 60000u

static uint8_t spi_wait(void)
{
    for (uint16_t spin = 0u; spin < SPI_SPIN_LIMIT; spin++) {
        if (SPSR & _BV(SPIF)) {
            return 1u;
        }
    }
    return 0u;
}

ag_status_t ag_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    /*
     * MOSI, SCK and SS as outputs -- and the bits come from the board, not from
     * three literals. They were 3, 5 and 2 here, which is a 328P fact: on an
     * ATmega32U4 the SPI pins are PB2, PB1 and PB0, so the literal set the
     * direction of three unrelated pins and left the real SPI lines as inputs.
     * That compiles, links and runs; the symptom is SPI that does not work,
     * with nothing pointing at this line. The same defect had already been
     * found once in the tone path and fixed there, and this call site was
     * missed -- which is the argument for the board owning every pin fact
     * rather than most of them.
     *
     * SS must be an output even when unused: leaving it an input and letting it
     * go low takes the SPI hardware out of master mode mid-transfer.
     *
     * DESELECTED FIRST, and that ordering is load-bearing. This now runs at
     * boot (main.c's ag_bus_init()), before a host has issued a single
     * SPI_XFER -- so if the hardware SS pin also happens to be a real
     * device's chip-select, as it usually is, PORTB's latch must already say
     * "high" before DDRB turns the pin into a driven output. Reversed, the
     * pin drives LOW for the instant between the two writes -- SS's reset
     * value is 0 -- which selects that device the moment DDRB takes effect,
     * with no transfer under way and no way for a host to have prevented it.
     * ag_hal_spi_xfer's own per-transfer assert/deassert is unaffected: it
     * always ends by driving its caller's cs pin high again. */
    PORTB |= (uint8_t)_BV(AG_SPI_BIT_SS);
    DDRB  |= (uint8_t)(_BV(AG_SPI_BIT_MOSI) | _BV(AG_SPI_BIT_SCK)
                      | _BV(AG_SPI_BIT_SS));
    SPCR = (uint8_t)(_BV(SPE) | _BV(MSTR)
                     | (uint8_t)((mode & 0x03u) << CPHA)
                     | (uint8_t)(clkdiv & 0x03u)
                     | (uint8_t)(lsb_first ? _BV(DORD) : 0u));
    return AG_OK;
}

/*
 * A KNOWN GAP, NOT ADDRESSED HERE: nothing marks MOSI/SCK/hardware-SS
 * reserved in the board descriptor, so PIN_MODE/DIGITAL_WRITE can still
 * reach them as ordinary GPIO after this runs -- clearing SCK's DDR bit
 * stops the clock from toggling, and driving the hardware SS pin low while
 * it is an input clears MSTR per the datasheet, dropping out of master mode
 * entirely. Re-running ag_hal_spi_init() would restore both, but nothing
 * calls it again once boot has. The same gap exists for I2C's SDA/SCL,
 * predates this file's boot-time init, and is a board-descriptor /
 * pin-ownership question -- whether these pins should be marked reserved
 * once their bus is enabled, unconditionally or only once first used --
 * bigger than bounding a wait or choosing a default, and is not decided
 * here.
 */

ag_status_t ag_hal_spi_xfer(const ag_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    uint8_t i;

    DDR_REG(cs)  |= (uint8_t)(1u << cs->bit);
    PORT_REG(cs)  = (uint8_t)(PORT_REG(cs) & (uint8_t)~(1u << cs->bit));  /* assert */

    for (i = 0u; i < len; i++) {
        SPDR = wr[i];
        if (!spi_wait()) {
            /* Released even on the failure path: leaving CS asserted after
             * giving up would hold the device that was on the other end of it
             * selected forever, which is a second failure riding on the
             * first. */
            PORT_REG(cs) |= (uint8_t)(1u << cs->bit);
            return AG_ERR_TIMEOUT;
        }
        rd[i] = SPDR;
    }

    PORT_REG(cs) |= (uint8_t)(1u << cs->bit);      /* release */
    return AG_OK;
}

/* --------------------------------------------------------------- 1-Wire */
/*
 * Bit-banged, and the timings ARE the protocol -- which is the one place this
 * firmware is permitted to busy-wait.
 *
 * The bus is driven by pulling the line low and RELEASING it rather than
 * driving it high: it is open-drain with a pull-up, and driving high would
 * fight another device that is pulling low.
 */

static void ow_low(const ag_pinref_t *p)
{
    PORT_REG(p) = (uint8_t)(PORT_REG(p) & (uint8_t)~(1u << p->bit));
    DDR_REG(p) |= (uint8_t)(1u << p->bit);
}

static void ow_release(const ag_pinref_t *p)
{
    DDR_REG(p) = (uint8_t)(DDR_REG(p) & (uint8_t)~(1u << p->bit));
}

static uint8_t ow_level(const ag_pinref_t *p)
{
    return (uint8_t)((PIN_REG(p) & (uint8_t)(1u << p->bit)) != 0u);
}

ag_status_t ag_hal_ow_reset(const ag_pinref_t *p, uint8_t *out_presence)
{
    ow_low(p);
    _delay_us(480);
    ow_release(p);
    _delay_us(70);
    /* A device answers by pulling the line low; the line reading HIGH here
     * means nobody is there. */
    *out_presence = (uint8_t)(ow_level(p) == 0u);
    _delay_us(410);
    return AG_OK;
}

static void ow_write_bit(const ag_pinref_t *p, uint8_t bit)
{
    ow_low(p);
    if (bit) { _delay_us(6);  ow_release(p); _delay_us(64); }
    else     { _delay_us(60); ow_release(p); _delay_us(10); }
}

static uint8_t ow_read_bit(const ag_pinref_t *p)
{
    uint8_t v;
    ow_low(p);
    _delay_us(6);
    ow_release(p);
    _delay_us(9);
    v = ow_level(p);
    _delay_us(55);
    return v;
}

ag_status_t ag_hal_ow_write(const ag_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0u; i < len; i++) {
        for (uint8_t b = 0u; b < 8u; b++) {
            ow_write_bit(p, (uint8_t)((buf[i] >> b) & 1u));   /* LSB first */
        }
    }
    return AG_OK;
}

ag_status_t ag_hal_ow_read(const ag_pinref_t *p, uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0u; i < len; i++) {
        uint8_t v = 0u;
        for (uint8_t b = 0u; b < 8u; b++) {
            if (ow_read_bit(p)) { v |= (uint8_t)(1u << b); }
        }
        buf[i] = v;
    }
    return AG_OK;
}
