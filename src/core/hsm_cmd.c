/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The channel (docs/hsm-self-defence/architecture.md section 2, slice 4): the
 * five commands the A55 sends the M7's response engine, wired to hsm_engine.c
 * and through it to hsm_port.h. The payloads are architecture.md's decisions
 * (section 8): every command first lets the engine see the time, so a late
 * heartbeat is S6 at the next message; every reply that is not a refusal is
 * the engine's state, [flags:1][selected:1][refused:4].
 *
 * The engine is one static tfw_hsm_t, in TCM on the M7. A reset clears it:
 * no P, and R1's built-in default governs until a verified set arrives.
 */

#include <stddef.h>
#include <string.h>

#include "tfw/config.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "hsm.h"
#include "hsm_cmd.h"
#include "hsm_port.h"

static tfw_hsm_t g_hsm;
static uint8_t   g_ready;

/* The envelope HSM_LOAD is reassembling: g_total 0 while none is pending. */
static uint8_t  g_env[TFW_SUIT_INTEGRATED_MAX];
static uint16_t g_total;
static uint16_t g_have;

void tfw_hsm_cmd_reset(void)
{
    tfw_hsm_init(&g_hsm, TFW_HSM_FLOOR_MIN);
    g_ready = 1u;
    g_total = 0u;
    g_have  = 0u;
}

/* The engine, with the time applied: R5 is checked before the message is. */
static tfw_hsm_t *engine(uint32_t now)
{
    if (g_ready == 0u) {
        tfw_hsm_cmd_reset();
    }
    tfw_hsm_tick(&g_hsm, now);
    return &g_hsm;
}

static tfw_status_t state(const tfw_hsm_t *h, tfw_writer_t *w)
{
    uint8_t flags = 0u;

    if (h->set.n != 0u) {
        flags |= TFW_HSM_STATE_SET;
    }
    if (h->locked != 0u) {
        flags |= TFW_HSM_STATE_LOCKED;
    }
    if (h->wiped != 0u) {
        flags |= TFW_HSM_STATE_WIPED;
    }
    tfw_write_u8(w, flags);
    tfw_write_u8(w, h->selected);
    tfw_write_u32(w, h->refused);
    return TFW_OK;
}

tfw_status_t tfw_hsm_cmd_heartbeat(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t   now = tfw_hal_millis();
    tfw_hsm_t *h   = engine(now);

    (void)r;
    tfw_hsm_heartbeat(h, now);
    return state(h, w);
}

/* Only the signals the A55 observes: S4 (IMA) and S5 (hotplug). S1-S3 are the
 * M7's own sensors and S6 its own timer, so a compromised A55 cannot raise
 * them, or the response they name, by claiming them. */
tfw_status_t tfw_hsm_cmd_report(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t   now    = tfw_hal_millis();
    tfw_hsm_t *h      = engine(now);
    uint8_t    signal = tfw_read_u8(r);

    if (signal != TFW_HSM_SIG_IMA_VIOLATION && signal != TFW_HSM_SIG_HOTPLUG) {
        return TFW_ERR_RANGE;
    }
    tfw_hsm_signal(h, signal, now);
    return state(h, w);
}

/* V is clear or compromised; a value past them is refused rather than read as
 * clear, so a verifier with a newer vocabulary is never misunderstood. */
tfw_status_t tfw_hsm_cmd_verdict(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t   now     = tfw_hal_millis();
    tfw_hsm_t *h       = engine(now);
    uint8_t    verdict = tfw_read_u8(r);

    if (verdict > TFW_HSM_VERDICT_COMPROMISED) {
        return TFW_ERR_RANGE;
    }
    tfw_hsm_verdict(h, verdict, now);
    return state(h, w);
}

tfw_status_t tfw_hsm_cmd_select(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_hsm_t   *h  = engine(tfw_hal_millis());
    tfw_status_t st = tfw_hsm_select(h, tfw_read_u8(r));

    return (st == TFW_OK) ? state(h, w) : st;
}

static tfw_status_t drop(tfw_status_t st)
{
    g_total = 0u;
    g_have  = 0u;
    return st;
}

/* [total:2][offset:2][bytes]. Offset 0 begins an envelope; each later chunk
 * follows on at the offset already received, under the same total. The last
 * chunk hands the whole envelope to the engine, whose status is the reply. A
 * chunk out of sequence drops the pending envelope: TFW_ERR_RANGE. */
tfw_status_t tfw_hsm_cmd_load(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t       now    = tfw_hal_millis();
    tfw_hsm_t     *h      = engine(now);
    uint16_t       total  = tfw_read_u16(r);
    uint16_t       offset = tfw_read_u16(r);
    uint8_t        n      = tfw_reader_remaining(r);
    const uint8_t *p      = tfw_read_blob(r, n);
    tfw_status_t   st;

    if (offset == 0u) {
        /* ADR D7 step 1: the envelope fits the buffer, or is refused (R7). */
        if (total == 0u || total > TFW_SUIT_INTEGRATED_MAX) {
            h->refused++;
            return drop(TFW_ERR_RANGE);
        }
        g_total = total;
        g_have  = 0u;
    } else if (g_total == 0u || total != g_total || offset != g_have) {
        return drop(TFW_ERR_RANGE);
    }
    if (n > (uint16_t)(g_total - g_have)) {
        return drop(TFW_ERR_RANGE);
    }
    if (n != 0u) {
        memcpy(&g_env[g_have], p, n);
        g_have = (uint16_t)(g_have + n);
    }
    if (g_have < g_total) {
        return state(h, w);
    }
    st = tfw_hsm_load(h, g_env, g_total, tfw_hsm_port_key(), now);
    (void)drop(TFW_OK);
    return (st == TFW_OK) ? state(h, w) : st;
}
