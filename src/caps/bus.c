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
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"

/* The largest transfer a reply can carry. */
#define TFW_BUS_MAX ((uint8_t)(TFW_MAX_PAYLOAD - 1u))

/*
 * Applies boot defaults to whichever of I2C/SPI this build has.
 *
 * Both calls run unconditionally here, rather than behind TFW_FEATURE_I2C /
 * TFW_FEATURE_SPI checks, because those two macros cannot be tested with an
 * #if in this file -- see the header comment above and mk/avr.mk's BUS_SRCS,
 * which links this translation unit as ONE unit with hal_bus.c whenever any
 * of I2C, SPI or 1-Wire is on. Every board that gates one of the three today
 * gates all three together (the ATtiny85; see mk/avr.mk's BUS_OFF), so
 * calling both is exactly "compiled-in peripherals" for every build that
 * exists. A board that someday wants I2C without SPI would need this split
 * the same way hal_bus.c's own comment says 1-Wire would.
 *
 * tfw_crypto_init() (main.c) runs AFTER this, not before: the ATECC608A
 * provider calls tfw_hal_i2c_init() itself at its own required rate, and that
 * call must be the one left standing on an element build. Running this first
 * makes the generic default the one that loses a conflict rather than the one
 * that wins it.
 *
 * tfw_i2c_cmd_config (below) is the same conflict at RUNTIME rather than boot:
 * see its own comment for why an element build refuses it outright.
 */
void tfw_bus_init(void)
{
    (void)tfw_hal_i2c_init(TFW_I2C_DEFAULT_HZ);
    (void)tfw_hal_spi_init(TFW_SPI_DEFAULT_MODE, TFW_SPI_DEFAULT_CLKDIV,
                          TFW_SPI_DEFAULT_LSB_FIRST);
}

tfw_status_t tfw_i2c_cmd_xfer(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t        addr = tfw_read_u8(r);
    uint8_t        rlen = tfw_read_u8(r);
    uint8_t        wlen = tfw_reader_remaining(r);
    const uint8_t *wr;
    uint8_t        rd[TFW_BUS_MAX];
    tfw_status_t    st;

    if (addr > 0x7Fu) {
        return TFW_ERR_RANGE;
    }
    if (addr == TFW_I2C_RESERVED_ADDR) {
        return TFW_ERR_PROTECTED_REGION;
    }
    if (rlen > TFW_BUS_MAX) {
        return TFW_ERR_RANGE;
    }
    wr = wlen ? tfw_read_blob(r, wlen) : (const uint8_t *)0;
    if (wlen && !wr) {
        return TFW_ERR_BAD_ARGC;
    }
    st = tfw_hal_i2c_xfer(addr, wr, wlen, rd, rlen);
    if (st != TFW_OK) {
        return st;
    }
    if (rlen) {
        tfw_write_blob(w, rd, rlen);
    }
    return TFW_OK;
}

/* [hz:4] -- the clock tfw_hal_i2c_init() should now run the TWI peripheral at.
 * Bounded to TFW_I2C_HZ_MIN..TFW_I2C_HZ_MAX (config.h): below the floor divides
 * by a number the register cannot express meaningfully, above the ceiling is
 * past what this family's TWI peripheral is rated for.
 *
 * REFUSED OUTRIGHT ON AN ELEMENT BUILD, unconditionally, before either bound
 * is even checked -- the same shape as the element's address being refused
 * from I2C_XFER whether or not an element is fitted. tfw_atecc_wake() needs
 * this exact peripheral at or below 100 kHz for its wake pulse (see its own
 * comment) and reasserts that rate before every wake regardless of what this
 * command last set -- so a rate accepted here would be silently undone by
 * the next crypto operation, on a bus a generic sensor is also expected to
 * share. Pretending to honour it would be worse than refusing: a host that
 * asked for 250 kHz and got TFW_OK would trust the number that followed, and
 * every reading after the next attestation would be timed on a bus running
 * at 100 kHz instead. */
