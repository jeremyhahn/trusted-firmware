/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Recovery by OTA: the bootloader stays reachable.
 *
 * Linked when BOOT_OTA=1. See boot/boot_recovery.h for why there are two of
 * these and boot/boot_ota.h for why a part with 28 KB of application space has
 * no second slot to fall back to.
 */

#include <string.h>

#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "../boot_hal.h"
#include "../boot_image.h"
#include "../boot_ota.h"
#include "../boot_platform.h"
#include "../boot_policy.h"
#include "../boot_recovery.h"
#include "../boot_store.h"
#include "../boot_session.h"
#include "../boot_swap.h"
#include "../boot_trial.h"
#include "frame.h"

static tfw_framer_t framer;
static tfw_ota_t    ota;
static uint8_t     outbuf[TFW_MAX_FRAME];
static tfw_boot_platform_t *cur;

/* ------------------------------------------------------------------ ota */

/*
 * STAGED is stored before the first page is erased. From then until the trial
 * is armed the region holds no image that may run: half of one while the
 * transfer lasts, and a whole, verified, unconfirmed one between the last page
 * and the TRIAL write. Without the mark, a reset in that second window would
 * find GOOD and boot the new image as accepted, with no trial. A reset during
 * this write itself leaves the old state (the page is not yet touched) or a
 * torn block (see tfw_boot_platform_store).
 */
static tfw_status_t write_page(uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)len;   /* always one whole page; tfw_ota pads the tail */
    /* Only where the session writes the region in place. On a board with two
     * slots it writes the secondary: the primary is whole until the swap, and
     * a reset mid-transfer boots it through the gate as on any boot. Nothing
     * reads the secondary until OTA_END stores PENDING. */
    if ((uint32_t)TFW_OTA_STAGE_DEST == (uint32_t)TFW_BOOT_APP_BASE &&
        cur->boot_state != TFW_BOOT_STATE_STAGED) {
        cur->boot_state = TFW_BOOT_STATE_STAGED;
        tfw_boot_platform_store(cur);
    }
    return tfw_boot_flash_write_page(addr, data);
}

static void emit(const uint8_t *b, uint16_t n)
{
    for (uint16_t i = 0u; i < n; i++) {
        tfw_boot_uart_tx(b[i]);
    }
}

/* Whether a handled frame proves the peer is making progress on an update.
 * PING and OTA_STATUS are deliberately not progress: they are what a probe
 * sends, and a probe must not be able to hold the session open. */
static uint8_t is_progress(uint8_t cmd, tfw_status_t st)
{
    return (uint8_t)(st == TFW_OK
                     && (cmd == TFW_CMD_OTA_BEGIN || cmd == TFW_CMD_OTA_DATA
                         || cmd == TFW_CMD_OTA_END));
}

/*
 * The OTA loop. `lim` NULL is recovery: no bound of any kind, one attempt after
 * another for as long as it takes, because recovery has nothing to fall back
 * to. A non-NULL `lim` is a REQUESTED session on a device that can already boot,
 * and it is bounded four ways, any of which returns to the caller:
 *
 *   idle_polls  consecutive empty receive polls: a quiet line.
 *   idle_bytes  bytes since the last accepted OTA frame: a busy line of junk,
 *               partial frames or PINGs, which leaves no empty poll for the
 *               first budget to count.
 *   max_polls   every iteration of the loop, empty or not, from entry.
 *   max_bytes   every byte received, from entry.
 *
 * The first two restart on an accepted OTA_BEGIN/DATA/END (is_progress). The
 * last two NEVER restart: an accepted frame is only well-formed, not
 * authenticated, so a peer that speaks the OTA grammar could otherwise drip one
 * frame per idle window forever. A drip hits max_polls, a flood hits max_bytes.
 * The units are iterations and bytes because there is no timer and no delay;
 * main.c converts durations from F_CPU and the baud rate.
 *
 * ONE ATTEMPT. A requested session ends after the first OTA_END that does not
 * lead to a verified image -- refused as short or out of order, or failed by
 * tfw_boot_evaluate. Without that, a bogus transfer could be followed by another
 * and the idle budgets restarted each time. Ending falls through to the normal
 * measure/verify/jump; see tfw_boot_session_open for what that means for a
 * region the failed attempt already overwrote.
 */
