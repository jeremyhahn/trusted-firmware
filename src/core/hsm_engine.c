/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The decision engine of docs/hsm-self-defence/ slice 2: architecture.md
 * section 5's table and rules R1 to R7, and ADR D4's floor, D7's order.
 *
 * A decision is taken at each signal, from the selected policy P of the
 * verified set, the last verdict V and its age against the window W. Lockdown
 * is A1, A2, A3 then A6 and sets the latch; nothing here clears it but
 * tfw_hsm_init, the reset.
 */
#include <string.h>

#include "hsm.h"
#include "hsm_port.h"

void tfw_hsm_init(tfw_hsm_t *h, uint32_t floor_min)
{
    memset(h, 0, sizeof *h);
    h->verdict = TFW_HSM_VERDICT_CLEAR;
    h->floor   = floor_min;
}

/* Lockdown, latched (R6). An action the SoC cannot perform yet does not stop
 * the ones after it. */
static void lockdown(tfw_hsm_t *h)
{
    static const uint8_t seq[] = { TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_POWER_OFF,
                                   TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_RECORD };

    for (uint8_t i = 0u; i < sizeof seq; i++) {
        (void)tfw_hsm_port_act(seq[i]);
    }
    h->locked = 1u;
}

/* P's actions in order, the wipe excluded: R3 alone wipes. */
static void run(const uint8_t *act, uint8_t n)
{
    for (uint8_t i = 0u; i < n; i++) {
        if (act[i] != TFW_HSM_ACT_KEYS_WIPE) {
            (void)tfw_hsm_port_act(act[i]);
        }
    }
}

/* V is "compromised" and was received within W of `now`. */
static uint8_t corroborated(const tfw_hsm_t *h, uint32_t now)
{
    return (uint8_t)(h->verdict == TFW_HSM_VERDICT_COMPROMISED
                     && (uint32_t)(now - h->verdict_at) <= h->set.window_ms);
}

/* The first rule of P naming `signal`, or none. */
static const tfw_hsm_rule_t *rule_for(const tfw_hsm_t *h, uint8_t signal)
{
    const tfw_hsm_policy_t *p = &h->set.policy[h->selected];

    for (uint8_t i = 0u; i < p->n; i++) {
        if (p->rule[i].signal == signal) {
            return &p->rule[i];
        }
    }
    return NULL;
}

/* The held script with this digest, or none. */
static const tfw_hsm_script_t *script_for(const tfw_hsm_t *h, const uint8_t *digest)
{
    for (uint8_t i = 0u; i < h->n_scripts; i++) {
        if (memcmp(h->script[i].digest, digest, TFW_SUIT_DIGEST_LEN) == 0) {
            return &h->script[i];
        }
    }
    return NULL;
}

/* Some rule of some policy of `s` names the script with this digest. */
static uint8_t named(const tfw_hsm_set_t *s, const uint8_t *digest)
{
    for (uint8_t i = 0u; i < s->n; i++) {
        for (uint8_t j = 0u; j < s->policy[i].n; j++) {
            const tfw_hsm_rule_t *r = &s->policy[i].rule[j];

            if (r->n == 0u && memcmp(r->body, digest, TFW_SUIT_DIGEST_LEN) == 0) {
                return 1u;
            }
        }
    }
    return 0u;
}

void tfw_hsm_signal(tfw_hsm_t *h, uint8_t signal, uint32_t now)
{
    const tfw_hsm_rule_t   *r;
    const tfw_hsm_script_t *sc;

    /* R1: no P, the built-in default. R5: heartbeat loss, whatever P says. */
    if (h->set.n == 0u || signal == TFW_HSM_SIG_HEARTBEAT_LOSS) {
        lockdown(h);
        return;
    }
    /* R2: a signal P does not name. P names only S1-S6 (the reader admits no
     * other), so a rule found is R3's deterministic signal. */
    r = rule_for(h, signal);
    if (r == NULL) {
        lockdown(h);
        return;
    }
    /* R3: the wipe, counted done only once acknowledged, then lockdown. */
    if (r->wipe != 0u && corroborated(h, now) != 0u) {
        if (tfw_hsm_port_act(TFW_HSM_ACT_KEYS_WIPE) == TFW_OK) {
            h->wiped = 1u;
        }
        lockdown(h);
        return;
    }
    /* R2: P's actions, or its script's; a script the engine does not hold
     * locks down. */
    if (r->n != 0u) {
        run(r->body, r->n);
        return;
    }
    sc = script_for(h, r->body);
    if (sc == NULL) {
        lockdown(h);
        return;
    }
    run(sc->act, sc->n);
}

