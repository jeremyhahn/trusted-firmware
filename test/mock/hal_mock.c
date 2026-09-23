/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "hal_mock.h"
#include "ag/hal.h"

#define RX_CAP 4096u
#define TX_CAP 4096u

static ag_ms_t          now;
static uint8_t          rx[RX_CAP];
static size_t           rx_head, rx_tail;
static uint8_t          tx[TX_CAP];
static size_t           tx_len;
static size_t           tx_capacity;      /* 0 = unlimited */
static ag_reset_cause_t cause;
static uint16_t         free_ram;
static unsigned         resets;

/* A flat pseudo-register file covering the whole low SRAM range, indexed the
 * same way the real HAL indexes real registers. Modelling the addresses rather
 * than the pins means the mock exercises the board descriptor's arithmetic too:
 * a table with a wrong port address fails here, not only on hardware. */
#define EEPROM_SIZE 1024u
static uint8_t  eeprom[EEPROM_SIZE];
static uint16_t adc_value[8];
static uint8_t  adc_ref;
static uint16_t tone_hz;
static uint32_t pulse_in_us;
static uint16_t capture_us[32];
static uint8_t  capture_n;
static uint8_t  bus_resp[64];
static uint8_t  bus_resp_n;
static uint8_t  last_i2c_addr;
static uint8_t  ow_present;

#define REGS 256u
static uint8_t regs[REGS];
#define PINR(port)  regs[(port) % REGS]
#define DDRR(port)  regs[((port) + 1u) % REGS]
#define PORTR(port) regs[((port) + 2u) % REGS]

void ag_mock_reset(void)
{
    now = 0u; rx_head = rx_tail = 0u; tx_len = 0u; tx_capacity = 0u;
    cause = AG_RESET_POWER_ON; free_ram = 1024u; resets = 0u;
    memset(regs, 0, sizeof regs);
    memset(eeprom, 0xFF, sizeof eeprom);   /* erased EEPROM reads as 0xFF */
    memset(adc_value, 0, sizeof adc_value);
    adc_ref = 0u;
    tone_hz = 0u; pulse_in_us = 0u; capture_n = 0u;
    bus_resp_n = 0u; last_i2c_addr = 0xFFu; ow_present = 0u;
}

void ag_mock_set_bus_response(const uint8_t *b, uint8_t n)
{
    if (n > sizeof bus_resp) { n = (uint8_t)sizeof bus_resp; }
    for (uint8_t i = 0u; i < n; i++) { bus_resp[i] = b[i]; }
    bus_resp_n = n;
}

uint8_t ag_mock_last_i2c_addr(void) { return last_i2c_addr; }
uint8_t ag_mock_ow_presence_set(uint8_t present) { ow_present = present; return present; }

uint8_t ag_mock_pwm_duty(uint16_t ocr) { return regs[ocr % REGS]; }

uint8_t ag_mock_pwm_connected(uint16_t tccra, uint8_t com_bit)
{
    return (uint8_t)((regs[tccra % REGS] & (uint8_t)(1u << com_bit)) != 0u);
}

uint16_t ag_mock_tone_hz(void) { return tone_hz; }

void ag_mock_set_pulse_in(uint32_t us) { pulse_in_us = us; }

void ag_mock_set_capture(const uint16_t *us, uint8_t n)
{
    if (n > 32u) { n = 32u; }
    for (uint8_t i = 0u; i < n; i++) { capture_us[i] = us[i]; }
    capture_n = n;
}

void ag_mock_set_adc(uint8_t channel, uint16_t raw)
{
    if (channel < 8u) { adc_value[channel] = raw; }
}

uint8_t *ag_mock_eeprom(void) { return eeprom; }

uint8_t ag_mock_pin_level(uint16_t port, uint8_t bit)
{
    /* An output pin's level is its PORT latch; an input's is the line. */
    if (DDRR(port) & (uint8_t)(1u << bit)) {
        return (uint8_t)((PORTR(port) & (uint8_t)(1u << bit)) != 0u);
    }
    return (uint8_t)((PINR(port) & (uint8_t)(1u << bit)) != 0u);
}

uint8_t ag_mock_pin_is_output(uint16_t port, uint8_t bit)
{
    return (uint8_t)((DDRR(port) & (uint8_t)(1u << bit)) != 0u);
}

uint8_t ag_mock_pin_pullup(uint16_t port, uint8_t bit)
{
    return (uint8_t)(!(DDRR(port) & (uint8_t)(1u << bit)) &&
                     (PORTR(port) & (uint8_t)(1u << bit)));
}

