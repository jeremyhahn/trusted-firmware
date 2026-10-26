/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Swap-move of the primary and secondary slots -- see boot_swap.h and
 * docs/ota/architecture.md.
 *
 * The primary is one unit longer than the secondary. The move phase shifts
 * the primary up one unit, top first. The swap phase then, for each unit i,
 * writes primary[i] from secondary[i] (a) and secondary[i] from the moved
 * copy at primary[i + 1] (b). Every write's source is intact until the step
 * after it, so redoing the step the record names is always safe.
 *
 * A unit is TFW_SWAP_UNIT bytes, one SPM page on the 2560, so a 64 KB slot
 * is 256 units and a step fits one byte (512 bytes on an MPS2 board's 128 KiB
 * slot, include/tfw/config.h). The record is a phase byte and a
 * step byte, and every advance rewrites exactly one of them: a reset that
 * tears an EEPROM write leaves the old position or the new one, never a
 * third. That is why sub-steps a and b alternate between two phase values by
 * the step's parity.
 *
 * A swap back runs the same steps with BACK set in the phase, so a resumed
 * one still knows its way. Forward ends SWAPPED, back ends idle.
 */
#include <string.h>

#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_store.h"
#include "boot_swap.h"
#include "boot_verify.h"
#include "crc16.h"

#define UNIT  ((uint16_t)TFW_SWAP_UNIT)
#define UNITS ((uint16_t)(TFW_SWAP_SLOT_LEN / UNIT))
#define PRI   ((uint32_t)TFW_SWAP_PRIMARY_BASE)
#define SEC   ((uint32_t)TFW_SWAP_SECONDARY_BASE)
#define REC   ((uint16_t)TFW_SWAP_RECORD_ADDR)

#define PH_MOVE 0x4Du
#define PH_X    0x58u   /* a on an even step, b on an odd one */
#define PH_Y    0x59u   /* b on an even step, a on an odd one */
#define PH_BACK 0x20u   /* or-ed into M, X and Y on the way back */
#define PH_DONE 0x44u   /* swapped: the image on trial is in the primary */
#define PH_IDLE 0xFFu

_Static_assert(UNITS >= 1u && UNITS <= 256u, "a step must fit the record's byte");
_Static_assert(SEC >= PRI + TFW_SWAP_SLOT_LEN + TFW_SWAP_UNIT,
               "the primary must hold the slot and the move unit");
_Static_assert(TFW_SWAP_UNIT % 128u == 0u && TFW_SWAP_SLOT_LEN % TFW_SWAP_UNIT == 0u,
               "a unit is whole 128-byte reads, and a slot whole units");
/* Below the boot section on the AVR; on an MPS2 board tfw-boot's region is
 * below both slots, in another bank (boards/mps2-an505/board_profile.h). */
_Static_assert(SEC + TFW_SWAP_SLOT_LEN <= TFW_APP_WRITE_END
               && (TFW_APP_WRITE_END <= TFW_BOOT_REGION_BASE
                   || PRI >= TFW_BOOT_REGION_BASE + TFW_BOOT_REGION_SIZE),
               "the secondary must be writable, and clear of the boot section");

static uint8_t unit[TFW_SWAP_UNIT];

/*
 * Progress is a journal of two copies at REC, each {phase, step, sequence,
 * CRC-16 of the three}, written in turn. An EEPROM write erases each byte
 * before it writes it, so a reset in one can leave a byte that is neither the
 * old value nor the new one. Only the copy being written can be torn, and its
 * CRC no longer matches; the other still holds the position before, and that
 * is the one read. The newer of two valid copies wins, by the sequence.
 *
 * Neither copy valid is an EEPROM never written (every byte 0xFF), or one
 * damaged by something other than this code. Either reads idle: nothing is
 * moved on a position that cannot be read. The gate checks the primary as it
 * stands, and a primary a swap left half-moved is refused into recovery,
 * where a new image swaps in whole over it.
 */
#define COPY 5u

