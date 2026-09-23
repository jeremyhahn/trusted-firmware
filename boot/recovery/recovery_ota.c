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

/*
 * The OTA loop: where the bootloader stays when there is nothing to boot.
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

        if (!ag_boot_uart_rx(&byte)) {
            continue;
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

            if (action == AG_BOOT_SESSION_RESET) {
                ag_boot_reset();
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
                     * verify. boot_state STAYS at STAGED, so the next reset
                     * comes straight back here instead of jumping into it. */
                    ag_ota_abort(&ota);
                    plat->ota_state = AG_BOOT_OTA_FAILED;
                    ag_boot_platform_store(plat);
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