void ag_mock_set_input_level(uint16_t port, uint8_t bit, uint8_t level)
{
    if (level) { PINR(port) |= (uint8_t)(1u << bit); }
    else       { PINR(port) = (uint8_t)(PINR(port) & (uint8_t)~(1u << bit)); }
}

void ag_mock_set_millis(ag_ms_t t) { now = t; }
void ag_mock_advance(ag_ms_t dt)   { now += dt; }

void ag_mock_feed_rx(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n && rx_tail < RX_CAP; i++) {
        rx[rx_tail++] = b[i];
    }
}

size_t ag_mock_drain_tx(uint8_t *out, size_t cap)
{
    size_t n = tx_len < cap ? tx_len : cap;
    memcpy(out, tx, n);
    memmove(tx, tx + n, tx_len - n);
    tx_len -= n;
    return n;
}

size_t ag_mock_tx_pending(void)              { return tx_len; }
void   ag_mock_set_tx_capacity(size_t n)     { tx_capacity = n; }
void   ag_mock_set_reset_cause(ag_reset_cause_t c) { cause = c; }
void   ag_mock_set_free_ram(uint16_t n)      { free_ram = n; }
unsigned ag_mock_reset_count(void)           { return resets; }

/* ------------------------------------------------------- the HAL itself */

ag_status_t      ag_hal_init(void)         { return AG_OK; }
ag_reset_cause_t ag_hal_reset_cause(void)  { return cause; }
ag_ms_t          ag_hal_millis(void)       { return now; }
void             ag_hal_delay_us(uint16_t us) { (void)us; }
uint16_t         ag_hal_free_ram(void)     { return free_ram; }

/* On the device this never returns. Here it counts and returns, because a test
 * that could not continue past a reset could not assert anything about it. */
void ag_hal_reset(void) { resets++; }

ag_status_t ag_hal_uart_init(uint32_t baud) { (void)baud; return AG_OK; }

uint8_t ag_hal_uart_rx(uint8_t *out)
{
    if (rx_head >= rx_tail) {
        return 0u;
    }
    *out = rx[rx_head++];
    return 1u;
}

uint8_t ag_hal_uart_tx_try(uint8_t byte)
{
    /* tx_capacity models a full hardware ring, so the transport's backpressure
     * path is reachable from a test rather than only from a busy real link. */
    if (tx_capacity && tx_len >= tx_capacity) {
        return 0u;
    }
    if (tx_len >= TX_CAP) {
        return 0u;
    }
    tx[tx_len++] = byte;
    return 1u;
}

uint8_t ag_hal_uart_tx_idle(void) { return (uint8_t)(tx_len == 0u); }

ag_status_t ag_hal_gpio_mode(const ag_pinref_t *p, ag_pin_mode_t mode)
{
    uint8_t mask = (uint8_t)(1u << p->bit);

    switch (mode) {
    case AG_PIN_OUTPUT:
        DDRR(p->port) |= mask;
        return AG_OK;
    case AG_PIN_INPUT:
        DDRR(p->port)  = (uint8_t)(DDRR(p->port)  & (uint8_t)~mask);
        PORTR(p->port) = (uint8_t)(PORTR(p->port) & (uint8_t)~mask);
        return AG_OK;
    case AG_PIN_INPUT_PULLUP:
        DDRR(p->port)   = (uint8_t)(DDRR(p->port) & (uint8_t)~mask);
        PORTR(p->port) |= mask;
        return AG_OK;
    default:
        return AG_ERR_RANGE;
    }
}

ag_status_t ag_hal_gpio_write(const ag_pinref_t *p, uint8_t level)
{
    uint8_t mask = (uint8_t)(1u << p->bit);
    if (level) { PORTR(p->port) |= mask; }
    else       { PORTR(p->port) = (uint8_t)(PORTR(p->port) & (uint8_t)~mask); }
    return AG_OK;
}

ag_status_t ag_hal_gpio_read(const ag_pinref_t *p, uint8_t *out)
{
    /* On real silicon PINx is the pin's SYNCHRONISER, not an input-only
     * latch: reading it on an output pin returns the level the port is
     * driving. A mock that returned only the externally-applied level would
     * make read-after-write on an output fail here and work on hardware,
     * which is the worst direction for a mock to be wrong in -- it sends you
     * hunting for a firmware bug that does not exist. */
    if (DDRR(p->port) & (uint8_t)(1u << p->bit)) {
        *out = (uint8_t)((PORTR(p->port) & (uint8_t)(1u << p->bit)) != 0u);
    } else {
        *out = (uint8_t)((PINR(p->port) & (uint8_t)(1u << p->bit)) != 0u);
    }
    return AG_OK;
}

