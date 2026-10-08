/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The pin scheduler: drive a pin to a level, and drive it back after a delay.
 *
 * Any pin can be driven to a level and driven back to the complement of that
 * level after a delay, whatever is wired to it.
 *
 * It stays in FIRMWARE rather than moving to the host with the rest of the
 * domain logic, and that is deliberate: it is the safety-critical part. A host
 * that crashes with a load energised must not leave it energised, and the
 * only way to guarantee that is for the device to own the deadline.
 *
 * No interrupt touches this table. The tick ISR increments a counter and does
 * nothing else; the sweep runs in the main loop. So g_slots has exactly one
 * writer, needs no locking, contains no volatile, and compiles unchanged on the
 * host -- which is why its behaviour around the millisecond wrap can be tested
 * in four lines instead of waiting 49.7 days.
 */

#include "sched.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/hal.h"

#define SLOT_ACTIVE      0x01u
#define SLOT_FINAL_LEVEL 0x02u

typedef struct {
    tfw_ms_t  deadline;
    tfw_pin_t pin;
    uint8_t  flags;
} tfw_sched_slot_t;

static tfw_sched_slot_t g_slots[TFW_SCHED_SLOTS];

void tfw_sched_init(void)
{
    uint8_t i;
    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        g_slots[i].flags = 0u;
    }
}

uint8_t tfw_sched_active(void)
{
    uint8_t i, n = 0u;
    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        if (g_slots[i].flags & SLOT_ACTIVE) { n++; }
    }
    return n;
}

static tfw_sched_slot_t *slot_for(tfw_pin_t pin)
{
    uint8_t i;
    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        if ((g_slots[i].flags & SLOT_ACTIVE) && g_slots[i].pin == pin) {
            return &g_slots[i];
        }
    }
    return 0;
}

static tfw_sched_slot_t *free_slot(void)
{
    uint8_t i;
    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        if (!(g_slots[i].flags & SLOT_ACTIVE)) {
            return &g_slots[i];
        }
    }
    return 0;
}

tfw_status_t tfw_sched_cmd_set(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t         pin   = tfw_read_u8(r);
    uint8_t          level = tfw_read_u8(r);
    uint32_t         ms    = tfw_read_u32(r);
    tfw_pinref_t      ref;
    tfw_sched_slot_t *s;
    tfw_status_t      st;

    if (tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    if (level > 1u) {
        return TFW_ERR_RANGE;
    }
    /* Bounded so the signed-difference comparison in the sweep can always tell
     * "due" from "not yet". Past 2^31 ms it cannot, and the failure mode would
     * be a valve that stays open for seven weeks -- so the bound is enforced
     * here, where it is a clean refusal. */
    if (ms == 0u || ms > TFW_SCHED_MAX_MS) {
        return TFW_ERR_RANGE;
    }

    /* Re-arming a pin that already has a timer EXTENDS it rather than consuming
     * a second slot. A host re-issuing a command -- which a periodic
     * controller does constantly -- would otherwise exhaust the table and then
     * start failing, having done nothing wrong. */
    s = slot_for(pin);
    if (!s) {
        s = free_slot();
    }
    if (!s) {
        return TFW_ERR_NO_SLOT;
    }

    st = tfw_hal_gpio_mode(&ref, TFW_PIN_OUTPUT);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_hal_gpio_write(&ref, level);
    if (st != TFW_OK) {
        return st;
    }

    s->pin      = pin;
    s->deadline = tfw_hal_millis() + ms;
    s->flags    = (uint8_t)(SLOT_ACTIVE | (level ? 0u : SLOT_FINAL_LEVEL));

    tfw_write_u8(w, (uint8_t)(s - g_slots));
    return TFW_OK;
}

tfw_status_t tfw_sched_cmd_cancel(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t         pin         = tfw_read_u8(r);
    uint8_t          apply_final = tfw_read_u8(r);
    tfw_sched_slot_t *s;

    (void)w;
    s = slot_for(pin);
    if (!s) {
        return TFW_ERR_NO_SLOT;
    }
    if (apply_final) {
        tfw_pinref_t ref;
        if (tfw_board_pin(pin, &ref) == TFW_OK) {
            (void)tfw_hal_gpio_write(&ref, (uint8_t)((s->flags & SLOT_FINAL_LEVEL) ? 1u : 0u));
        }
    }
    s->flags = 0u;
    return TFW_OK;
}

tfw_status_t tfw_sched_cmd_query(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t               pin = tfw_read_u8(r);
    const tfw_sched_slot_t *s   = slot_for(pin);
    tfw_ms_t                now = tfw_hal_millis();

    if (!s) {
        tfw_write_u8(w, 0u);     /* not active */
        tfw_write_u8(w, 0u);
        tfw_write_u32(w, 0u);
        return TFW_OK;
    }
    tfw_write_u8(w, 1u);
    tfw_write_u8(w, (uint8_t)((s->flags & SLOT_FINAL_LEVEL) ? 0u : 1u));  /* held level */
    /* Clamped at zero: a deadline already passed but not yet swept would
     * otherwise report a remaining time of four billion milliseconds. */
    tfw_write_u32(w, (tfw_ms_t)((int32_t)(s->deadline - now) > 0
                              ? (s->deadline - now) : 0u));
    return TFW_OK;
}

void tfw_sched_poll(void)
{
    tfw_ms_t now = tfw_hal_millis();
    uint8_t i;

    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        tfw_pinref_t ref;

        if (!(g_slots[i].flags & SLOT_ACTIVE)) {
            continue;
        }
        /*
         * THE SIGNED DIFFERENCE. now and deadline are unsigned and both wrap
         * every 49.7 days; subtracting them and reading the result as signed
         * is correct across that wrap for any interval below 2^31 ms, which
         * tfw_sched_cmd_set enforces.
         *
         * Writing `now >= deadline` instead would work for seven weeks and
         * then, for a few milliseconds, fire every armed timer at once or none
         * of them ever again -- a bug that reproduces twice a year.
         */
        if ((int32_t)(now - g_slots[i].deadline) >= 0) {
            if (tfw_board_pin(g_slots[i].pin, &ref) == TFW_OK) {
                (void)tfw_hal_gpio_write(&ref,
                    (uint8_t)((g_slots[i].flags & SLOT_FINAL_LEVEL) ? 1u : 0u));
            }
            g_slots[i].flags = 0u;
        }
    }
}
