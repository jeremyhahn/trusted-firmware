/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "sys.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "ag/hal.h"
#include "ag/crypto.h"
#include "ag/board.h"
#include "../core/dispatch.h"

/* Bumped by hand when the firmware is released; distinct from the protocol
 * version, which describes the wire and moves far less often. */
#define AG_FW_MAJOR 0u
#define AG_FW_MINOR 1u
#define AG_FW_PATCH 0u

static uint8_t reset_pending;
static uint8_t reset_mode;

void ag_sys_reset_state(void)
{
    reset_pending = 0u;
    reset_mode    = 0u;
}

uint8_t ag_sys_reset_pending(void)
{
    return reset_pending;
}

/*
 * CAPS -- what this build actually is.
 *
 * Two tiers. With no argument it returns the board header; with a pin index it
 * returns that pin's descriptor. A full pin table for an ATmega2560 is 350
 * bytes and fits in no frame this protocol defines, let alone an ATtiny's.
 *
 * The feature bitmap reports what was COMPILED IN, not what the MCU could in
 * principle do. Two boards with the same part can ship different capability
 * sets, so a host that inferred capability from the MCU id would be wrong on
 * the second one.
 */
ag_status_t ag_sys_cmd_caps(ag_reader_t *r, ag_writer_t *w)
{
    if (ag_reader_remaining(r) == 1u) {
        /*
         * A pin page. Five bytes per pin, asked for one pin at a time, because
         * an ATmega2560's full table is 350 bytes and fits in no frame this
         * protocol defines.
         *
         * The PWM byte distinguishes three states, not two: the pin has an
         * output-compare unit, it has none, or it has one that this build spent
         * on the millisecond tick. A host that cannot tell the last two apart
         * will move a load to a different pin when the right fix is a different
         * build.
         */
        uint8_t     pin = ag_read_u8(r);
        uint8_t     adc = AG_ADC_NONE;
        ag_pinref_t ref;
        ag_pwmref_t pwm;
        uint8_t     flags = ag_board_pin_flags(pin);

        if (pin >= ag_board_pin_count()) {
            return AG_ERR_BAD_PIN;
        }
        (void)ag_board_adc_channel(pin, &adc);
        ag_write_u8(w, pin);
        ag_write_u8(w, flags);
        /* The port index and bit, as the board packs them, or 0xFF for a pin
         * with no digital port at all -- an ADC-only input, which can be read
         * and never written. */
        ag_write_u8(w, ag_board_pin(pin, &ref) == AG_OK
                           ? (uint8_t)((ref.bit & 0x0Fu) | 0xA0u) : 0xFFu);
        ag_write_u8(w, adc);
        ag_write_u8(w, ag_board_pwm(pin, &pwm) == AG_OK ? 1u
                       : ((flags & AG_PIN_PWM) ? 2u : 0u));
        return AG_OK;
    }

    ag_write_u8(w,  AG_PROTO_VERSION_MAJOR);
    ag_write_u8(w,  AG_PROTO_VERSION_MINOR);
    ag_write_u8(w,  AG_FW_MAJOR);
    ag_write_u8(w,  AG_FW_MINOR);
    ag_write_u8(w,  AG_FW_PATCH);
    ag_write_u8(w,  AG_BOARD_ID);
    ag_write_u8(w,  AG_PIN_COUNT);
    ag_write_u8(w,  (uint8_t)AG_SCHED_SLOTS);
    ag_write_u8(w,  (uint8_t)AG_MAX_PAYLOAD);
    /* Asked, not assumed. A host learns what root of trust it is talking to
     * before it decides what any evidence from this device is worth. */
    ag_write_u8(w,  (uint8_t)ag_crypto_tier());
    ag_write_u32(w, AG_FEATURE_BITMAP);
    /* Which signature algorithms the provider can actually perform. Appended
     * after the feature bitmap so a host built against the earlier header stops
     * reading where it always did, rather than misparsing the tail. */
    ag_write_u16(w, ag_crypto_algorithms());
    return AG_OK;
}

/* PING -- echoes its payload. The one command that proves the whole path,
 * framing through dispatch and back, without touching any hardware. */
ag_status_t ag_sys_cmd_ping(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t        n = ag_reader_remaining(r);
    const uint8_t *p = ag_read_blob(r, n);

    if (n && !p) {
        return AG_ERR_BAD_ARGC;
    }
    if (n) {
        ag_write_blob(w, p, n);
    }
    return AG_OK;
}

/*
 * RESET -- and it deliberately does not reset.
 *
 * It latches the request and returns normally, so the response is framed and
 * queued like any other. ag_sys_poll() then waits for the UART to go idle and
 * only then pulls the trigger.
 *
 * The firmware being replaced called delay(100) and hoped. That is a guess that
 * is too long on every healthy link and too short on a busy one; this waits for
 * the actual condition instead.
 */
ag_status_t ag_sys_cmd_reset(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t mode = ag_read_u8(r);
    (void)w;

    if (mode > 1u) {
        return AG_ERR_RANGE;
    }
    reset_mode    = mode;
    reset_pending = 1u;
    return AG_OK;
}

ag_status_t ag_sys_cmd_sys_info(ag_reader_t *r, ag_writer_t *w)
{
    (void)r;
    ag_write_u32(w, ag_hal_millis());
    ag_write_u16(w, ag_hal_free_ram());
    ag_write_u8(w,  (uint8_t)ag_hal_reset_cause());
    return AG_OK;
}

void ag_sys_poll(void)
{
    if (reset_pending && ag_hal_uart_tx_idle()) {
        ag_hal_reset();
    }
}