static uint8_t jr[COPY];   /* the newest valid copy, as last read or written */
static uint8_t jr_at;      /* the copy it is: 0 or 1 */

static uint8_t valid(const uint8_t *c)
{
    return (uint8_t)(tfw_crc16(c, 3u) == (uint16_t)(c[3] | ((uint16_t)c[4] << 8)));
}

static void load(void)
{
    uint8_t c[2u * COPY];
    uint8_t k;

    memset(c, 0xFF, sizeof c);
    (void)tfw_boot_nvm_read(REC, c, (uint8_t)sizeof c);
    k = (uint8_t)(valid(&c[COPY])
                  && (valid(c) == 0u || (int8_t)(c[COPY + 2u] - c[2]) > 0));
    memcpy(jr, &c[k * COPY], COPY);
    jr_at = k;
    if (valid(jr) == 0u) {
        jr[0] = PH_IDLE;
    }
}

/* The other copy, one sequence on, and read back: the position before stays
 * readable until the new one is, and a write that did not land is an error. */
static tfw_status_t put(uint8_t ph, uint8_t step)
{
    uint8_t      c[COPY];
    uint16_t     crc;
    tfw_status_t st;

    c[0] = ph;
    c[1] = step;
    c[2] = (uint8_t)(jr[2] + 1u);
    crc  = tfw_crc16(c, 3u);
    c[3] = (uint8_t)crc;
    c[4] = (uint8_t)(crc >> 8);
    st = tfw_boot_nvm_write((uint16_t)(REC + (uint8_t)(jr_at ^ 1u) * COPY), c, COPY);
    load();
    if (st == TFW_OK && memcmp(jr, c, COPY) != 0) {
        st = TFW_ERR_INTERNAL;
    }
    return st;
}

static tfw_status_t move_unit(uint32_t to, uint32_t from, uint16_t ps)
{
    uint16_t     o;
    tfw_status_t st = TFW_OK;

    for (o = 0u; o < UNIT; o = (uint16_t)(o + 128u)) {
        tfw_boot_flash_read(from + o, &unit[o], 128u);
    }
    for (o = 0u; st == TFW_OK && o < UNIT; o = (uint16_t)(o + ps)) {
        st = tfw_boot_flash_write_page(to + o, &unit[o]);
    }
    return st;
}

static uint8_t record(void)
{
    load();
    return jr[0];
}

static uint8_t moving(uint8_t ph)
{
    ph = (uint8_t)(ph & (uint8_t)~PH_BACK);
    return (uint8_t)(ph == PH_MOVE || ph == PH_X || ph == PH_Y);
}

uint8_t tfw_boot_swap_in_progress(void)
{
    return moving(record());
}

tfw_status_t tfw_boot_swap(void)
{
    uint16_t     ps = tfw_boot_flash_page_size();
    uint8_t      ph;
    uint8_t      i;
    tfw_status_t st = TFW_OK;

    if (ps == 0u || ps > UNIT || (UNIT % ps) != 0u) {
        return TFW_ERR_RANGE;
    }
    ph = record();
    i  = jr[1];
    if (moving(ph) == 0u) {
        ph = (uint8_t)(PH_MOVE | ((ph == PH_DONE) ? PH_BACK : 0u));
        i  = (uint8_t)(UNITS - 1u);
        st = put(ph, i);
    }
    while (st == TFW_OK && moving(ph) != 0u) {
        uint8_t  back = (uint8_t)(ph & PH_BACK);
        uint8_t  p    = (uint8_t)(ph & (uint8_t)~PH_BACK);
        uint32_t at   = (uint32_t)i * UNIT;

        if (p == PH_MOVE) {
            st = move_unit(PRI + at + UNIT, PRI + at, ps);
            if (i == 0u) {
                ph = (uint8_t)(PH_X | back);
            } else {
                i--;
            }
        } else if ((p == PH_X) == ((i & 1u) == 0u)) {
            st = move_unit(PRI + at, SEC + at, ps);   /* a */
            ph = (uint8_t)(ph ^ (PH_X ^ PH_Y));
        } else {
            st = move_unit(SEC + at, PRI + at + UNIT, ps);   /* b */
            if ((uint16_t)i + 1u == UNITS) {
                ph = (back != 0u) ? PH_IDLE : PH_DONE;
            } else {
                i++;
            }
        }
        if (st == TFW_OK) {
            st = put(ph, i);
        }
    }
    return st;
}

