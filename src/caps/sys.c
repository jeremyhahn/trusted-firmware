/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "sys.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/board.h"
#include "../core/dispatch.h"
#include "../core/auth_session.h"
#include "../core/update_request.h"

/* Bumped by hand when the firmware is released; distinct from the protocol
 * version, which describes the wire and moves far less often. */
#define TFW_FW_MAJOR 0u
#define TFW_FW_MINOR 1u
#define TFW_FW_PATCH 0u

static uint8_t reset_pending;
static uint8_t reset_mode;

/* SYS_INFO's view of the framer's counters, refreshed once a pass by
 * tfw_sys_set_framer_errors(). See sys.h for why this indirection exists
 * rather than sys.c reaching into the transport itself. */
static uint16_t sys_err_crc;
static uint16_t sys_err_format;

void tfw_sys_reset_state(void)
{
    reset_pending  = 0u;
    reset_mode     = 0u;
    sys_err_crc    = 0u;
    sys_err_format = 0u;
}

void tfw_sys_set_framer_errors(uint16_t err_crc, uint16_t err_format)
{
    sys_err_crc    = err_crc;
    sys_err_format = err_format;
}

uint8_t tfw_sys_reset_pending(void)
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
tfw_status_t tfw_sys_cmd_caps(tfw_reader_t *r, tfw_writer_t *w)
{
    if (tfw_reader_remaining(r) == 1u) {
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
        uint8_t pin = tfw_read_u8(r);
        uint8_t adc = TFW_ADC_NONE;

        /* Bounds first. Reading the descriptor before checking happens to be
         * safe -- the accessors bound-check and return zero -- but a bounds
         * check that comes after the read is one somebody will later move a
         * read in front of. */
        if (pin >= tfw_board_pin_count()) {
            return TFW_ERR_BAD_PIN;
        }
        (void)tfw_board_adc_channel(pin, &adc);
        tfw_write_u8(w, pin);
        tfw_write_u8(w, tfw_board_pin_flags(pin));
        /* The port index and bit exactly as the board packs them -- high nibble
         * TFW_PORT_NONE for a pin with no digital port, which is an ADC-only
         * input that can be read and never written. */
        tfw_write_u8(w, tfw_board_pin_port_bit(pin));
        tfw_write_u8(w, adc);
        tfw_write_u8(w, tfw_board_pin_pwm_state(pin));
        return TFW_OK;
    }

    tfw_write_u8(w,  TFW_PROTO_VERSION_MAJOR);
    tfw_write_u8(w,  TFW_PROTO_VERSION_MINOR);
    tfw_write_u8(w,  TFW_FW_MAJOR);
    tfw_write_u8(w,  TFW_FW_MINOR);
    tfw_write_u8(w,  TFW_FW_PATCH);
    tfw_write_u8(w,  TFW_BOARD_ID);
    tfw_write_u8(w,  TFW_PIN_COUNT);
    tfw_write_u8(w,  (uint8_t)TFW_SCHED_SLOTS);
    tfw_write_u8(w,  (uint8_t)TFW_MAX_PAYLOAD);
    /* Asked, not assumed. A host learns what root of trust it is talking to
     * before it decides what any evidence from this device is worth. */
    tfw_write_u8(w,  (uint8_t)tfw_crypto_tier());
    tfw_write_u32(w, TFW_FEATURE_BITMAP);
    /* Which signature algorithms the provider can actually perform. Appended
     * after the feature bitmap so a host built against the earlier header stops
     * reading where it always did, rather than misparsing the tail. */
    tfw_write_u16(w, tfw_crypto_algorithms());
    return TFW_OK;
}

/* PING -- echoes its payload. The one command that proves the whole path,
 * framing through dispatch and back, without touching any hardware. */
tfw_status_t tfw_sys_cmd_ping(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t        n = tfw_reader_remaining(r);
    const uint8_t *p = tfw_read_blob(r, n);

    if (n && !p) {
        return TFW_ERR_BAD_ARGC;
    }
    if (n) {
        tfw_write_blob(w, p, n);
    }
    return TFW_OK;
}

/*
 * RESET -- and it deliberately does not reset.
 *
 * Mode 0 restarts the application. Mode 1 does the same watchdog reset after
 * leaving tfw-boot a one-shot request to open an update session instead of
 * jumping; see include/tfw/boot.h. A board with no boot section answers mode 1
 * with TFW_ERR_UNSUPPORTED. Mode 1 requires the operator auth session on the
 * element tier (TFW_ERR_AUTH_REQUIRED otherwise).
 *
 * It latches the request and returns normally, so the response is framed and
 * queued like any other. tfw_sys_poll() then waits for the UART to go idle and
 * only then pulls the trigger.
 *
 * The firmware being replaced called delay(100) and hoped. That is a guess that
 * is too long on every healthy link and too short on a busy one; this waits for
 * the actual condition instead.
 */
tfw_status_t tfw_sys_cmd_reset(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t mode = tfw_read_u8(r);
    (void)w;

    if (mode > 1u) {
        return TFW_ERR_RANGE;
    }

    /*
     * Mode 1 is the one RESET that changes what the device will accept next --
     * a signed image, delivered over an open session -- so it needs the
     * operator auth session (issue #4) exactly as a reserved-EEPROM read does.
     * Checked BEFORE anything else about mode 1, including whether this board
     * has a bootloader at all: an unauthenticated peer learns nothing from the
     * answer. Mode 0 never reaches here, and on the soft and CRYPTO=none tiers
     * tfw_auth_gate() is permissive (auth_session_none.c), so those builds are
     * unaffected -- the same accepted gap storage.c documents.
     */
    if (mode == 1u) {
        tfw_status_t gate = tfw_auth_gate();
        if (gate != TFW_OK) {
            return gate;
        }
        if (!tfw_update_request_supported()) {
            return TFW_ERR_UNSUPPORTED;
        }
    }
    reset_mode    = mode;
    reset_pending = 1u;
    return TFW_OK;
}

/*
 * The framer's counters are APPENDED, not inserted: everything through
 * reset_cause is exactly what the first SYS_INFO ever shipped, so an older
 * host that reads only that prefix gets the same three fields at the same
 * offsets it always has, and a newer host reads two more. Extending a reply
 * this way needs nothing on the wire to say the reply grew -- LEN already
 * says so, and the framer never truncates a real frame -- which is the same
 * reasoning include/tfw/status.h gives for why status codes extend rather
 * than renumber, applied to a reply shape instead of an enum.
 */
tfw_status_t tfw_sys_cmd_sys_info(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)r;
    tfw_write_u32(w, tfw_hal_millis());
    tfw_write_u16(w, tfw_hal_free_ram());
    tfw_write_u8(w,  (uint8_t)tfw_hal_reset_cause());
    tfw_write_u16(w, sys_err_crc);
    tfw_write_u16(w, sys_err_format);
    return TFW_OK;
}

void tfw_sys_poll(void)
{
    if (reset_pending && tfw_hal_uart_tx_idle()) {
        if (reset_mode == 1u) {
            tfw_update_request_arm();
        }
        tfw_hal_reset();
    }
}
