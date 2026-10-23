/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A serial NOR part on the shield's SPI bus -- the external OTA store of
 * docs/ota/adr-ota-image-format-and-trust.md decision 6 (issue #75).
 *
 * The command set is the one every 25-series part shares: READ 0x03, WREN 0x06,
 * the 4 KB sector erase 0x20, PAGE PROGRAM 0x02, READ STATUS 0x05 and READ
 * JEDEC ID 0x9F. Nothing here names a vendor: the probe reads only whether a
 * JEP106 manufacturer byte answers, so any such part of 512 Kbit or more works,
 * and which one a board carries is ADR open question 2.
 *
 * READ STATUS cannot tell an empty select from a ready part: WIP reads clear
 * on a bus that echoes or floats low, so an erase and a program "succeed"
 * there (#167). The probe is what OTA_BEGIN asks before it erases anything.
 *
 * A busy part answers READ STATUS and ignores everything else, and a failed
 * status poll after a program or erase leaves it busy for the next caller. So
 * every command but READ STATUS and JEDEC ID polls WIP clear first: WREN (and
 * through it PROGRAM and ERASE) and READ. The probe does not: on a bus that
 * floats high WIP never clears, and the probe must answer TFW_ERR_NO_STORE.
 *
 * Each transaction is one tfw_hal_spi_xfer: the HAL holds the select for the
 * whole transfer, so the command, the address and the data share one buffer on
 * the stack. There is no page buffer -- NOR programs a partial page, and a
 * write that crosses a 256-byte page is split here, because the part would
 * otherwise wrap inside the page and overwrite its start.
 *
 * The bus is shared with the WIZnet and the SD socket, so the mode is applied
 * before every transaction, as src/net/w5x00.c does for its own.
 */
#include <string.h>
#include "tfw/board.h"
#include "tfw/hal.h"
#include "../core/ota_store.h"

#define NOR_READ       0x03u
#define NOR_WREN       0x06u
#define NOR_ERASE_4K   0x20u
#define NOR_PROGRAM    0x02u
#define NOR_STATUS     0x05u
#define NOR_JEDEC_ID   0x9Fu   /* answers the JEP106 manufacturer byte first */
#define NOR_WIP        0x01u   /* status bit 0: a program or erase is running */
#define NOR_PAGE       256u
#define NOR_HEAD       4u      /* the opcode and a 24-bit address */

#define NOR_SPI_MODE   0u
#define NOR_SPI_CLKDIV 0u

/* The worst sector erase of the common parts is 400 ms; a page program is a few
 * ms. One deadline above both, on tfw_hal_millis(), never a delay. */
#define NOR_BUSY_MS    1000u

static tfw_pinref_t cs;

static void bus(void)
{
    (void)tfw_hal_spi_init(NOR_SPI_MODE, NOR_SPI_CLKDIV, 0u);
}

/* Level before direction, so the pin never drives low on the way. */
static tfw_status_t deselect(tfw_pin_t pin, tfw_pinref_t *out)
{
    if (tfw_board_pin(pin, out) != TFW_OK) {
        return TFW_ERR_UNSUPPORTED;
    }
    (void)tfw_hal_gpio_write(out, 1u);
    (void)tfw_hal_gpio_mode(out, TFW_PIN_OUTPUT);
    return TFW_OK;
}

static void head(uint8_t *b, uint8_t op, uint32_t addr)
{
    b[0] = op;
    b[1] = (uint8_t)(addr >> 16);
    b[2] = (uint8_t)(addr >> 8);
    b[3] = (uint8_t)addr;
}

static tfw_status_t wait_ready(void)
{
    tfw_ms_t     start = tfw_hal_millis();
    uint8_t      b[2];

    for (;;) {
        tfw_status_t st;

        b[0] = NOR_STATUS;
        b[1] = 0u;
        bus();
        st = tfw_hal_spi_xfer(&cs, b, b, 2u);
        if (st != TFW_OK) {
            return st;
        }
        if ((b[1] & NOR_WIP) == 0u) {
            return TFW_OK;
        }
        if ((tfw_ms_t)(tfw_hal_millis() - start) >= (tfw_ms_t)NOR_BUSY_MS) {
            return TFW_ERR_TIMEOUT;
        }
    }
}

static tfw_status_t write_enable(void)
{
    uint8_t      b  = NOR_WREN;
    tfw_status_t st = wait_ready();

    if (st != TFW_OK) {
        return st;
    }
    bus();
    return tfw_hal_spi_xfer(&cs, &b, &b, 1u);
}

static tfw_status_t program(uint32_t addr, const uint8_t *src, uint8_t len)
{
    uint8_t      b[NOR_HEAD + TFW_STORE_CHUNK];
    tfw_status_t st = write_enable();

    if (st != TFW_OK) {
        return st;
    }
    head(b, NOR_PROGRAM, addr);
    memcpy(&b[NOR_HEAD], src, len);
    bus();
    st = tfw_hal_spi_xfer(&cs, b, b, (uint8_t)(NOR_HEAD + len));
    if (st != TFW_OK) {
        return st;
    }
    return wait_ready();
}

tfw_status_t tfw_store_init(void)
{
    tfw_pinref_t other;

    /* The WIZnet and the SD card answer whenever their selects float low. */
    (void)deselect(TFW_ETH_CS_PIN, &other);
    (void)deselect(TFW_ETH_SD_CS_PIN, &other);
    return deselect(TFW_OTA_STORE_CS_PIN, &cs);
}

tfw_status_t tfw_store_probe(void)
{
    uint8_t      b[2];
    tfw_status_t st;

    b[0] = NOR_JEDEC_ID;
    b[1] = 0u;
    bus();
    st = tfw_hal_spi_xfer(&cs, b, b, 2u);
    if (st != TFW_OK) {
        return st;
    }
    /* JEP106 assigns neither: MISO floating low or high, or echoing the 0x00
     * sent, reads as one of them. */
    if (b[1] == 0x00u || b[1] == 0xFFu) {
        return TFW_ERR_NO_STORE;
    }
    return TFW_OK;
}

tfw_status_t tfw_store_erase(uint32_t addr)
{
    uint8_t      b[NOR_HEAD];
    tfw_status_t st = write_enable();

    if (st != TFW_OK) {
        return st;
    }
    head(b, NOR_ERASE_4K, addr);
    bus();
    st = tfw_hal_spi_xfer(&cs, b, b, NOR_HEAD);
    if (st != TFW_OK) {
        return st;
    }
    return wait_ready();
}

tfw_status_t tfw_store_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t      b[NOR_HEAD + TFW_STORE_CHUNK];
    tfw_status_t st;

    if (len > TFW_STORE_CHUNK) {
        return TFW_ERR_RANGE;
    }
    st = wait_ready();
    if (st != TFW_OK) {
        return st;
    }
    head(b, NOR_READ, addr);
    memset(&b[NOR_HEAD], 0, len);
    bus();
    st = tfw_hal_spi_xfer(&cs, b, b, (uint8_t)(NOR_HEAD + len));
    if (st != TFW_OK) {
        return st;
    }
    memcpy(buf, &b[NOR_HEAD], len);
    return TFW_OK;
}

tfw_status_t tfw_store_write(uint32_t addr, const uint8_t *src, uint8_t len)
{
    if (len > TFW_STORE_CHUNK) {
        return TFW_ERR_RANGE;
    }
    while (len > 0u) {
        uint16_t     room = (uint16_t)(NOR_PAGE - (uint16_t)(addr & (NOR_PAGE - 1u)));
        uint8_t      n    = (room < len) ? (uint8_t)room : len;
        tfw_status_t st   = program(addr, src, n);

        if (st != TFW_OK) {
            return st;
        }
        addr += n;
        src  += n;
        len  -= n;
    }
    return TFW_OK;
}
