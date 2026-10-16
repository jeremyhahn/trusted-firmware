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
#include "boot_stage.h"
#include "boot_swap.h"
#include "boot_store.h"
#include "boot_tick.h"
#include "boot_trial.h"
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
     * The boot HAL's reset cause is the HAL's OWN latch, taken before ITS OWN
     * .init3 cleared the register -- the same three instructions this image
     * and the application both run. Asking it here rather than re-reading the
     * register is the point: by the time main() runs the register has already
     * been cleared once, by us, and issue #18 is exactly the bug of something
     * -- a bootloader, ours or the stock one -- doing that before the value
     * was carried anywhere. What crosses to the application is the abstract
     * cause, never the register image: the application decodes nothing.
     */
    tfw_boot_handoff_build(block, tfw_boot_cdi_tier(), cdi, v->measurement,
                          v->trailer.image_version, tfw_boot_reset_cause());
    tfw_boot_handoff_write(block, TFW_BOOT_HANDOFF_LEN);

    tfw_secure_zero(cdi, sizeof cdi);
    tfw_secure_zero(block, sizeof block);

    tfw_boot_jump_application();
}

/*
 * Ends the trial, if there is one to end, and goes to recovery with `st`.
 *
 * Both ways a trial ends at boot -- a reset that found it still running, and a
 * pending image that did not verify -- store TRIAL, FAILED and stop. One tail
 * for the two, because the boot section has no room for the second copy.
 * Anything else (a GOOD image that no longer verifies) is not a trial: nothing
 * is written, and recovery reports why.
 */
static _Noreturn void end_trial(tfw_boot_platform_t *plat, tfw_status_t st)
{
    if (tfw_boot_trial_fail(plat) == TFW_OK) {
        tfw_boot_platform_store(plat);
    }
    tfw_boot_recovery(plat, st);
}

