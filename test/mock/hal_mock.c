/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "handoff.h"

#define RX_CAP 4096u
#define TX_CAP 4096u

static tfw_ms_t          now;
static uint8_t          rx[RX_CAP];
static size_t           rx_head, rx_tail;
static uint8_t          tx[TX_CAP];
static size_t           tx_len;
static size_t           tx_capacity;      /* 0 = unlimited */
static tfw_reset_cause_t cause;
static uint32_t         free_ram;
static unsigned         resets;
static unsigned         wdt_arms, wdt_pets;
static uint8_t          irq_on;

#define EEPROM_SIZE 1024u
static uint8_t  eeprom[EEPROM_SIZE];
static size_t   ee_written;              /* bytes tfw_hal_eeprom_write was handed */
static uint32_t ee_lo, ee_hi;            /* the span [ee_lo, ee_hi) they covered */
#define EE_NOT_STUCK 0xFFFFFFFFuL
static uint32_t ee_stuck = EE_NOT_STUCK; /* a cell whose writes do not stick */
static uint8_t  handoff[TFW_BOOT_HANDOFF_LEN];
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
static uint32_t last_i2c_hz;
static uint8_t  last_spi_mode, last_spi_clkdiv, last_spi_lsb;

/* Pin and PWM state, kept per OPAQUE KEY and not in a register file.
 *
 * The keys are whatever the board descriptor put in the pin and PWM references
 * (hal_types.h): the mock tells one from another and nothing else. It does not
 * add offsets to them, so there is no PINx/DDRx/PORTx overlay here to agree with
 * the AVR's, and a test that passes against this mock has not leaned on one.
 * Each port keeps the three things a pin has -- what the line is doing, which
 * way it points, what the output latch holds -- as bit masks. Thirty-two bits
 * wide, because the host's pin coordinate is wider than the AVR's (hal_types.h)
 * and a bit the board table can name must reach a state that can hold it. */
typedef struct {
    uint16_t key;
    uint8_t  used;
    uint32_t line;   /* level driven onto an input by the test */
    uint32_t dir;    /* 1 = output */
    uint32_t latch;  /* output level, or the pull-up on an input */
} mock_port_t;

static uint32_t pin_mask(uint8_t bit) { return (uint32_t)1u << bit; }

typedef struct {
    uint16_t key;
    uint8_t  used;
    uint8_t  val;
} mock_word_t;

#define MOCK_SLOTS 32u
static mock_port_t ports[MOCK_SLOTS];
static mock_word_t pwm_ctl[MOCK_SLOTS];   /* connected bits, per control key */
static mock_word_t pwm_duty[MOCK_SLOTS];  /* duty, per duty key */

static void slot_overflow(void)
{
    fputs("hal_mock: more distinct pin/PWM keys than the mock tracks\n", stderr);
    abort();
}

static mock_port_t *port_find(uint16_t key, uint8_t create)
{
    static mock_port_t none;
    uint8_t i;

    for (i = 0u; i < MOCK_SLOTS; i++) {
        if (ports[i].used && ports[i].key == key) { return &ports[i]; }
    }
    if (!create) {
        memset(&none, 0, sizeof none);
        return &none;
    }
    for (i = 0u; i < MOCK_SLOTS; i++) {
        if (!ports[i].used) {
            ports[i].used = 1u;
            ports[i].key  = key;
            return &ports[i];
        }
    }
    slot_overflow();
    return &ports[0];
}

static mock_word_t *word_find(mock_word_t *tab, uint16_t key, uint8_t create)
{
    static mock_word_t none;
    uint8_t i;

    for (i = 0u; i < MOCK_SLOTS; i++) {
        if (tab[i].used && tab[i].key == key) { return &tab[i]; }
    }
    if (!create) {
        none.val = 0u;
        return &none;
    }
    for (i = 0u; i < MOCK_SLOTS; i++) {
        if (!tab[i].used) {
            tab[i].used = 1u;
            tab[i].key  = key;
            return &tab[i];
        }
    }
    slot_overflow();
    return &tab[0];
}

