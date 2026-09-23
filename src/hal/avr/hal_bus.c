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
    TWSR = 0u;                                   /* prescaler 1 */
    TWBR = (uint8_t)(((F_CPU / hz) - 16UL) / 2UL);
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

ag_status_t ag_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    DDRB |= (uint8_t)(_BV(3) | _BV(5) | _BV(2));   /* MOSI, SCK, SS as outputs */
    SPCR = (uint8_t)(_BV(SPE) | _BV(MSTR)
                     | (uint8_t)((mode & 0x03u) << CPHA)
                     | (uint8_t)(clkdiv & 0x03u)
                     | (uint8_t)(lsb_first ? _BV(DORD) : 0u));
    return AG_OK;
}

ag_status_t ag_hal_spi_xfer(const ag_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    uint8_t i;

    DDR_REG(cs)  |= (uint8_t)(1u << cs->bit);
    PORT_REG(cs)  = (uint8_t)(PORT_REG(cs) & (uint8_t)~(1u << cs->bit));  /* assert */

    for (i = 0u; i < len; i++) {
        SPDR = wr[i];
        while (!(SPSR & _BV(SPIF))) { }
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
