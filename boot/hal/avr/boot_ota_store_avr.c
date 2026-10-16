/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The external OTA store's bus, for tfw-boot on AVR -- the store section of
 * boot/boot_hal.h. Linked only where the board names an OTA_STORE
 * (mk/boot.mk, BOOT_STORE).
 *
 * Register-level, as the rest of this HAL is: tfw-boot has no pin table, so the
 * selects come from the board profile as a port letter and a bit, pasted into
 * register names here. The store's is D8 (TFW_OTA_STORE_CS_IO, ADR open
 * question 2); the Ethernet shield's WIZnet (D10) and SD socket (D4) share the
 * bus and are held high for as long as it is open.
 */
#include <avr/io.h>

#include "tfw/boot.h"
#include "../../boot_hal.h"

#define IO_PORT(io)     IO_PORT_(io)
#define IO_DDR(io)      IO_DDR_(io)
#define IO_BIT(io)      IO_BIT_(io)
#define IO_PORT_(p, b)  PORT##p
#define IO_DDR_(p, b)   DDR##p
#define IO_BIT_(p, b)   ((uint8_t)(1u << (b)))

#define STORE TFW_OTA_STORE_CS_IO
#define ETH   TFW_ETH_CS_IO
#define SD    TFW_SD_CS_IO

#define SPI_OUT ((uint8_t)((1u << TFW_SPI_BIT_MOSI) | (1u << TFW_SPI_BIT_SCK) | \
                           (1u << TFW_SPI_BIT_SS)))

tfw_status_t tfw_boot_store_open(void)
{
    /* Level before direction: each select is pulled high while still an
     * input, then driven, so no part on the bus sees a low it was not sent.
     * The hardware SS goes high and out too, or a low on it would drop the
     * peripheral out of master mode mid-transfer. */
    IO_PORT(ETH)   |= IO_BIT(ETH);
    IO_DDR(ETH)    |= IO_BIT(ETH);
    IO_PORT(SD)    |= IO_BIT(SD);
    IO_DDR(SD)     |= IO_BIT(SD);
    IO_PORT(STORE) |= IO_BIT(STORE);
    IO_DDR(STORE)  |= IO_BIT(STORE);
    PORTB |= (uint8_t)(1u << TFW_SPI_BIT_SS);
    DDRB  |= SPI_OUT;

    SPCR = (uint8_t)((1u << SPE) | (1u << MSTR));   /* mode 0, F_CPU / 4 */
    SPSR = 0u;
    return TFW_OK;
}

static uint8_t spi(uint8_t b)
{
    SPDR = b;
    while ((SPSR & (uint8_t)(1u << SPIF)) == 0u) {
    }
    return SPDR;
}

tfw_status_t tfw_boot_store_cmd(const uint8_t *cmd, uint8_t clen, uint8_t *in, uint16_t ilen)
{
    IO_PORT(STORE) &= (uint8_t)~IO_BIT(STORE);
    while (clen-- > 0u) {
        (void)spi(*cmd++);
    }
    while (ilen-- > 0u) {
        *in++ = spi(0u);
    }
    IO_PORT(STORE) |= IO_BIT(STORE);
    return TFW_OK;
}

void tfw_boot_store_close(void)
{
    /* Back to the reset state the application's HAL expects: the peripheral
     * off, every pin an input without its pull-up. Direction first, so a
     * select goes from driven high to pulled high to floating, never low. */
    SPCR = 0u;
    DDRB  &= (uint8_t)~SPI_OUT;
    PORTB &= (uint8_t)~(1u << TFW_SPI_BIT_SS);
    IO_DDR(STORE)  &= (uint8_t)~IO_BIT(STORE);
    IO_PORT(STORE) &= (uint8_t)~IO_BIT(STORE);
    IO_DDR(SD)     &= (uint8_t)~IO_BIT(SD);
    IO_PORT(SD)    &= (uint8_t)~IO_BIT(SD);
    IO_DDR(ETH)    &= (uint8_t)~IO_BIT(ETH);
    IO_PORT(ETH)   &= (uint8_t)~IO_BIT(ETH);
}