/* `unconfirmed`: the fall-back from a trial left unconfirmed, whose block write
 * clears TFW_BOOT_EE_FLAG_REFUSED (docs/ota/architecture.md "The fallback
 * flags"), so OTA_INFO reads fell back alone. A refusal's leaves the byte. */
static tfw_status_t fall_back(tfw_boot_platform_t *p, uint8_t unconfirmed)
{
    tfw_status_t st = TFW_ERR_BAD_STATE;

    /* The block first: a reset before the swap back finds GOOD and swapped,
     * and at_reset runs the swap back then. The other order would leave the
     * slots back and the block on trial, which reads as an ordinary failed
     * trial: recovery, with the previous image right there. */
    if (record() == PH_DONE) {
        st = tfw_boot_trial_fall_back(p);
    }
    if (st == TFW_OK) {
        if (unconfirmed != 0u) {
            tfw_boot_platform_store_flags(p, 0u);
        } else {
            tfw_boot_platform_store(p);
        }
        st = tfw_boot_swap();
    }
    return st;
}

tfw_status_t tfw_boot_swap_fall_back(tfw_boot_platform_t *p)
{
    return fall_back(p, 0u);
}

/* OTA_CONFIRM's request (include/tfw/boot.h): whole, and naming the image the
 * primary holds -- its trailer's measurement, which the gate checks again
 * before the image runs. */
static uint8_t confirm_requested(void)
{
    uint8_t        c[TFW_BOOT_CONFIRM_LEN];
    uint8_t        key[TFW_BOOT_PUBKEY_LEN];
    const uint8_t *k = key;
    uint8_t        len;
    tfw_status_t   st;

    (void)tfw_boot_nvm_read(TFW_BOOT_CONFIRM_ADDR, c, TFW_BOOT_CONFIRM_LEN);
    /* unit is free: the swap is done with it. The trailer's signed span, then
     * the Sig_structure built from it. */
    tfw_boot_flash_read(TFW_BOOT_TRAILER_ADDR, unit, TFW_BOOT_TR_SIGNED_LEN);
    if (c[0] != TFW_BOOT_CONFIRM_MAGIC
        || tfw_crc16(c, TFW_BOOT_CONFIRM_O_CRC)
               != (uint16_t)(c[TFW_BOOT_CONFIRM_O_CRC]
                             | ((uint16_t)c[TFW_BOOT_CONFIRM_O_CRC + 1u] << 8))
        || memcmp(&c[TFW_BOOT_CONFIRM_O_MEAS], &unit[TFW_BOOT_TR_MEASUREMENT],
                  TFW_BOOT_CONFIRM_MEAS_LEN) != 0) {
        return 0u;
    }
    /* The row key's signature over OTA_CONFIRM's Sig_structure (#137, #231,
     * docs/ota/api.md): the running image's measurement and image_version,
     * with the key and verifier that check the image. The element keeps its
     * key, as boot_stage.c's authorized() finds. */
    st = tfw_boot_pubkey(key);
    if (st == TFW_ERR_UNSUPPORTED) {
        k = 0;
    } else if (st != TFW_OK) {
        return 0u;
    }
    len = tfw_boot_cose_confirm(&unit[TFW_BOOT_TR_SIGNED_LEN], &unit[TFW_BOOT_TR_MEASUREMENT],
                                tfw_boot_rd_u32(&unit[TFW_BOOT_TR_IMAGE_VERSION]));
    return (uint8_t)(tfw_boot_verify(&unit[TFW_BOOT_TR_SIGNED_LEN], len,
                                     &c[TFW_BOOT_CONFIRM_O_SIG], k) == TFW_OK);
}

