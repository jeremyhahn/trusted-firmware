/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot: measure, verify, check the floor, derive the CDI, jump.
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

#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "boot_cdi.h"
#include "boot_hal.h"
#include "boot_measure.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "boot_request.h"
#include "boot_store.h"
#include "hal_early.h"
#include "core/secure_zero.h"


/* ------------------------------------------------------------- platform */

/* ------------------------------------------------------------- the boot */

/*
 * Derives the CDI, hands it over and jumps. Does not return.
 *
 * The CDI is built, written and zeroized within a few instructions of each
 * other. There is no MPU on a classic AVR, so this window is the whole
 * mitigation for DICE HW Requirements §6.4 -- which
 * docs/dice-attestation/architecture.md already records as "partial" and which
 * this does not improve on, only honour.
 */
void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    uint8_t cdi[TFW_BOOT_MEASURE_LEN];
    uint8_t block[TFW_BOOT_HANDOFF_LEN];

    /*
     * tfw_secure_zero, not a plain memset, for every touch of `cdi` in this
     * function -- including this first one. `cdi` is a local: if
     * tfw_boot_cdi_derive fails without writing it (see its own contract),
     * whatever the stack held is what a plain, elidable zero-write might
     * leave behind, and the application would read that as a genuine CDI
     * rather than the zero tfw_boot_cdi_derive's failure is supposed to
     * signal.
     */
    tfw_secure_zero(cdi, sizeof cdi);
    if (tfw_boot_cdi_derive(v->measurement, cdi) != TFW_OK) {
        /* No UDS: an unprovisioned device. It still boots -- refusing would
         * turn a skipped factory step into a brick -- but the block carries a
         * zero CDI and the tier the bootloader actually found, so the
         * application refuses to attest rather than attesting to nothing. */
        tfw_secure_zero(cdi, sizeof cdi);
    }

    /*
     * tfw_hal_early_flags is hal_early.c's OWN latch of MCUSR, taken before
     * ITS OWN .init3 cleared the register -- the same three instructions this
     * image and the application both run, see that file's header. Reading it
     * here rather than re-reading MCUSR is the point: by the time main() runs
     * the register has already been cleared once, by us, and issue #18 is
     * exactly the bug of something -- a bootloader, ours or the stock one --
     * doing that before the value was carried anywhere.
     */
    tfw_boot_handoff_build(block, tfw_boot_cdi_tier(), cdi, v->measurement,
                          v->trailer.image_version, tfw_hal_early_flags);
    tfw_boot_handoff_write(block, TFW_BOOT_HANDOFF_LEN);

    tfw_secure_zero(cdi, sizeof cdi);
    tfw_secure_zero(block, sizeof block);

    tfw_boot_jump_application();
}

int main(void)
{
    tfw_boot_platform_t plat;
    tfw_boot_verdict_t  verdict;
    tfw_status_t        st;
    uint32_t           floor;
    uint32_t           next;
    uint8_t            update;

    /*
     * The application's one-shot update request, taken before ANYTHING else
     * can fail or reset: take clears it unconditionally, so whatever happens
     * from here on -- a refused image, a session abandoned, a power cut -- the
     * next reset starts from nothing. tfw_hal_early_flags is this image's own
     * latch of MCUSR (hal_early.c), which is how a request is told apart from
     * garbage left by a power cycle: only a watchdog reset honours it.
     */
    update = tfw_boot_request_take(tfw_hal_early_flags);

    tfw_boot_uart_init(TFW_UART_BAUD);
    (void)tfw_boot_cdi_init();

    tfw_boot_platform_load(&plat, &st);
    if (st != TFW_OK) {
        /*
         * No usable reserved region: never provisioned, corrupted, or a layout
         * from the future. Every one of those means there is no pinned public
         * key, so nothing can be verified and nothing may be booted. The OTA
         * loop is reachable and a new image can be sent, but it will not
         * verify either until the region is repaired -- which needs a
         * programmer, by design. The device says which failure it is and waits.
         */
        memset(&plat, 0, sizeof plat);
        tfw_boot_recovery(&plat, st);
    }

    floor = tfw_boot_floor_load(&plat);

    /*
     * An update that started and never finished.
     *
     * The application region is whatever the interrupted write left behind.
     * Jumping into it would execute a half-erased image -- which on AVR is
     * 0xFF, decoded as a long run of SBRS instructions, and the part wanders
     * into whatever happens to be after it. Refuse and stay here.
     */
    if (plat.boot_state == TFW_BOOT_STATE_STAGED) {
        tfw_boot_recovery(&plat, TFW_ERR_CRYPTO);
    }

    /*
     * A healthy device that was asked to take an update. Reached only after the
     * platform block loaded and no update is half-done -- both of those are
     * recovery already, which serves the same loop without a limit.
     *
     * This returns when the session times out, and everything below then runs
     * as it would have: measure, verify, floor, jump. Asking for a session
     * therefore grants nothing; an image delivered in it is evaluated by the
     * same tfw_boot_evaluate as one found at reset.
     */
    if (update) {
        /* Field by field, not an initialiser: on AVR a constant aggregate is
         * emitted as initialised data in RAM, and tfw-boot has no .data copy --
         * so the table would read as zeros, and zero disables every bound. */
        tfw_boot_session_limits_t lim;

        lim.idle_polls = TFW_BOOT_SESSION_IDLE_POLLS(F_CPU);
        lim.idle_bytes = TFW_BOOT_SESSION_IDLE_BYTES(TFW_UART_BAUD);
        lim.max_polls  = TFW_BOOT_SESSION_MAX_POLLS(F_CPU);
        lim.max_bytes  = TFW_BOOT_SESSION_MAX_BYTES;
        tfw_boot_session_open(&plat, &lim);
    }

    st = tfw_boot_evaluate(floor, &verdict);
    if (st != TFW_OK) {
        tfw_boot_recovery(&plat, st);
    }

    /* The floor advances on a verified boot, not on a verified update. An image
     * that verifies but cannot run leaves the floor where it was, so the
     * previous version is still installable. */
    if (tfw_boot_floor_advance(verdict.trailer.image_version, floor,
                              &next) == TFW_OK && next != floor) {
        tfw_boot_floor_store(&plat, next);
    }

    tfw_boot_hand_over(&verdict);
    return 0;   /* unreachable; the compiler wants it */
}
