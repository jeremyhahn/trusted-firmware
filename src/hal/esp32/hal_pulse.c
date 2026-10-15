/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Pulse timing by polling the pin against esp_timer's microsecond count.
 *
 * Polling, as the AVR's implementation does, rather than the RMT peripheral:
 * the contract is "measure one pulse" and "capture these edges", both bounded
 * by a timeout the caller supplies, and a busy poll at the idle priority is
 * preempted by every ESP-IDF task -- so an interval can read long by the
 * length of a preemption. RMT capture is the follow-up that removes that, and
 * the contract does not change for it.
 */

#include "driver/gpio.h"
#include "esp_timer.h"

#include "tfw/hal.h"

static int level_of(const tfw_pinref_t *p)
{
    return gpio_get_level((gpio_num_t)p->gpio);
}

/* Waits for the pin to read `want`; 0 on success, -1 on timeout. */
static int wait_level(const tfw_pinref_t *p, int want, int64_t deadline)
{
    while (level_of(p) != want) {
        if (esp_timer_get_time() > deadline) {
            return -1;
        }
    }
    return 0;
}

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                             uint32_t timeout_us, uint32_t *out_us)
{
    int     want     = level ? 1 : 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_us;
    int64_t start;

    /* A pulse already in progress is not a pulse we saw start. */
    if (wait_level(p, !want, deadline) != 0 || wait_level(p, want, deadline) != 0) {
        return TFW_ERR_TIMEOUT;
    }
    start = esp_timer_get_time();
    if (wait_level(p, !want, deadline) != 0) {
        return TFW_ERR_TIMEOUT;
    }
    *out_us = (uint32_t)(esp_timer_get_time() - start);
    return TFW_OK;
}

tfw_status_t tfw_hal_pulse_capture(const tfw_pinref_t *p, uint8_t start_level,
                                  uint16_t *out_us, uint8_t max_edges,
                                  uint32_t timeout_us, uint8_t *out_n)
{
    int     cur      = start_level ? 1 : 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_us;
    int64_t last;
    uint8_t n = 0u;

    *out_n = 0u;
    if (wait_level(p, cur, deadline) != 0) {
        return TFW_ERR_TIMEOUT;
    }
    last = esp_timer_get_time();
    while (n < max_edges) {
        int64_t now;

        if (wait_level(p, !cur, deadline) != 0) {
            break;              /* the line went quiet: what we have is the capture */
        }
        now = esp_timer_get_time();
        out_us[n++] = (uint16_t)((now - last) > 0xFFFF ? 0xFFFF : (now - last));
        last = now;
        cur  = !cur;
    }
    *out_n = n;
    return n ? TFW_OK : TFW_ERR_TIMEOUT;
}