ag_status_t ag_hal_adc_init(void) { return AG_OK; }

ag_status_t ag_hal_adc_set_ref(ag_adc_ref_t ref)
{
    if ((uint8_t)ref > (uint8_t)AG_ADC_REF_INTERNAL) { return AG_ERR_RANGE; }
    adc_ref = (uint8_t)ref;
    return AG_OK;
}

ag_status_t ag_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    if (channel > 7u) { return AG_ERR_BAD_PIN; }
    *out_raw = adc_value[channel];
    return AG_OK;
}

ag_status_t ag_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) { return AG_ERR_RANGE; }
    memcpy(buf, &eeprom[addr], len);
    return AG_OK;
}

ag_status_t ag_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) { return AG_ERR_RANGE; }
    memcpy(&eeprom[addr], buf, len);
    return AG_OK;
}

ag_status_t ag_hal_pwm_write(const ag_pwmref_t *p, uint8_t duty)
{
    regs[p->tccra % REGS] |= (uint8_t)(1u << p->com_bit);
    regs[p->ocr % REGS]    = duty;
    return AG_OK;
}

ag_status_t ag_hal_pwm_stop(const ag_pwmref_t *p)
{
    regs[p->tccra % REGS] = (uint8_t)(regs[p->tccra % REGS] & (uint8_t)~(1u << p->com_bit));
    return AG_OK;
}

ag_status_t ag_hal_tone_start(const ag_pinref_t *p, uint16_t hz)
{
    (void)p;
    if (hz == 0u) { tone_hz = 0u; return AG_OK; }
    /* The AVR implementation refuses below about 490 Hz at its prescaler; the
     * mock models that rather than accepting everything, so the range check is
     * exercised where it can be asserted. */
    if (hz < 490u) { return AG_ERR_RANGE; }
    tone_hz = hz;
    return AG_OK;
}

ag_status_t ag_hal_tone_stop(void) { tone_hz = 0u; return AG_OK; }

ag_status_t ag_hal_pulse_in(const ag_pinref_t *p, uint8_t level,
                            uint32_t timeout_us, uint32_t *out_us)
{
    (void)p; (void)level;
    if (pulse_in_us == 0u)        { return AG_ERR_TIMEOUT; }
    if (pulse_in_us > timeout_us) { return AG_ERR_TIMEOUT; }
    *out_us = pulse_in_us;
    return AG_OK;
}

ag_status_t ag_hal_pulse_capture(const ag_pinref_t *p, uint8_t start_level,
                                 uint16_t *out_us, uint8_t max_edges,
                                 uint32_t timeout_us, uint8_t *out_n)
{
    (void)p; (void)start_level; (void)timeout_us;
    uint8_t n = capture_n < max_edges ? capture_n : max_edges;
    if (n == 0u) { return AG_ERR_TIMEOUT; }
    for (uint8_t i = 0u; i < n; i++) { out_us[i] = capture_us[i]; }
    *out_n = n;
    return AG_OK;
}

ag_status_t ag_hal_i2c_init(uint32_t hz) { (void)hz; return AG_OK; }

ag_status_t ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    (void)wr; (void)wlen;
    last_i2c_addr = addr7;
    if (rlen > bus_resp_n) { return AG_ERR_NACK; }
    for (uint8_t i = 0u; i < rlen; i++) { rd[i] = bus_resp[i]; }
    return AG_OK;
}

ag_status_t ag_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb)
{ (void)mode; (void)clkdiv; (void)lsb; return AG_OK; }

ag_status_t ag_hal_spi_xfer(const ag_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    (void)cs; (void)wr;
    for (uint8_t i = 0u; i < len; i++) { rd[i] = i < bus_resp_n ? bus_resp[i] : 0u; }
    return AG_OK;
}

ag_status_t ag_hal_ow_reset(const ag_pinref_t *p, uint8_t *out_presence)
{ (void)p; *out_presence = ow_present; return AG_OK; }

ag_status_t ag_hal_ow_write(const ag_pinref_t *p, const uint8_t *buf, uint8_t len)
{ (void)p; (void)buf; (void)len; return AG_OK; }

ag_status_t ag_hal_ow_read(const ag_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p;
    for (uint8_t i = 0u; i < len; i++) { buf[i] = i < bus_resp_n ? bus_resp[i] : 0u; }
    return AG_OK;
}
