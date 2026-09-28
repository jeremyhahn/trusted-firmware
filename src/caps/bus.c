/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * I2C, SPI and 1-Wire as generic primitives -- with one carve-out.
 *
 * THE SECURE ELEMENT IS NOT REACHABLE THROUGH THESE.
 *
 * A generic I2C command that could address any device on the bus could address
 * the ATECC608A, which sits on that same bus. That would let a host talk to the
 * element directly, around the firmware's crypto layer: reading its
 * configuration, exhausting its monotonic counters, or issuing commands the
 * attestation path assumes only it issues. The device's own root of trust would
 * be an ordinary peripheral.
 *
 * So the element's address is refused here, and reported as reserved in CAPS so
 * a host learns about it by discovery rather than by failure. The same applies
 * to a TPM's chip-select on SPI.
 *
 * This is the second place where "expose the hardware generically" and "be a
 * trusted device" conflict; the EEPROM region is the first. In both, the answer
 * is a narrow, named, discoverable exclusion rather than a weaker primitive.
 */

#include "bus.h"
#include "ag/board.h"
#include "ag/config.h"
#include "ag/crypto.h"
#include "ag/hal.h"

/* The largest transfer a reply can carry. */
#define AG_BUS_MAX ((uint8_t)(AG_MAX_PAYLOAD - 1u))

/*
 * Applies boot defaults to whichever of I2C/SPI this build has.
 *
 * Both calls run unconditionally here, rather than behind AG_FEATURE_I2C /
 * AG_FEATURE_SPI checks, because those two macros cannot be tested with an
 * #if in this file -- see the header comment above and mk/avr.mk's BUS_SRCS,
 * which links this translation unit as ONE unit with hal_bus.c whenever any
 * of I2C, SPI or 1-Wire is on. Every board that gates one of the three today
 * gates all three together (the ATtiny85; see mk/avr.mk's BUS_OFF), so
 * calling both is exactly "compiled-in peripherals" for every build that
 * exists. A board that someday wants I2C without SPI would need this split
 * the same way hal_bus.c's own comment says 1-Wire would.
 *
 * ag_crypto_init() (main.c) runs AFTER this, not before: the ATECC608A
 * provider calls ag_hal_i2c_init() itself at its own required rate, and that
 * call must be the one left standing on an element build. Running this first
 * makes the generic default the one that loses a conflict rather than the one
 * that wins it.
 *
 * ag_i2c_cmd_config (below) is the same conflict at RUNTIME rather than boot:
 * see its own comment for why an element build refuses it outright.
 */
void ag_bus_init(void)
{
    (void)ag_hal_i2c_init(AG_I2C_DEFAULT_HZ);
    (void)ag_hal_spi_init(AG_SPI_DEFAULT_MODE, AG_SPI_DEFAULT_CLKDIV,
                          AG_SPI_DEFAULT_LSB_FIRST);
}

ag_status_t ag_i2c_cmd_xfer(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t        addr = ag_read_u8(r);
    uint8_t        rlen = ag_read_u8(r);
    uint8_t        wlen = ag_reader_remaining(r);
    const uint8_t *wr;
    uint8_t        rd[AG_BUS_MAX];
    ag_status_t    st;

    if (addr > 0x7Fu) {
        return AG_ERR_RANGE;
    }
    if (addr == AG_I2C_RESERVED_ADDR) {
        return AG_ERR_PROTECTED_REGION;
    }
    if (rlen > AG_BUS_MAX) {
        return AG_ERR_RANGE;
    }
    wr = wlen ? ag_read_blob(r, wlen) : (const uint8_t *)0;
    if (wlen && !wr) {
        return AG_ERR_BAD_ARGC;
    }
    st = ag_hal_i2c_xfer(addr, wr, wlen, rd, rlen);
    if (st != AG_OK) {
        return st;
    }
    if (rlen) {
        ag_write_blob(w, rd, rlen);
    }
    return AG_OK;
}

/* [hz:4] -- the clock ag_hal_i2c_init() should now run the TWI peripheral at.
 * Bounded to AG_I2C_HZ_MIN..AG_I2C_HZ_MAX (config.h): below the floor divides
 * by a number the register cannot express meaningfully, above the ceiling is
 * past what this family's TWI peripheral is rated for.
 *
 * REFUSED OUTRIGHT ON AN ELEMENT BUILD, unconditionally, before either bound
 * is even checked -- the same shape as the element's address being refused
 * from I2C_XFER whether or not an element is fitted. ag_atecc_wake() needs
 * this exact peripheral at or below 100 kHz for its wake pulse (see its own
 * comment) and reasserts that rate before every wake regardless of what this
 * command last set -- so a rate accepted here would be silently undone by
 * the next crypto operation, on a bus a generic sensor is also expected to
 * share. Pretending to honour it would be worse than refusing: a host that
 * asked for 250 kHz and got AG_OK would trust the number that followed, and
 * every reading after the next attestation would be timed on a bus running
 * at 100 kHz instead. */
