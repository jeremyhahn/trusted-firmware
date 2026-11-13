/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's data (soc_tables.h). No register is read or written here:
 * the file builds and runs on the host as it does on the part.
 */

#include <stdint.h>

#include "tfw/status.h"
#include "soc_tables.h"

typedef struct {
    uint32_t baud;
    uint32_t reg;
} baud_row_t;

/* nrf54l15_types.h, UARTE_BAUDRATE_BAUDRATE_Baud*: the register values for a
 * 16 MHz UARTE. */
static const baud_row_t baud_rows[] = {
    {    1200u, 0x0004F000uL },
    {    2400u, 0x0009D000uL },
    {    4800u, 0x0013B000uL },
    {    9600u, 0x00275000uL },
    {   14400u, 0x003AF000uL },
    {   19200u, 0x004EA000uL },
    {   28800u, 0x0075C000uL },
    {   31250u, 0x00800000uL },
    {   38400u, 0x009D0000uL },
    {   56000u, 0x00E50000uL },
    {   57600u, 0x00EB0000uL },
    {   76800u, 0x013A9000uL },
    {  115200u, 0x01D60000uL },
    {  230400u, 0x03B00000uL },
    {  250000u, 0x04000000uL },
    {  460800u, 0x07400000uL },
    {  921600u, 0x0F000000uL },
    { 1000000u, 0x10000000uL },
};

tfw_status_t tfw_nrf54l_uarte_baud(uint32_t baud, uint32_t *reg)
{
    for (uint32_t i = 0u; i < sizeof baud_rows / sizeof baud_rows[0]; i++) {
        if (baud_rows[i].baud == baud) {
            *reg = baud_rows[i].reg;
            return TFW_OK;
        }
    }
    return TFW_ERR_RANGE;
}

/* The pins per port: one more than nrf54l15_application_peripherals.h's
 * P0_PIN_NUM_MAX, P1_PIN_NUM_MAX and P2_PIN_NUM_MAX (6, 16 and 10). */
static const uint8_t pins_per_port[] = { 7u, 17u, 11u };

uint8_t tfw_nrf54l_pin_exists(uint8_t port, uint8_t bit)
{
    return (uint8_t)(port < sizeof pins_per_port && bit < pins_per_port[port]);
}

uint32_t tfw_nrf54l_psel(uint8_t port, uint8_t bit)
{
    return ((uint32_t)(port & 0x7u) << 5) | (uint32_t)(bit & 0x1Fu);
}

uint8_t tfw_nrf54l_is_rev1(uint32_t part, uint32_t revision)
{
    return (uint8_t)(part == 0x1CuL && revision == 0x01uL);
}

uint8_t tfw_nrf54l_rram_span(uint32_t floor, uint32_t addr, uint32_t len)
{
    /* Each comparison is against what is left above addr, so no sum wraps. */
    if (addr < floor || addr > TFW_NRF54L_RRAM_SIZE) {
        return 0u;
    }
    return (uint8_t)(len <= TFW_NRF54L_RRAM_SIZE - addr);
}

uint32_t tfw_nrf54l_rram_unit(uint32_t addr, uint32_t left)
{
    return ((addr & 3uL) == 0u && left >= 4u) ? 4u : 1u;
}

/* The SAADC's analog inputs are fixed pins: AIN0-AIN7 are P1.4, P1.5, P1.6,
 * P1.7, P1.11, P1.12, P1.13 and P1.14 (nrfx soc/nrfx_soc_defines.h,
 * SAADC_EXTERNAL_AIN_PSELS for NRF54L15_XXAA). AIN0 and AIN1 are the
 * console UARTE's pins and AIN6 and AIN7 the DK's BUTTON0 and LED3 (Zephyr's
 * nrf54l15dk_common.dtsi), so the board's channels are AIN2-AIN5. */
static const uint8_t saadc_bits[TFW_NRF54L_SAADC_CHANNELS] = { 6u, 7u, 11u, 12u };

uint8_t tfw_nrf54l_saadc_pin(uint8_t channel, uint8_t *port, uint8_t *bit)
{
    if (channel >= TFW_NRF54L_SAADC_CHANNELS) {
        return 0u;
    }
    *port = 1u;
    *bit  = saadc_bits[channel];
    return 1u;
}

uint32_t tfw_nrf54l_saadc_pselp(uint8_t port, uint8_t bit)
{
    return (1uL << 30) | ((uint32_t)(port & 0xFu) << 8) | (uint32_t)(bit & 0x1Fu);
}

uint16_t tfw_nrf54l_saadc_raw(int16_t sample)
{
    if (sample < 0) {
        return 0u;
    }
    return (sample > 4095) ? 4095u : (uint16_t)sample;
}