/* What serve does after a frame: carry on, carry on with the idle bounds
 * restarted, or end the session. */
typedef enum {
    NEXT_CONTINUE,
    NEXT_PROGRESS,
    NEXT_END
} next_t;

/*
 * One whole frame: answered, and on a verified OTA_END the image activated.
 *
 * Kept out of line on purpose. The session's clock is its empty pass
 * (boot_tick.h), and an empty pass touches nothing here. Under -flto this
 * function, the swap-move and the gate inlined into serve made serve's frame
 * 220 bytes on the 2560 OTA image, past ldd/std's 63-byte reach, and moved its
 * 32-bit poll counters there: an empty pass went from 77 to about 104 cycles,
 * and the session ran 35% past its window. Out of line, its resp[] and the
 * commit's locals are a frame of their own, live only while a frame is
 * handled, and serve keeps its counters in registers.
 */
static next_t answer(tfw_boot_platform_t *plat,
                     const tfw_boot_session_limits_t *lim) __attribute__((noinline));

static next_t answer(tfw_boot_platform_t *plat,
                     const tfw_boot_session_limits_t *lim)
{
    uint8_t  resp[TFW_MAX_PAYLOAD];
    uint8_t  rlen = 0u;
    uint8_t  cmd  = tfw_frame_op(&framer);
    uint8_t  seq  = tfw_frame_seq(&framer);
    tfw_boot_session_action_t action;
    tfw_status_t st;
    uint16_t n;
    next_t   next;

    action = TFW_BOOT_SESSION_CONTINUE;
    st = cmd == TFW_CMD_OTA_BEGIN ? tfw_boot_trial_begin_permit(plat) : TFW_OK;
    if (st == TFW_OK) {
        st = tfw_boot_session_handle(&ota, cmd,
                                    tfw_frame_payload(&framer),
                                    tfw_frame_payload_len(&framer),
                                    resp, &rlen, &action);
    }

    n = tfw_frame_encode_response(outbuf, sizeof outbuf, seq, cmd, st,
                                 resp, rlen);
    emit(outbuf, n);

    next = is_progress(cmd, st) ? NEXT_PROGRESS : NEXT_CONTINUE;
    if (action == TFW_BOOT_SESSION_RESET) {
        tfw_boot_reset();
    }
    if (lim && cmd == TFW_CMD_OTA_END && st != TFW_OK) {
        return NEXT_END;   /* the one attempt failed before verification */
    }
    if (action != TFW_BOOT_SESSION_COMMIT) {
        return next;
    }

    /*
     * COMMIT. Every byte arrived; now find out whether any of it was
     * signed.
     *
     * The same tfw_boot_evaluate the cold path runs, against the same
     * floor, with the same ordering. One decision function, so an image
     * that would be refused at boot cannot be accepted at update.
     * A floor whose authority cannot be read refuses the image: the
     * EEPROM copy is not the element's floor and checks nothing.
     *
     * On a board with two slots the image is in the secondary. It is
     * checked there first and, passed, swapped into the primary, where
     * the gate reads it again; refused in the secondary nothing is
     * swapped, refused in the primary the slots swap back, and the
     * previous image is as it was (tfw_boot_swap_commit). Elsewhere it
     * was written in place.
     */
    {
        tfw_boot_verdict_t v;
        uint32_t           floor;

        st = tfw_boot_floor_load(plat, &floor);
        if (st == TFW_OK) {
            st = tfw_boot_swap_commit(plat, floor);
        }
        if (st == TFW_OK) {
            st = tfw_boot_evaluate(floor, &v);
        }
        n  = tfw_frame_encode_event(outbuf, sizeof outbuf, st, 0, 0u);
        emit(outbuf, n);

        if (st != TFW_OK) {
            /* The application region holds an image that will not
             * verify. In recovery the loop carries on and takes another
             * attempt. In a requested session this was the one attempt:
             * return, and the caller's tfw_boot_evaluate -- which will
             * fail the same way -- lands in recovery if the region was
             * overwritten. */
            tfw_ota_abort(&ota);
            (void)tfw_boot_swap_fall_back(plat);
            plat->ota_state = TFW_BOOT_OTA_FAILED;
            tfw_boot_platform_store(plat);
            return lim ? NEXT_END : next;
        }

        /* The one trial (decision 9). The image is already where it
         * runs, so a verified OTA_END is activated at once, and the
         * trial is armed before the jump. The floor does not move:
         * it advances at the reset after a confirm (decision 5).
         * One write, STAGED -> TRIAL. A reset before it leaves STAGED
         * (recovery, the image does not run); during it, a torn block
         * (recovery); after it, TRIAL (the trial failed: recovery). */
        plat->ota_state = TFW_BOOT_OTA_PENDING;
        (void)tfw_boot_trial_arm(plat);
        tfw_boot_platform_store(plat);

        tfw_boot_hand_over(&v);
    }
    return next;
}