ag_status_t ag_i2c_cmd_config(ag_reader_t *r, ag_writer_t *w)
{
    uint32_t hz = ag_read_u32(r);

    (void)w;
    if (ag_crypto_tier() == AG_CRYPTO_SECURE_ELEMENT) {
        return AG_ERR_PROTECTED_REGION;
    }
    if (hz < AG_I2C_HZ_MIN || hz > AG_I2C_HZ_MAX) {
        return AG_ERR_RANGE;
    }
    return ag_hal_i2c_init(hz);
}

ag_status_t ag_spi_cmd_xfer(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t       cs   = ag_read_u8(r);
    uint8_t        len  = ag_reader_remaining(r);
    const uint8_t *wr;
    uint8_t        rd[AG_BUS_MAX];
    ag_pinref_t    ref;
    ag_status_t    st;

    if (len == 0u || len > AG_BUS_MAX) {
        return AG_ERR_RANGE;
    }
    /* A chip-select that is reserved -- a TPM's, or the UART's pins -- is
     * refused for the same reason the element's I2C address is. */
    if (ag_board_pin_flags(cs) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_PROTECTED_REGION;
    }
    st = ag_board_pin(cs, &ref);
    if (st != AG_OK) {
        return st;
    }
    wr = ag_read_blob(r, len);
    if (!wr) {
        return AG_ERR_BAD_ARGC;
    }
    st = ag_hal_spi_xfer(&ref, wr, rd, len);
    if (st != AG_OK) {
        return st;
    }
    ag_write_blob(w, rd, len);
    return AG_OK;
}

/* [mode:1][clkdiv:1][lsb_first:1] -- the three parameters ag_hal_spi_init()
 * takes, all of which the host previously had no way to choose at all. mode
 * is CPOL:CPHA, 0-3; clkdiv is SPCR's raw SPR1:SPR0 field, 0-3; lsb_first is
 * a boolean. All three are checked here, once, rather than trusted to the
 * HAL: a mode or clkdiv past 3 would still fit in the register (the write
 * masks to two bits), silently selecting a DIFFERENT mode or divider than the
 * one asked for -- which is the same wrong-config-not-an-error failure this
 * command exists to close. */
ag_status_t ag_spi_cmd_config(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t mode      = ag_read_u8(r);
    uint8_t clkdiv    = ag_read_u8(r);
    uint8_t lsb_first = ag_read_u8(r);

    (void)w;
    if (mode > 3u || clkdiv > 3u || lsb_first > 1u) {
        return AG_ERR_RANGE;
    }
    return ag_hal_spi_init(mode, clkdiv, lsb_first);
}

ag_status_t ag_ow_cmd_reset(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin = ag_read_u8(r);
    ag_pinref_t ref;
    ag_status_t st;
    uint8_t     presence = 0u;

    if (ag_board_pin_flags(pin) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    st = ag_hal_ow_reset(&ref, &presence);
    if (st != AG_OK) {
        return st;
    }
    ag_write_u8(w, presence);
    return AG_OK;
}

ag_status_t ag_ow_cmd_write(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t       pin = ag_read_u8(r);
    uint8_t        len = ag_reader_remaining(r);
    const uint8_t *buf;
    ag_pinref_t    ref;
    ag_status_t    st;

    (void)w;
    if (len == 0u || len > AG_BUS_MAX) {
        return AG_ERR_RANGE;
    }
    if (ag_board_pin_flags(pin) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    buf = ag_read_blob(r, len);
    if (!buf) {
        return AG_ERR_BAD_ARGC;
    }
    return ag_hal_ow_write(&ref, buf, len);
}

ag_status_t ag_ow_cmd_read(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin = ag_read_u8(r);
    uint8_t     len = ag_read_u8(r);
    uint8_t     buf[AG_BUS_MAX];
    ag_pinref_t ref;
    ag_status_t st;

    if (len == 0u || len > AG_BUS_MAX) {
        return AG_ERR_RANGE;
    }
    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    st = ag_hal_ow_read(&ref, buf, len);
    if (st != AG_OK) {
        return st;
    }
    ag_write_blob(w, buf, len);
    return AG_OK;
}