tfw_status_t tfw_i2c_cmd_config(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t hz = tfw_read_u32(r);

    (void)w;
    if (tfw_crypto_tier() == TFW_CRYPTO_SECURE_ELEMENT) {
        return TFW_ERR_PROTECTED_REGION;
    }
    if (hz < TFW_I2C_HZ_MIN || hz > TFW_I2C_HZ_MAX) {
        return TFW_ERR_RANGE;
    }
    return tfw_hal_i2c_init(hz);
}

tfw_status_t tfw_spi_cmd_xfer(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t       cs   = tfw_read_u8(r);
    uint8_t        len  = tfw_reader_remaining(r);
    const uint8_t *wr;
    uint8_t        rd[TFW_BUS_MAX];
    tfw_pinref_t    ref;
    tfw_status_t    st;

    if (len == 0u || len > TFW_BUS_MAX) {
        return TFW_ERR_RANGE;
    }
    /* A chip-select that is reserved -- a TPM's, or the UART's pins -- is
     * refused for the same reason the element's I2C address is. */
    if (tfw_board_pin_flags(cs) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_PROTECTED_REGION;
    }
    st = tfw_board_pin(cs, &ref);
    if (st != TFW_OK) {
        return st;
    }
    wr = tfw_read_blob(r, len);
    if (!wr) {
        return TFW_ERR_BAD_ARGC;
    }
    st = tfw_hal_spi_xfer(&ref, wr, rd, len);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_blob(w, rd, len);
    return TFW_OK;
}

/* [mode:1][clkdiv:1][lsb_first:1] -- the three parameters tfw_hal_spi_init()
 * takes, all of which the host previously had no way to choose at all. mode
 * is CPOL:CPHA, 0-3; clkdiv is SPCR's raw SPR1:SPR0 field, 0-3; lsb_first is
 * a boolean. All three are checked here, once, rather than trusted to the
 * HAL: a mode or clkdiv past 3 would still fit in the register (the write
 * masks to two bits), silently selecting a DIFFERENT mode or divider than the
 * one asked for -- which is the same wrong-config-not-an-error failure this
 * command exists to close. */
tfw_status_t tfw_spi_cmd_config(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t mode      = tfw_read_u8(r);
    uint8_t clkdiv    = tfw_read_u8(r);
    uint8_t lsb_first = tfw_read_u8(r);

    (void)w;
    if (mode > 3u || clkdiv > 3u || lsb_first > 1u) {
        return TFW_ERR_RANGE;
    }
    return tfw_hal_spi_init(mode, clkdiv, lsb_first);
}

tfw_status_t tfw_ow_cmd_reset(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin = tfw_read_u8(r);
    tfw_pinref_t ref;
    tfw_status_t st;
    uint8_t     presence = 0u;

    if (tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_hal_ow_reset(&ref, &presence);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_u8(w, presence);
    return TFW_OK;
}

tfw_status_t tfw_ow_cmd_write(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t       pin = tfw_read_u8(r);
    uint8_t        len = tfw_reader_remaining(r);
    const uint8_t *buf;
    tfw_pinref_t    ref;
    tfw_status_t    st;

    (void)w;
    if (len == 0u || len > TFW_BUS_MAX) {
        return TFW_ERR_RANGE;
    }
    if (tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    buf = tfw_read_blob(r, len);
    if (!buf) {
        return TFW_ERR_BAD_ARGC;
    }
    return tfw_hal_ow_write(&ref, buf, len);
}

tfw_status_t tfw_ow_cmd_read(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin = tfw_read_u8(r);
    uint8_t     len = tfw_read_u8(r);
    uint8_t     buf[TFW_BUS_MAX];
    tfw_pinref_t ref;
    tfw_status_t st;

    if (len == 0u || len > TFW_BUS_MAX) {
        return TFW_ERR_RANGE;
    }
    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_hal_ow_read(&ref, buf, len);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_blob(w, buf, len);
    return TFW_OK;
}