void tfw_mock_reset(void)
{
    now = 0u; rx_head = rx_tail = 0u; tx_len = 0u; tx_capacity = 0u;
    cause = TFW_RESET_POWER_ON; free_ram = 1024u; resets = 0u; irq_on = 0u;
    wdt_arms = 0u; wdt_pets = 0u;
    memset(ports, 0, sizeof ports);
    memset(pwm_ctl, 0, sizeof pwm_ctl);
    memset(pwm_duty, 0, sizeof pwm_duty);
    memset(eeprom, 0xFF, sizeof eeprom);   /* erased EEPROM reads as 0xFF */
    ee_written = 0u; ee_lo = ee_hi = 0u; ee_stuck = EE_NOT_STUCK;
    memset(handoff, 0, sizeof handoff);    /* zeroed: reads as "no handoff" */
    memset(adc_value, 0, sizeof adc_value);
    adc_ref = 0u;
    tone_hz = 0u; pulse_in_us = 0u; capture_n = 0u;
    bus_resp_n = 0u; last_i2c_addr = 0xFFu; ow_present = 0u;
    last_i2c_hz = 0xFFFFFFFFuL; last_spi_mode = last_spi_clkdiv = last_spi_lsb = 0xFFu;
}

void tfw_mock_set_bus_response(const uint8_t *b, uint8_t n)
{
    if (n > sizeof bus_resp) { n = (uint8_t)sizeof bus_resp; }
    for (uint8_t i = 0u; i < n; i++) { bus_resp[i] = b[i]; }
    bus_resp_n = n;
}

uint8_t tfw_mock_last_i2c_addr(void) { return last_i2c_addr; }
uint8_t tfw_mock_ow_presence_set(uint8_t present) { ow_present = present; return present; }

uint32_t tfw_mock_last_i2c_hz(void)      { return last_i2c_hz; }
uint8_t  tfw_mock_last_spi_mode(void)    { return last_spi_mode; }
uint8_t  tfw_mock_last_spi_clkdiv(void)  { return last_spi_clkdiv; }
uint8_t  tfw_mock_last_spi_lsb(void)     { return last_spi_lsb; }

uint8_t tfw_mock_pwm_duty(uint16_t duty_key)
{
    return word_find(pwm_duty, duty_key, 0u)->val;
}

uint8_t tfw_mock_pwm_connected(uint16_t ctl_key, uint8_t bit)
{
    return (uint8_t)((word_find(pwm_ctl, ctl_key, 0u)->val & (uint8_t)(1u << bit)) != 0u);
}

uint16_t tfw_mock_tone_hz(void) { return tone_hz; }

void tfw_mock_set_pulse_in(uint32_t us) { pulse_in_us = us; }

void tfw_mock_set_capture(const uint16_t *us, uint8_t n)
{
    if (n > 32u) { n = 32u; }
    for (uint8_t i = 0u; i < n; i++) { capture_us[i] = us[i]; }
    capture_n = n;
}

void tfw_mock_set_adc(uint8_t channel, uint16_t raw)
{
    if (channel < 8u) { adc_value[channel] = raw; }
}

uint8_t *tfw_mock_eeprom(void) { return eeprom; }
size_t   tfw_mock_eeprom_size(void) { return sizeof eeprom; }
void     tfw_mock_eeprom_stuck_at(uint16_t addr) { ee_stuck = addr; }

size_t tfw_mock_eeprom_written(uint16_t *lo, uint16_t *hi)
{
    *lo = (uint16_t)ee_lo;
    *hi = (uint16_t)ee_hi;
    return ee_written;
}
uint8_t *tfw_mock_handoff(void) { return handoff; }

uint8_t *tfw_hal_handoff_block(void) { return handoff; }

uint8_t tfw_mock_pin_level(uint16_t port, uint8_t bit)
{
    const mock_port_t *s = port_find(port, 0u);
    uint32_t mask = pin_mask(bit);

    /* An output pin's level is its latch; an input's is the line. */
    if (s->dir & mask) {
        return (uint8_t)((s->latch & mask) != 0u);
    }
    return (uint8_t)((s->line & mask) != 0u);
}

uint8_t tfw_mock_pin_is_output(uint16_t port, uint8_t bit)
{
    return (uint8_t)((port_find(port, 0u)->dir & pin_mask(bit)) != 0u);
}

uint8_t tfw_mock_pin_pullup(uint16_t port, uint8_t bit)
{
    const mock_port_t *s = port_find(port, 0u);
    uint32_t mask = pin_mask(bit);

    return (uint8_t)(!(s->dir & mask) && (s->latch & mask));
}

void tfw_mock_set_input_level(uint16_t port, uint8_t bit, uint8_t level)
{
    mock_port_t *s = port_find(port, 1u);
    uint32_t mask = pin_mask(bit);

    if (level) { s->line |= mask; }
    else       { s->line = s->line & ~mask; }
}

uint8_t tfw_mock_irq_enabled(void) { return irq_on; }

void tfw_mock_set_millis(tfw_ms_t t) { now = t; }
void tfw_mock_advance(tfw_ms_t dt)   { now += dt; }

