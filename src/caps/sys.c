/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "sys.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "ag/hal.h"
#include "../core/dispatch.h"

/* Bumped by hand when the firmware is released; distinct from the protocol
 * version, which describes the wire and moves far less often. */
#define AG_FW_MAJOR 0u
#define AG_FW_MINOR 1u
#define AG_FW_PATCH 0u

#ifndef AG_BOARD_NAME
#define AG_BOARD_NAME "host"
#endif
#ifndef AG_BOARD_ID
#define AG_BOARD_ID   0x00u
#endif
#ifndef AG_PIN_COUNT
#define AG_PIN_COUNT  0u
#endif

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
        uint8_t pin = ag_read_u8(r);
        /* Per-pin descriptors arrive with the board layer. Until then the
         * honest answer is that this build has no pins, not a fabricated
         * descriptor. */
        (void)pin;
        return AG_ERR_UNSUPPORTED;
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
    ag_write_u8(w,  (uint8_t)AG_CRYPTO_NONE);   /* until a provider lands */
    ag_write_u32(w, AG_FEATURE_BITMAP);
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