int main(void)
{
    tfw_boot_platform_t plat;
    tfw_boot_verdict_t  verdict;
    tfw_status_t        st;
    uint32_t           floor;
    uint8_t            update;
    tfw_boot_reset_action_t act;

    /*
     * The application's one-shot update request, taken before ANYTHING else
     * can fail or reset: take clears it unconditionally, so whatever happens
     * from here on -- a refused image, a session abandoned, a power cut -- the
     * next reset starts from nothing. The boot HAL's reset cause is how a
     * request is told apart from garbage left by a power cycle: only a
     * watchdog reset honours it.
     */
    update = tfw_boot_request_take(tfw_boot_reset_cause());

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

    /*
     * The one trial (decision 9), decided before anything else is looked at.
     * Each case below writes the platform block once; its comment says what a
     * reset in the middle of that write leaves. On the soft tier a torn write
     * of the block fails its CRC, and the next reset finds no usable region:
     * recovery, which refuses to overwrite it (tfw_boot_platform_store).
     */
    /* On a board with two slots (the 2560), a swap a reset cut short is
     * finished first, and a swapped trial that ended goes back to the image
     * before it rather than to recovery (boot_swap.h). Elsewhere, unchanged. */
    act = tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
    if (act == TFW_BOOT_RESET_FAIL) {
        /*
         * A reset found the trial still running: the image never confirmed --
         * it hung, was reset by the watchdog, lost power or chose not to. It
         * does not run again; there is no second trial and no previous image
         * to go back to on the in-place path, so recovery: another OTA round.
         * A reset before or during this write repeats it (fail is idempotent).
         */
        end_trial(&plat, TFW_ERR_BAD_STATE);
    }

    /*
     * The floor everything below is checked against. An element that is there
     * and cannot be read leaves no floor to check against: the EEPROM copy is
     * not the authority on that build, so it is not used in its place. Nothing
     * is verified and nothing runs -- recovery, reporting TFW_ERR_CRYPTO, where
     * every image is refused for the same reason until the read succeeds; a
     * RESET command reads it again. The block is not written here, so a
     * CONFIRMED trial is still there to settle when it does -- and recovery
     * refuses OTA_BEGIN over it (tfw_boot_trial_begin_permit) until then.
     */
    st = tfw_boot_floor_load(&plat, &floor);
    if (st != TFW_OK) {
        tfw_boot_recovery(&plat, st);
    }
    if (act == TFW_BOOT_RESET_SETTLE) {
        /*
         * The trial was confirmed. The floor advances now, to the version of
         * the image that proved it runs, never at staging or on trial
          * (decision 5), and GOOD is written only once the authority holds it.
         * On the element tier the counter is written first and the block
         * second: a reset before or during the counter write finds CONFIRMED
         * again and carries the counter on from wherever it stopped; a reset
         * between them finds CONFIRMED again, the advance is then no step, and
         * only GOOD is written. A counter write the element refuses writes
         * nothing: the block stays CONFIRMED, this boot goes on with the image
         * it verified against the old floor, and the next reset retries the
         * advance. It opens no update session either: the session evaluates
         * against the floor the element still holds, below the confirmed
         * version, and an older image could slip in before the advance. The
         * request is dropped; the application asks again once it has settled.
         * On the soft tier the floor and GOOD are one write of the block.
         */
        st = tfw_boot_evaluate(floor, &verdict);
        if (st != TFW_OK) {
            tfw_boot_recovery(&plat, st);
        }
        if (tfw_boot_settle(&plat, verdict.trailer.image_version, &floor) != TFW_OK) {
            update = 0u;
        }
    }

    /*
     * A healthy device that was asked to take an update. Reached only after the
     * platform block loaded and no trial is left to end -- both of those are
     * recovery already, which serves the same loop without a limit.
     *
     * This returns when the session times out, and everything below then runs
     * as it would have: measure, verify, jump. Asking for a session therefore
     * grants nothing; an image delivered in it is evaluated by the same
     * tfw_boot_evaluate as one found at reset, and one that verifies runs only
     * on trial.
     */
    /*
     * An image the application staged in the external store (issue #75). Read,
     * checked again and copied in here, before a session and before the STAGED
     * check below: a copy a reset cut short left STAGED, and is redone from
     * the store rather than sent to recovery. One that copies leaves PENDING,
     * and goes to tfw_boot_evaluate and its one trial below like any other.
     * Nothing staged, or nothing that checks, copies nothing and boots on.
     */
    (void)tfw_boot_stage_take(&plat, floor);

    if (update) {
        /* Field by field, not an initialiser: on AVR a constant aggregate is
         * emitted as initialised data in RAM, and tfw-boot has no .data copy --
         * so the table would read as zeros, and zero disables every bound. */
        tfw_boot_session_limits_t lim;

        lim.idle_polls = TFW_BOOT_SESSION_IDLE_POLLS(TFW_BOOT_TICK_HZ);
        lim.idle_bytes = TFW_BOOT_SESSION_IDLE_BYTES(TFW_UART_BAUD);
        lim.max_polls  = TFW_BOOT_SESSION_MAX_POLLS(TFW_BOOT_TICK_HZ);
        lim.max_bytes  = TFW_BOOT_SESSION_MAX_BYTES;
        tfw_boot_session_open(&plat, &lim);
    }

    /*
     * An update that started and was never armed for its trial.
     *
     * STAGED is stored before the first page is written, so the application
     * region is whatever the interrupted write left behind -- or a whole image
     * that verifies but never ran on trial. Jumping into the first would
     * execute a half-erased image -- which on AVR is 0xFF, decoded as a long
     * run of SBRS instructions, and the part wanders into whatever happens to
     * be after it. Jumping into the second would accept it untried. Checked
     * after the session, which may itself have left one of these behind.
     */
    if (plat.boot_state == TFW_BOOT_STATE_STAGED) {
        tfw_boot_recovery(&plat, TFW_ERR_CRYPTO);
    }

    /* A pending image in the secondary is checked there and, passed, swapped
     * into the primary, where the gate evaluates it. Refused in the secondary,
     * nothing is swapped; refused in the primary, the slots swap back. Either
     * way the previous image is evaluated and booted as it was. */
    (void)tfw_boot_swap_take(&plat, floor);
    st = tfw_boot_evaluate(floor, &verdict);
    if (st != TFW_OK && tfw_boot_swap_fall_back(&plat) == TFW_OK) {
        st = tfw_boot_evaluate(floor, &verdict);
    }
    if (st != TFW_OK) {
        /* A pending image that does not verify gets no trial: TRIAL, FAILED,
         * as an unconfirmed trial ends, so no later reset boots it -- not even
         * one whose evaluation passes because the failure was transient. A
         * reset before this write leaves PENDING: evaluated again, and at
         * most tried, never booted as accepted. */
        end_trial(&plat, st);
    }

    /*
     * A pending image that verified: armed for its one trial before the jump.
     * A reset before this write leaves PENDING (tried again, as if never
     * reached); after it, TRIAL (the trial ended, unconfirmed).
     */
    if (tfw_boot_trial_arm(&plat) == TFW_OK) {
        tfw_boot_platform_store(&plat);
        tfw_boot_watchdog_arm();
    }

    tfw_boot_hand_over(&verdict);
    return 0;   /* unreachable; the compiler wants it */
}