void tfw_mock_feed_rx(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n && rx_tail < RX_CAP; i++) {
        rx[rx_tail++] = b[i];
    }
}

size_t tfw_mock_drain_tx(uint8_t *out, size_t cap)
{
    size_t n = tx_len < cap ? tx_len : cap;
    memcpy(out, tx, n);
    memmove(tx, tx + n, tx_len - n);
    tx_len -= n;
    return n;
}

size_t tfw_mock_tx_pending(void)              { return tx_len; }
void   tfw_mock_set_tx_capacity(size_t n)     { tx_capacity = n; }
void   tfw_mock_set_reset_cause(tfw_reset_cause_t c) { cause = c; }
void   tfw_mock_set_free_ram(uint32_t n)      { free_ram = n; }
unsigned tfw_mock_reset_count(void)           { return resets; }

/* ------------------------------------------------------- the HAL itself */

tfw_status_t      tfw_hal_init(void)         { return TFW_OK; }

/*
 * Mirrors src/hal/avr/hal_sys.c's own preference: a valid handoff (issue #18)
 * wins over whatever the test set directly with tfw_mock_set_reset_cause,
 * exactly as tfw-boot's observation wins over the application's own .init3 on
 * real hardware. The handoff already carries the abstract cause, so there is
 * nothing to decode here.
 */
tfw_reset_cause_t tfw_hal_reset_cause(void)
{
    uint8_t handed_off;

    if (tfw_boot_handoff_reset_cause(&handed_off)) {
        return (tfw_reset_cause_t)handed_off;
    }
    return cause;
}
tfw_ms_t          tfw_hal_millis(void)       { return now; }
__attribute__((weak))
void             tfw_hal_delay_us(uint16_t us) { (void)us; }
uint32_t         tfw_hal_free_ram(void)     { return free_ram; }

/* On the device this never returns. Here it counts and returns, because a test
 * that could not continue past a reset could not assert anything about it. */
void tfw_hal_reset(void) { resets++; }

void tfw_hal_watchdog_arm(void) { wdt_arms++; }
void tfw_hal_watchdog_pet(void) { wdt_pets++; }
unsigned tfw_mock_watchdog_arms(void) { return wdt_arms; }
unsigned tfw_mock_watchdog_pets(void) { return wdt_pets; }

void tfw_hal_irq_enable(void) { irq_on = 1u; }

tfw_status_t tfw_hal_uart_init(uint32_t baud) { (void)baud; return TFW_OK; }

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    if (rx_head >= rx_tail) {
        return 0u;
    }
    *out = rx[rx_head++];
    return 1u;
}

uint8_t tfw_hal_uart_tx_try(uint8_t byte)
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

uint8_t tfw_hal_uart_tx_idle(void) { return (uint8_t)(tx_len == 0u); }

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    mock_port_t *s = port_find(p->port, 1u);
    uint32_t mask = pin_mask(p->bit);

    switch (mode) {
    case TFW_PIN_OUTPUT:
        s->dir |= mask;
        return TFW_OK;
    case TFW_PIN_INPUT:
        s->dir   = s->dir & ~mask;
        s->latch = s->latch & ~mask;
        return TFW_OK;
    case TFW_PIN_INPUT_PULLUP:
        s->dir    = s->dir & ~mask;
        s->latch |= mask;
        return TFW_OK;
    default:
        return TFW_ERR_RANGE;
    }
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    mock_port_t *s = port_find(p->port, 1u);
    uint32_t mask = pin_mask(p->bit);

    if (level) { s->latch |= mask; }
    else       { s->latch = s->latch & ~mask; }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    /* On real silicon the input register is the pin's SYNCHRONISER, not an
     * input-only latch: reading it on an output pin returns the level the port
     * is driving. A mock that returned only the externally-applied level would
     * make read-after-write on an output fail here and work on hardware,
     * which is the worst direction for a mock to be wrong in -- it sends you
     * hunting for a firmware bug that does not exist. */
    *out = tfw_mock_pin_level(p->port, p->bit);
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_init(void) { return TFW_OK; }

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    if ((uint8_t)ref > (uint8_t)TFW_ADC_REF_INTERNAL) { return TFW_ERR_RANGE; }
    adc_ref = (uint8_t)ref;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    if (channel > 7u) { return TFW_ERR_BAD_PIN; }
    *out_raw = adc_value[channel];
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) { return TFW_ERR_RANGE; }
    memcpy(buf, &eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    if ((uint32_t)addr + len > EEPROM_SIZE) { return TFW_ERR_RANGE; }
    if (len != 0u) {
        if (ee_written == 0u || addr < ee_lo)              { ee_lo = addr; }
        if (ee_written == 0u || (uint32_t)addr + len > ee_hi) { ee_hi = (uint32_t)addr + len; }
        ee_written += len;
    }
    for (i = 0u; i < len; i++) {
        if ((uint32_t)addr + i != ee_stuck) {
            eeprom[addr + i] = buf[i];
        }
    }
    return TFW_OK;
}