void tfw_hsm_verdict(tfw_hsm_t *h, uint8_t verdict, uint32_t now)
{
    h->verdict    = verdict;
    h->verdict_at = now;
}

tfw_status_t tfw_hsm_select(tfw_hsm_t *h, uint8_t index)
{
    if (index >= h->set.n) {
        return TFW_ERR_RANGE;
    }
    h->selected = index;
    return TFW_OK;
}

void tfw_hsm_heartbeat(tfw_hsm_t *h, uint32_t now)
{
    h->beat_at = now;
}

void tfw_hsm_tick(tfw_hsm_t *h, uint32_t now)
{
    if (h->locked == 0u && h->set.n != 0u
        && (uint32_t)(now - h->beat_at) > h->set.heartbeat_ms) {
        tfw_hsm_signal(h, TFW_HSM_SIG_HEARTBEAT_LOSS, now);
    }
}

/* D7 steps 6 to 8 for a policy set: the floor, the payload, then the swap. */
static tfw_status_t load_set(tfw_hsm_t *h, const uint8_t *env, uint16_t len,
                             const tfw_suit_manifest_t *m, const uint8_t *payload, uint32_t now)
{
    uint8_t      dg[TFW_SUIT_DIGEST_LEN];
    uint8_t      kept = 0u;
    tfw_status_t st;

    if (m->seq < h->floor) {
        return TFW_ERR_PROTECTED_REGION;
    }
    st = tfw_suit_integrated_digest(env, len, dg);
    if (st != TFW_OK) {
        return st;
    }
    if (m->seq == h->floor && h->floor_set != 0u
        && memcmp(dg, h->floor_digest, TFW_SUIT_DIGEST_LEN) != 0) {
        return TFW_ERR_PROTECTED_REGION;
    }
    st = tfw_hsm_read_set(payload, (uint16_t)m->image_size, &h->next);
    if (st != TFW_OK) {
        return st;
    }
    h->set       = h->next;
    h->selected  = 0u;
    h->floor     = m->seq;
    h->floor_set = 1u;
    h->beat_at   = now;
    memcpy(h->floor_digest, dg, TFW_SUIT_DIGEST_LEN);
    /* An old script runs only while the current set names it (D4). */
    for (uint8_t i = 0u; i < h->n_scripts; i++) {
        if (named(&h->set, h->script[i].digest) != 0u) {
            h->script[kept++] = h->script[i];
        }
    }
    h->n_scripts = kept;
    return TFW_OK;
}

static tfw_status_t load_script(tfw_hsm_t *h, const tfw_suit_manifest_t *m, const uint8_t *payload)
{
    tfw_hsm_script_t sc;
    tfw_status_t     st;

    if (named(&h->set, m->image_digest) == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (script_for(h, m->image_digest) != NULL) {
        return TFW_OK;
    }
    st = tfw_hsm_read_script(payload, (uint16_t)m->image_size, &sc);
    if (st != TFW_OK) {
        return st;
    }
    if (h->n_scripts == TFW_HSM_SCRIPTS_MAX) {
        return TFW_ERR_NO_SLOT;
    }
    memcpy(sc.digest, m->image_digest, TFW_SUIT_DIGEST_LEN);
    h->script[h->n_scripts++] = sc;
    return TFW_OK;
}

tfw_status_t tfw_hsm_load(tfw_hsm_t *h, const uint8_t *env, uint16_t len,
                          const uint8_t key[TFW_SUIT_KEY_LEN], uint32_t now)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = NULL;
    uint8_t             kind    = 0u;
    tfw_status_t        st;

    st = tfw_suit_read_integrated(env, len, key,
                                  (uint8_t)(TFW_SUIT_KIND_POLICY | TFW_SUIT_KIND_SCRIPT),
                                  &m, &payload, &kind);
    if (st == TFW_OK) {
        st = kind == TFW_SUIT_KIND_POLICY ? load_set(h, env, len, &m, payload, now)
                                          : load_script(h, &m, payload);
    }
    if (st != TFW_OK) {
        h->refused++;
        return st;
    }
    /* S3: the set just loaded carries what this boot should measure; a port
     * that cannot measure yet raises nothing. */
    if (kind == TFW_SUIT_KIND_POLICY && h->set.n_expected != 0u) {
        tfw_status_t ms = tfw_hsm_port_measure(&h->set);
        if (ms != TFW_OK && ms != TFW_ERR_UNSUPPORTED) {
            tfw_hsm_signal(h, TFW_HSM_SIG_MEASUREMENT, now);
        }
    }
    return TFW_OK;
}
