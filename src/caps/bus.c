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
#include "ag/hal.h"

/* The ATECC608A's 7-bit address. Refused whether or not one is fitted: a build
 * that allowed it when absent would allow it the moment one was added. */
#ifndef AG_I2C_RESERVED_ADDR
#define AG_I2C_RESERVED_ADDR 0x60u
#endif

/* The largest transfer a reply can carry. */
#define AG_BUS_MAX ((uint8_t)(AG_MAX_PAYLOAD - 1u))

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