/* The same array, as on AVR: the platform block sits in the EEPROM a test
 * provisions through tfw_hal_eeprom_write or the mock's backing store. */
tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_eeprom_read(addr, buf, len);
}

/* The part's program flash, as large as the largest one-slot part's: what a
 * test lays in it is the running image OTA_BEGIN backs up. */
static uint8_t flash[0x8000];

uint8_t *tfw_mock_flash(void)
{
    return flash;
}

void tfw_hal_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, &flash[addr], len);
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    word_find(pwm_ctl, p->ctl, 1u)->val |= (uint8_t)(1u << p->bit);
    word_find(pwm_duty, p->duty, 1u)->val = duty;
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    mock_word_t *w = word_find(pwm_ctl, p->ctl, 1u);

    w->val = (uint8_t)(w->val & (uint8_t)~(1u << p->bit));
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    (void)p;
    if (hz == 0u) { tone_hz = 0u; return TFW_OK; }
    /* The AVR implementation refuses below about 490 Hz at its prescaler; the
     * mock models that rather than accepting everything, so the range check is
     * exercised where it can be asserted. */
    if (hz < 490u) { return TFW_ERR_RANGE; }
    tone_hz = hz;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void) { tone_hz = 0u; return TFW_OK; }

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                            uint32_t timeout_us, uint32_t *out_us)
{
    (void)p; (void)level;
    if (pulse_in_us == 0u)        { return TFW_ERR_TIMEOUT; }
    if (pulse_in_us > timeout_us) { return TFW_ERR_TIMEOUT; }
    *out_us = pulse_in_us;
    return TFW_OK;
}

tfw_status_t tfw_hal_pulse_capture(const tfw_pinref_t *p, uint8_t start_level,
                                 uint16_t *out_us, uint8_t max_edges,
                                 uint32_t timeout_us, uint8_t *out_n)
{
    (void)p; (void)start_level; (void)timeout_us;
    uint8_t n = capture_n < max_edges ? capture_n : max_edges;
    if (n == 0u) { return TFW_ERR_TIMEOUT; }
    for (uint8_t i = 0u; i < n; i++) { out_us[i] = capture_us[i]; }
    *out_n = n;
    return TFW_OK;
}

/*
 * WEAK, and only these three, because the element-tier simulator
 * (tools/tfwsim, built by `make tfwsim-atecc608a`) links this mock for every
 * other peripheral and test/model/atecc608a.c as the I2C bus. Two strong
 * definitions would be a link error, and leaving the bus to the mock would
 * mean the real ATECC608A driver talks to nothing. A weak definition is the
 * link-time substitution this tree already uses for the HAL: the model's
 * strong symbols win, and every build that does not link the model gets
 * exactly the definitions below, as before.
 */
__attribute__((weak))
tfw_status_t tfw_hal_i2c_init(uint32_t hz) { last_i2c_hz = hz; return TFW_OK; }

__attribute__((weak))
tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    (void)wr; (void)wlen;
    last_i2c_addr = addr7;
    if (rlen > bus_resp_n) { return TFW_ERR_NACK; }
    for (uint8_t i = 0u; i < rlen; i++) { rd[i] = bus_resp[i]; }
    return TFW_OK;
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb)
{ last_spi_mode = mode; last_spi_clkdiv = clkdiv; last_spi_lsb = lsb; return TFW_OK; }

tfw_status_t tfw_mock_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                               uint8_t *rd, uint8_t len)
{
    (void)cs; (void)wr;
    for (uint8_t i = 0u; i < len; i++) { rd[i] = i < bus_resp_n ? bus_resp[i] : 0u; }
    return TFW_OK;
}

/* Weak so test/model/spi_nor_model.c can be the SPI bus (issue #75): it
 * answers its own chip select and hands every other one to the function above. */
__attribute__((weak))
tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    return tfw_mock_spi_xfer(cs, wr, rd, len);
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{ (void)p; *out_presence = ow_present; return TFW_OK; }

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{ (void)p; (void)buf; (void)len; return TFW_OK; }

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p;
    for (uint8_t i = 0u; i < len; i++) { buf[i] = i < bus_resp_n ? bus_resp[i] : 0u; }
    return TFW_OK;
}