static void serve(tfw_boot_platform_t *plat, tfw_status_t entry_reason,
                  const tfw_boot_session_limits_t *lim)
{
    uint32_t idle        = 0u;
    uint32_t bytes       = 0u;
    uint32_t total_polls = 0u;
    uint32_t total_bytes = 0u;

    cur = plat;
    (void)tfw_ota_init(&ota, write_page, tfw_boot_flash_page_size());
    tfw_framer_init(&framer);

    /* Announce, once, why we are here. A host that sees this knows immediately
     * that it is talking to a bootloader and why, rather than inferring it from
     * a series of TFW_ERR_UNKNOWN_CMD. */
    {
        uint16_t n = tfw_frame_encode_event(outbuf, sizeof outbuf, entry_reason,
                                           0, 0u);
        emit(outbuf, n);
    }

    for (;;) {
        uint8_t byte;
        next_t  next;

        if (lim && lim->max_polls != 0u && ++total_polls >= lim->max_polls) {
            return;
        }
        if (!tfw_boot_uart_rx(&byte)) {
            if (lim && lim->idle_polls != 0u && ++idle >= lim->idle_polls) {
                return;
            }
            continue;
        }
        if (lim && lim->idle_bytes != 0u && ++bytes >= lim->idle_bytes) {
            return;
        }
        if (lim && lim->max_bytes != 0u && ++total_bytes >= lim->max_bytes) {
            return;
        }
        if (tfw_framer_push(&framer, byte) != TFW_FRAME_READY) {
            continue;
        }

        next = answer(plat, lim);
        if (next == NEXT_END) {
            return;
        }
        if (next == NEXT_PROGRESS) {
            idle  = 0u;
            bytes = 0u;
        }
    }
}

/*
 * Where the bootloader stays when there is nothing to boot.
 *
 * This is the recovery path and it never times out into a jump. A device whose
 * application failed to verify has no application to fall back to -- see
 * boot/boot_ota.h on why a part with 28 KB of application space has no second
 * slot -- so "wait here until somebody sends a good image" is the only honest
 * behaviour. It is also the only one that keeps the device reachable, which is
 * what an interrupted update needs.
 */
_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t entry_reason)
{
    serve(plat, entry_reason, 0);
    for (;;) { }   /* unreachable: an unbounded serve() does not return */
}

/*
 * The same loop for a device that HAS a good image, entered because the
 * application asked. The announcement carries TFW_OK -- "not a failure; you
 * asked for me" -- where recovery's carries the reason it was refused.
 *
 * The session ends in one of these ways: an OTA_END that verifies (hand over, no
 * return); RESET (watchdog reset, and the request is already spent so the next
 * boot is normal); or it RETURNS -- on any bound in `lim` being spent, or on the
 * first OTA_END that does not verify -- and the caller carries on to the
 * ordinary measure, verify and jump. Returning is what stops an
 * unauthenticated peer parking a healthy device here.
 *
 * WHAT A RETURN COSTS. The fall-through runs the same tfw_boot_evaluate as a cold
 * boot, so tfw-boot never jumps into anything unverified. But the region is
 * single-slot: OTA_DATA erases and rewrites application pages as each page
 * fills, and OTA_BEGIN touches nothing. A session abandoned or failed AFTER at
 * least one page was written has already destroyed the application it replaced,
 * so the fall-through fails measurement and lands in tfw_boot_recovery -- which
 * is unbounded and waits for a good image. That is correct, expected, and the
 * residual risk of having no second slot (boot/boot_ota.h). A session that
 * ended before any page was written leaves the old application intact and it
 * boots.
 */
void tfw_boot_session_open(tfw_boot_platform_t *plat,
                          const tfw_boot_session_limits_t *lim)
{
    serve(plat, TFW_OK, lim);
}
