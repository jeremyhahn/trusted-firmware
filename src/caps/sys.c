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
 * rather than sys.c reaching into the transport itself.
 *
 * Held as the four bytes SYS_INFO sends -- crc then format, each a
 * little-endian u16 -- so the reply copies them in one call. Storing a u16 a
 * byte at a time costs no more than storing it whole; sending it as two u16
 * writes cost the 32U4 element build flash it did not have (#12). */
static uint8_t sys_err_wire[4];

void tfw_sys_reset_state(void)
{
    reset_pending = 0u;
    reset_mode    = 0u;
    tfw_sys_set_framer_errors(0u, 0u);
}

void tfw_sys_set_framer_errors(uint16_t err_crc, uint16_t err_format)
{
    sys_err_wire[0] = (uint8_t)(err_crc & 0xFFu);
    sys_err_wire[1] = (uint8_t)(err_crc >> 8);
    sys_err_wire[2] = (uint8_t)(err_format & 0xFFu);
    sys_err_wire[3] = (uint8_t)(err_format >> 8);
}

uint8_t tfw_sys_reset_pending(void)
{
    return reset_pending;
}

/* Four bytes, in the order given, as one little-endian u32: on the wire exactly
 * what four tfw_write_u8 calls would put there. Given constants the word folds
 * at compile time, and the calls it saves are part of what pays for #12 on the
 * 32U4 element build, which has no flash to spare. */
static void write_bytes4(tfw_writer_t *w, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    tfw_write_u32(w, (uint32_t)b0 | ((uint32_t)b1 << 8) |
                     ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24));
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
         * A pin page. Six bytes per pin -- see tfw_board_pin_page() for the
         * layout -- asked for one pin at a time, because an ATmega2560's full
         * table would not fit in any frame this protocol defines.
         *
         * The PWM byte distinguishes three states, not two: the pin has an
         * output-compare unit, it has none, or it has one that this build spent
         * on the millisecond tick. A host that cannot tell the last two apart
         * will move a load to a different pin when the right fix is a different
         * build.
         *
         * Bounds first: tfw_board_pin_page() refuses a pin beyond the board
         * before it reads the table, and writes nothing when it does. A bounds
         * check that comes after the read is one somebody will later move a
         * read in front of.
         */
        uint8_t      page[TFW_PIN_PAGE_LEN];
        tfw_status_t st = tfw_board_pin_page(tfw_read_u8(r), page);

        if (st != TFW_OK) {
            return st;
        }
        tfw_write_blob(w, page, TFW_PIN_PAGE_LEN);
        return TFW_OK;
    }

    write_bytes4(w, TFW_PROTO_VERSION_MAJOR, TFW_PROTO_VERSION_MINOR,
                 TFW_FW_MAJOR, TFW_FW_MINOR);
    write_bytes4(w, TFW_FW_PATCH, TFW_BOARD_ID,
                 TFW_PIN_COUNT, (uint8_t)TFW_SCHED_SLOTS);
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
    uint32_t ram;

    (void)r;
    tfw_write_u32(w, tfw_hal_millis());
    ram = tfw_hal_free_ram();
    /* The u16 stays where it always was, saturating at 0xFFFF rather than
     * wrapping, so an older host reads "at least 64 KiB free" instead of a small
     * wrong number. The exact value is the u32 appended at the end. */
    tfw_write_u16(w, (ram > 0xFFFFu) ? 0xFFFFu : (uint16_t)ram);
    tfw_write_u8(w,  (uint8_t)tfw_hal_reset_cause());
    tfw_write_blob(w, sys_err_wire, sizeof sys_err_wire);
    tfw_write_u32(w, ram);
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
