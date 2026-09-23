/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ag-boot: measure, verify, check the floor, derive the CDI, jump.
 *
 * No interrupts are ever enabled here. The vector table stays pointed at the
 * application section throughout, so there is no IVSEL to set and, more to the
 * point, none to forget to clear -- a bootloader that left the vectors moved
 * would send the application's first timer interrupt into boot-section code
 * that had already been jumped out of.
 *
 * The .init3 early init is SHARED with the application, in
 * src/hal/avr/hal_early.c, rather than reimplemented. Two copies of "clear
 * MCUSR and disarm the watchdog" would be two chances to get the one sequence
 * wrong that turns a part into a brick you need a screwdriver to reach.
 */

#include <string.h>

#include "ag/boot.h"
#include "ag/protocol.h"
#include "boot_cdi.h"
#include "boot_hal.h"
#include "boot_measure.h"
#include "boot_image.h"
#include "boot_ota.h"
#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_session.h"
#include "frame.h"

static ag_framer_t framer;
static ag_ota_t    ota;
static uint8_t     ee[AG_BOOT_EE_LEN];
static uint8_t     outbuf[AG_MAX_FRAME];

/* ------------------------------------------------------------- platform */

static void platform_load(ag_boot_platform_t *p, ag_status_t *st)
{
    *st = ag_boot_eeprom_read(AG_BOOT_EE_BASE, ee, AG_BOOT_EE_LEN);
    if (*st == AG_OK) {
        *st = ag_boot_platform_parse(ee, p);
    }
}

static void platform_store(const ag_boot_platform_t *p)
{
    ag_boot_platform_serialise(p, ee);
    (void)ag_boot_eeprom_write(AG_BOOT_EE_BASE, ee, AG_BOOT_EE_LEN);
}

/*
 * The floor, from whichever authority this build has.
 *
 * The element's counter wins where there is one, because it is the only copy
 * that cannot be made to descend. Without an element the EEPROM copy is all
 * there is, and it is exactly as trustworthy as the BODLEVEL setting that stops
 * a sagging supply from corrupting it -- which is why
 * docs/security/fuses-and-lockbits.md calls brown-out detection a security
 * setting on this part.
 */
static uint32_t floor_load(const ag_boot_platform_t *p)
{
    uint32_t v;

    if (ag_boot_floor_read(&v) == AG_OK) {
        return v;
    }
    return p->rollback_floor;
}

static void floor_store(ag_boot_platform_t *p, uint32_t value)
{
    if (ag_boot_floor_write(value) == AG_OK) {
        return;                       /* the element holds it; EEPROM is moot */
    }
    p->rollback_floor = value;
    platform_store(p);
}

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

/* ------------------------------------------------------------- the boot */

/*
 * Derives the CDI, hands it over and jumps. Does not return.
 *
 * The CDI is built, written and zeroized within a few instructions of each
 * other. There is no MPU on a classic AVR, so this window is the whole
 * mitigation for DICE HW Requirements §6.4 -- which
 * docs/attestation/dice-profiles.md already records as "partial" and which this
 * does not improve on, only honour.
 */
static void hand_over_and_jump(const ag_boot_verdict_t *v)
{
    uint8_t cdi[AG_BOOT_MEASURE_LEN];
    uint8_t block[AG_BOOT_HANDOFF_LEN];

    memset(cdi, 0, sizeof cdi);
    if (ag_boot_cdi_derive(v->measurement, cdi) != AG_OK) {
        /* No UDS: an unprovisioned device. It still boots -- refusing would
         * turn a skipped factory step into a brick -- but the block carries a
         * zero CDI and the tier the bootloader actually found, so the
         * application refuses to attest rather than attesting to nothing. */
        memset(cdi, 0, sizeof cdi);
    }

    ag_boot_handoff_build(block, ag_boot_cdi_tier(), cdi, v->measurement,
                          v->trailer.image_version);
    ag_boot_handoff_write(block, AG_BOOT_HANDOFF_LEN);

    memset(cdi, 0, sizeof cdi);
    memset(block, 0, sizeof block);

    ag_boot_jump_application();
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
static void ota_loop(ag_boot_platform_t *plat, ag_status_t entry_reason)
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
                uint32_t          floor = floor_load(plat);
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
                    platform_store(plat);
                    continue;
                }

                if (ag_boot_floor_advance(v.trailer.image_version, floor,
                                          &next) == AG_OK) {
                    floor_store(plat, next);
                }
                plat->boot_state = AG_BOOT_STATE_GOOD;
                plat->ota_state  = AG_BOOT_OTA_VERIFIED;
                platform_store(plat);

                hand_over_and_jump(&v);
            }
        }
    }
}

int main(void)
{
    ag_boot_platform_t plat;
    ag_boot_verdict_t  verdict;
    ag_status_t        st;
    uint32_t           floor;
    uint32_t           next;

    ag_boot_uart_init(AG_UART_BAUD);
    (void)ag_boot_cdi_init();

    platform_load(&plat, &st);
    if (st != AG_OK) {
        /*
         * No usable reserved region: never provisioned, corrupted, or a layout
         * from the future. Every one of those means there is no pinned public
         * key, so nothing can be verified and nothing may be booted. The OTA
         * loop is reachable and a new image can be sent, but it will not
         * verify either until the region is repaired -- which needs a
         * programmer, by design. The device says which failure it is and waits.
         */
        memset(&plat, 0, sizeof plat);
        ota_loop(&plat, st);
    }

    floor = floor_load(&plat);

    /*
     * An update that started and never finished.
     *
     * The application region is whatever the interrupted write left behind.
     * Jumping into it would execute a half-erased image -- which on AVR is
     * 0xFF, decoded as a long run of SBRS instructions, and the part wanders
     * into whatever happens to be after it. Refuse and stay here.
     */
    if (plat.boot_state == AG_BOOT_STATE_STAGED) {
        ota_loop(&plat, AG_ERR_CRYPTO);
    }

    st = ag_boot_evaluate(floor, &verdict);
    if (st != AG_OK) {
        ota_loop(&plat, st);
    }

    /* The floor advances on a verified boot, not on a verified update. An image
     * that verifies but cannot run leaves the floor where it was, so the
     * previous version is still installable. */
    if (ag_boot_floor_advance(verdict.trailer.image_version, floor,
                              &next) == AG_OK && next != floor) {
        floor_store(&plat, next);
    }

    hand_over_and_jump(&verdict);
    return 0;   /* unreachable; the compiler wants it */
}