/* Cleared once a trial has ended, either way, so no request outlives its trial;
 * written only when set, so an ordinary boot wears nothing. */
static void confirm_clear(void)
{
    uint8_t c = 0xFFu;

    (void)tfw_boot_nvm_read(TFW_BOOT_CONFIRM_ADDR, &c, 1u);
    if (c != 0xFFu) {
        c = 0xFFu;
        (void)tfw_boot_nvm_write(TFW_BOOT_CONFIRM_ADDR, &c, 1u);
    }
}

tfw_boot_reset_action_t tfw_boot_swap_at_reset(tfw_boot_platform_t *p,
                                               tfw_boot_reset_action_t act)
{
    if (tfw_boot_swap_in_progress() != 0u) {
        (void)tfw_boot_swap();
    }
    if (record() != PH_DONE) {
        confirm_clear();
        return act;
    }
    /* A trial the application confirmed (OTA_CONFIRM) settles: CONFIRMED is
     * stored before the request is cleared, so a reset between the two finds
     * CONFIRMED and settles it as before. Without a valid request, TRIAL fails
     * exactly as it always has. */
    if (act == TFW_BOOT_RESET_FAIL && confirm_requested() != 0u
        && tfw_boot_trial_confirm(p) == TFW_OK) {
        tfw_boot_platform_store(p);
        act = TFW_BOOT_RESET_SETTLE;
    }
    confirm_clear();
    if (act == TFW_BOOT_RESET_SETTLE) {
        /* Kept: idle before GOOD is written, so a reset between them settles
         * an ordinary confirmed trial. Not stored, nothing settles: the block
         * stays CONFIRMED at the old floor, and the next reset retries. */
        if (put(PH_IDLE, 0u) != TFW_OK) {
            act = TFW_BOOT_RESET_BOOT;
        }
    } else if (act == TFW_BOOT_RESET_FAIL) {
        if (fall_back(p, 1u) == TFW_OK) {
            act = TFW_BOOT_RESET_BOOT;
        }
    } else if (act == TFW_BOOT_RESET_BOOT) {
        (void)tfw_boot_swap();   /* fell back; the swap back had not begun */
    }
    return act;
}

tfw_status_t tfw_boot_swap_take(tfw_boot_platform_t *p, uint32_t floor)
{
    tfw_boot_verdict_t v;
    tfw_status_t       st;

    if (p->ota_state != TFW_BOOT_OTA_PENDING || record() != PH_IDLE) {
        return TFW_OK;
    }
    /* The whole gate, where the image lies, before a page is written: a
     * refused image never swaps, so a broken or hostile updater cannot wear
     * the flash with images that will not run. It ends its round as a failed
     * trial does, GOOD with FAILED, and the primary runs on. The gate runs
     * again in the primary after the swap, as on every boot. */
    st = tfw_boot_evaluate_at(SEC, floor, &v);
    if (st != TFW_OK) {
        if (tfw_boot_trial_fall_back(p) == TFW_OK) {
            tfw_boot_platform_store(p);
        }
        return st;
    }
    return tfw_boot_swap();
}

tfw_status_t tfw_boot_swap_commit(tfw_boot_platform_t *p, uint32_t floor)
{
    /* Idle only: swapped, the primary holds an image already on trial, and the
     * gate would evaluate that rather than the one just sent. */
    if (record() != PH_IDLE) {
        return TFW_ERR_BAD_STATE;
    }
    /* The primary was never written: the image before this one is still the
     * good one. PENDING is stored before the check, so a reset from here on
     * is main()'s path: checked again, then swapped. */
    p->boot_state = TFW_BOOT_STATE_GOOD;
    p->ota_state  = TFW_BOOT_OTA_PENDING;
    tfw_boot_platform_store(p);
    return tfw_boot_swap_take(p, floor);
}
