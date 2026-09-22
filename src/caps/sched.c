/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The pin scheduler: drive a pin to a level, and drive it back after a delay.
 *
 * This is the one behaviour carried forward from the firmware being replaced,
 * where it was a 7x3 table of irrigation channels. Generalised from "channel
 * index" to any pin, and from "on then off" to "level then its complement".
 *
 * It stays in FIRMWARE rather than moving to the host with the rest of the
 * domain logic, and that is deliberate: it is the safety-critical part. A host
 * that crashes with a pump running must not leave the pump running, and the
 * only way to guarantee that is for the device to own the deadline.
 *
 * No interrupt touches this table. The tick ISR increments a counter and does
 * nothing else; the sweep runs in the main loop. So g_slots has exactly one
 * writer, needs no locking, contains no volatile, and compiles unchanged on the
 * host -- which is why its behaviour around the millisecond wrap can be tested
 * in four lines instead of waiting 49.7 days.
 */

#include "sched.h"
#include "ag/board.h"
#include "ag/config.h"
#include "ag/hal.h"

#define SLOT_ACTIVE      0x01u
#define SLOT_FINAL_LEVEL 0x02u

typedef struct {
    ag_ms_t  deadline;
    ag_pin_t pin;
    uint8_t  flags;
} ag_sched_slot_t;

static ag_sched_slot_t g_slots[AG_SCHED_SLOTS];

void ag_sched_init(void)
{
    uint8_t i;
    for (i = 0u; i < AG_SCHED_SLOTS; i++) {
        g_slots[i].flags = 0u;
    }
}

uint8_t ag_sched_active(void)
{
    uint8_t i, n = 0u;
    for (i = 0u; i < AG_SCHED_SLOTS; i++) {
        if (g_slots[i].flags & SLOT_ACTIVE) { n++; }
    }
    return n;
}

static ag_sched_slot_t *slot_for(ag_pin_t pin)
{
    uint8_t i;
    for (i = 0u; i < AG_SCHED_SLOTS; i++) {
        if ((g_slots[i].flags & SLOT_ACTIVE) && g_slots[i].pin == pin) {
            return &g_slots[i];
        }
    }
    return 0;
}

static ag_sched_slot_t *free_slot(void)
{
    uint8_t i;
    for (i = 0u; i < AG_SCHED_SLOTS; i++) {
        if (!(g_slots[i].flags & SLOT_ACTIVE)) {
            return &g_slots[i];
        }
    }
    return 0;
}

ag_status_t ag_sched_cmd_set(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t         pin   = ag_read_u8(r);
    uint8_t          level = ag_read_u8(r);
    uint32_t         ms    = ag_read_u32(r);
    ag_pinref_t      ref;
    ag_sched_slot_t *s;
    ag_status_t      st;

    if (ag_board_pin_flags(pin) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    if (level > 1u) {
        return AG_ERR_RANGE;
    }
    /* Bounded so the signed-difference comparison in the sweep can always tell
     * "due" from "not yet". Past 2^31 ms it cannot, and the failure mode would
     * be a valve that stays open for seven weeks -- so the bound is enforced
     * here, where it is a clean refusal. */
    if (ms == 0u || ms > AG_SCHED_MAX_MS) {
        return AG_ERR_RANGE;
    }

    /* Re-arming a pin that already has a timer EXTENDS it rather than consuming
     * a second slot. A host re-issuing a command -- which an irrigation
     * controller does constantly -- would otherwise exhaust the table and then
     * start failing, having done nothing wrong. */
    s = slot_for(pin);
    if (!s) {
        s = free_slot();
    }
    if (!s) {
        return AG_ERR_NO_SLOT;
    }

    st = ag_hal_gpio_mode(&ref, AG_PIN_OUTPUT);
    if (st != AG_OK) {
        return st;
    }
    st = ag_hal_gpio_write(&ref, level);
    if (st != AG_OK) {
        return st;
    }

    s->pin      = pin;
    s->deadline = ag_hal_millis() + ms;
    s->flags    = (uint8_t)(SLOT_ACTIVE | (level ? 0u : SLOT_FINAL_LEVEL));

    ag_write_u8(w, (uint8_t)(s - g_slots));
    return AG_OK;
}

ag_status_t ag_sched_cmd_cancel(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t         pin         = ag_read_u8(r);
    uint8_t          apply_final = ag_read_u8(r);
    ag_sched_slot_t *s;

    (void)w;
    s = slot_for(pin);
    if (!s) {
        return AG_ERR_NO_SLOT;
    }
    if (apply_final) {
        ag_pinref_t ref;
        if (ag_board_pin(pin, &ref) == AG_OK) {
            (void)ag_hal_gpio_write(&ref, (uint8_t)((s->flags & SLOT_FINAL_LEVEL) ? 1u : 0u));
        }
    }
    s->flags = 0u;
    return AG_OK;
}

ag_status_t ag_sched_cmd_query(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t               pin = ag_read_u8(r);
    const ag_sched_slot_t *s   = slot_for(pin);
    ag_ms_t                now = ag_hal_millis();

    if (!s) {
        ag_write_u8(w, 0u);     /* not active */
        ag_write_u8(w, 0u);
        ag_write_u32(w, 0u);
        return AG_OK;
    }
    ag_write_u8(w, 1u);
    ag_write_u8(w, (uint8_t)((s->flags & SLOT_FINAL_LEVEL) ? 0u : 1u));  /* held level */
    /* Clamped at zero: a deadline already passed but not yet swept would
     * otherwise report a remaining time of four billion milliseconds. */
    ag_write_u32(w, (ag_ms_t)((int32_t)(s->deadline - now) > 0
                              ? (s->deadline - now) : 0u));
    return AG_OK;
}

void ag_sched_poll(void)
{
    ag_ms_t now = ag_hal_millis();
    uint8_t i;

    for (i = 0u; i < AG_SCHED_SLOTS; i++) {
        ag_pinref_t ref;

        if (!(g_slots[i].flags & SLOT_ACTIVE)) {
            continue;
        }
        /*
         * THE SIGNED DIFFERENCE. now and deadline are unsigned and both wrap
         * every 49.7 days; subtracting them and reading the result as signed
         * is correct across that wrap for any interval below 2^31 ms, which
         * ag_sched_cmd_set enforces.
         *
         * Writing `now >= deadline` instead would work for seven weeks and
         * then, for a few milliseconds, fire every armed timer at once or none
         * of them ever again -- a bug that reproduces twice a year.
         */
        if ((int32_t)(now - g_slots[i].deadline) >= 0) {
            if (ag_board_pin(g_slots[i].pin, &ref) == AG_OK) {
                (void)ag_hal_gpio_write(&ref,
                    (uint8_t)((g_slots[i].flags & SLOT_FINAL_LEVEL) ? 1u : 0u));
            }
            g_slots[i].flags = 0u;
        }
    }
}
