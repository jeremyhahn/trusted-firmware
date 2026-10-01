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

#include "ag/boot.h"
#include "ag/protocol.h"
#include "../boot_hal.h"
#include "../boot_image.h"
#include "../boot_ota.h"
#include "../boot_platform.h"
#include "../boot_policy.h"
#include "../boot_recovery.h"
#include "../boot_store.h"
#include "../boot_session.h"
#include "frame.h"

static ag_framer_t framer;
static ag_ota_t    ota;
static uint8_t     outbuf[AG_MAX_FRAME];

/* ------------------------------------------------------------------ ota */

static ag_status_t write_page(uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)len;   /* always one whole page; ag_ota pads the tail */
    return ag_boot_flash_write_page(addr, data);
}

static void emit(const uint8_t *b, uint16_t n)
{
    for (uint16_t i = 0u; i < n; i++) {
        ag_boot_uart_tx(b[i]);
    }
}

/* Whether a handled frame proves the peer is making progress on an update.
 * PING and OTA_STATUS are deliberately not progress: they are what a probe
 * sends, and a probe must not be able to hold the session open. */
static uint8_t is_progress(uint8_t cmd, ag_status_t st)
{
    return (uint8_t)(st == AG_OK
                     && (cmd == AG_CMD_OTA_BEGIN || cmd == AG_CMD_OTA_DATA
                         || cmd == AG_CMD_OTA_END));
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
 * ag_boot_evaluate. Without that, a bogus transfer could be followed by another
 * and the idle budgets restarted each time. Ending falls through to the normal
 * measure/verify/jump; see ag_boot_session_open for what that means for a
 * region the failed attempt already overwrote.
 */
static void serve(ag_boot_platform_t *plat, ag_status_t entry_reason,
                  const ag_boot_session_limits_t *lim)
{
    uint32_t idle        = 0u;
    uint32_t bytes       = 0u;
    uint32_t total_polls = 0u;
    uint32_t total_bytes = 0u;

    (void)ag_ota_init(&ota, write_page, ag_boot_flash_page_size());
    ag_framer_init(&framer);

    /* Announce, once, why we are here. A host that sees this knows immediately
     * that it is talking to a bootloader and why, rather than inferring it from
     * a series of AG_ERR_UNKNOWN_CMD. */
    {
        uint16_t n = ag_frame_encode_event(outbuf, sizeof outbuf, entry_reason,
                                           0, 0u);
        emit(outbuf, n);
    }

    for (;;) {
        uint8_t byte;

        if (lim && lim->max_polls != 0u && ++total_polls >= lim->max_polls) {
            return;
        }
        if (!ag_boot_uart_rx(&byte)) {
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
        if (ag_framer_push(&framer, byte) != AG_FRAME_READY) {
            continue;
        }

        {
            uint8_t  resp[AG_MAX_PAYLOAD];
            uint8_t  rlen = 0u;
            uint8_t  cmd  = ag_frame_op(&framer);
            uint8_t  seq  = ag_frame_seq(&framer);
            ag_boot_session_action_t action;
            ag_status_t st;
            uint16_t n;

            st = ag_boot_session_handle(&ota, cmd,
                                        ag_frame_payload(&framer),
                                        ag_frame_payload_len(&framer),
                                        resp, &rlen, &action);

            n = ag_frame_encode_response(outbuf, sizeof outbuf, seq, cmd, st,
                                         resp, rlen);
            emit(outbuf, n);

            if (is_progress(cmd, st)) {
                idle  = 0u;
                bytes = 0u;
            }
            if (action == AG_BOOT_SESSION_RESET) {
                ag_boot_reset();
            }
            if (lim && cmd == AG_CMD_OTA_END && st != AG_OK) {
                return;   /* the one attempt failed before verification */
            }
            if (action != AG_BOOT_SESSION_COMMIT) {
                continue;
            }

            /*
             * COMMIT. Every byte arrived; now find out whether any of it was
             * signed.
             *
             * The same ag_boot_evaluate the cold path runs, against the same
             * floor, with the same ordering. One decision function, so an image
             * that would be refused at boot cannot be accepted at update.
             */
            {
                ag_boot_verdict_t v;
                uint32_t          floor = ag_boot_floor_load(plat);
                uint32_t          next;

                st = ag_boot_evaluate(floor, &v);
                n  = ag_frame_encode_event(outbuf, sizeof outbuf, st, 0, 0u);
                emit(outbuf, n);

                if (st != AG_OK) {
                    /* The application region holds an image that will not
                     * verify. In recovery the loop carries on and takes another
                     * attempt. In a requested session this was the one attempt:
                     * return, and the caller's ag_boot_evaluate -- which will
                     * fail the same way -- lands in recovery if the region was
                     * overwritten. */
                    ag_ota_abort(&ota);
                    plat->ota_state = AG_BOOT_OTA_FAILED;
                    ag_boot_platform_store(plat);
                    if (lim) {
                        return;
                    }
                    continue;
                }

                if (ag_boot_floor_advance(v.trailer.image_version, floor,
                                          &next) == AG_OK) {
                    ag_boot_floor_store(plat, next);
                }
                plat->boot_state = AG_BOOT_STATE_GOOD;
                plat->ota_state  = AG_BOOT_OTA_VERIFIED;
                ag_boot_platform_store(plat);

                ag_boot_hand_over(&v);
            }
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
void ag_boot_recovery(ag_boot_platform_t *plat, ag_status_t entry_reason)
{
    serve(plat, entry_reason, 0);
    for (;;) { }   /* unreachable: an unbounded serve() does not return */
}

/*
 * The same loop for a device that HAS a good image, entered because the
 * application asked. The announcement carries AG_OK -- "not a failure; you
 * asked for me" -- where recovery's carries the reason it was refused.
 *
 * The session ends in one of these ways: an OTA_END that verifies (hand over, no
 * return); RESET (watchdog reset, and the request is already spent so the next
 * boot is normal); or it RETURNS -- on any bound in `lim` being spent, or on the
 * first OTA_END that does not verify -- and the caller carries on to the
 * ordinary measure, verify and jump. Returning is what stops an
 * unauthenticated peer parking a healthy device here.
 *
 * WHAT A RETURN COSTS. The fall-through runs the same ag_boot_evaluate as a cold
 * boot, so ag-boot never jumps into anything unverified. But the region is
 * single-slot: OTA_DATA erases and rewrites application pages as each page
 * fills, and OTA_BEGIN touches nothing. A session abandoned or failed AFTER at
 * least one page was written has already destroyed the application it replaced,
 * so the fall-through fails measurement and lands in ag_boot_recovery -- which
 * is unbounded and waits for a good image. That is correct, expected, and the
 * residual risk of having no second slot (boot/boot_ota.h). A session that
 * ended before any page was written leaves the old application intact and it
 * boots.
 */
void ag_boot_session_open(ag_boot_platform_t *plat,
                          const ag_boot_session_limits_t *lim)
{
    serve(plat, AG_OK, lim);
}
