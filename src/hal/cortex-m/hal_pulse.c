/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Pulse timing on any Cortex-M: the pin read through tfw_hal_gpio_read, the
 * time counted on SysTick's current value across reloads, as tfw_hal_delay_us
 * counts it (cm_cycles.h). Needs tfw_hal_tick_init, which main() calls first.
 *
 * Polling, as the AVR's and the ESP32's implementations do, with their
 * semantics: pulse_in waits for the leading edge (a pulse already in progress
 * is not one it saw start), then measures the level; capture lines up on
 * start_level and records each interval, each bounded by timeout_us, and a
 * line that goes quiet ends the capture. An interrupt between two polls makes
 * an interval read long by its length; the count itself loses no time, since
 * every poll adds the cycles since the one before.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "cm_cycles.h"

/* Elapsed whole microseconds since the clock started, and the cycles toward
 * the next one. */
typedef struct {
    uint32_t last;
    uint32_t frac;
    uint32_t us;
} pulse_clock_t;

static void clock_start(pulse_clock_t *c)
{
    c->last = TFW_CM_SYST_CVR;
    c->frac = 0u;
    c->us   = 0u;
}

static void clock_tick(pulse_clock_t *c)
{
    c->frac += tfw_cm_cycles_since(&c->last);
    c->us   += c->frac / TFW_CM_CYCLES_PER_US;
    c->frac %= TFW_CM_CYCLES_PER_US;
}

/* Waits for the pin to read `want`: TFW_OK, TFW_ERR_TIMEOUT once the clock
 * passes `deadline`, or the read's own failure. */
static tfw_status_t wait_level(const tfw_pinref_t *p, uint8_t want, pulse_clock_t *c,
                               uint32_t deadline)
{
    for (;;) {
        uint8_t      lv = 0u;
        tfw_status_t st = tfw_hal_gpio_read(p, &lv);

        if (st != TFW_OK) {
            return st;
        }
        /* Tick before the comparison, so a level seen on this read is
         * stamped with this read's time, not the previous poll's; and
         * check the deadline first, so a level first seen after it is a
         * timeout, not a measurement. */
        clock_tick(c);
        if (c->us > deadline) {
            return TFW_ERR_TIMEOUT;
        }
        if ((lv ? 1u : 0u) == want) {
            return TFW_OK;
        }
    }
}

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                             uint32_t timeout_us, uint32_t *out_us)
{
    uint8_t       want = level ? 1u : 0u;
    pulse_clock_t c;
    tfw_status_t  st;
    uint32_t      start;

    clock_start(&c);
    st = wait_level(p, (uint8_t)!want, &c, timeout_us);
    if (st == TFW_OK) {
        st = wait_level(p, want, &c, timeout_us);
    }
    if (st != TFW_OK) {
        return st;
    }
    start = c.us;
    st    = wait_level(p, (uint8_t)!want, &c, timeout_us);
    if (st != TFW_OK) {
        return st;
    }
    *out_us = c.us - start;
    return TFW_OK;
}

tfw_status_t tfw_hal_pulse_capture(const tfw_pinref_t *p, uint8_t start_level,
                                  uint16_t *out_us, uint8_t max_edges,
                                  uint32_t timeout_us, uint8_t *out_n)
{
    uint8_t       want = start_level ? 1u : 0u;
    uint8_t       n    = 0u;
    pulse_clock_t c;
    tfw_status_t  st;

    *out_n = 0u;
    clock_start(&c);
    st = wait_level(p, want, &c, timeout_us); /* line up on the first level */
    if (st != TFW_OK) {
        return st;
    }
    while (n < max_edges) {
        uint32_t from = c.us;
        uint32_t w;

        want = (uint8_t)!want;
        st   = wait_level(p, want, &c, from + timeout_us);
        if (st == TFW_ERR_TIMEOUT) {
            /* A quiet line ends the capture rather than failing it: the
             * edges already recorded are the message. */
            break;
        }
        if (st != TFW_OK) {
            return st;
        }
        w           = c.us - from;
        out_us[n++] = (uint16_t)(w > 65535u ? 65535u : w);
    }
    *out_n = n;
    return n ? TFW_OK : TFW_ERR_TIMEOUT;
}
