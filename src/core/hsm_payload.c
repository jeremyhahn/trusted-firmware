/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The payload reader of docs/hsm-self-defence/ slice 2: ADR D5's policy set
 * and D6's script, from bytes tfw_suit_read_integrated has authenticated. The
 * CDDL is read exactly, in deterministic encoding only (RFC 8949 section
 * 4.2.1): every argument in its shortest form, definite lengths, and the
 * policy set's keys 1 to 5 present once each in ascending order.
 */
#include <string.h>

#include "hsm.h"
#include "suit_cbor.h"

#define SIMPLE_FALSE 20u
#define SIMPLE_TRUE  21u
#define SET_MEMBERS  5u
#define SET_VERSION  1u
#define RULE_MEMBERS 3u

/* The bytes a head takes in its shortest form for argument `v`. */
static uint16_t shortest(uint32_t v)
{
    if (v < 24u) {
        return 1u;
    }
    if (v <= 0xFFu) {
        return 2u;
    }
    return v <= 0xFFFFu ? 3u : 5u;
}

/* One head, refused unless it is in the shortest form. An eight-byte
 * argument is past every bound here: too long a form when it fits four
 * bytes, else past 32 bits (tfw_suit_head). */
static tfw_status_t head(tfw_suit_cur_t *c, uint8_t *major, uint32_t *val)
{
    uint16_t     before = c->n;
    tfw_status_t st;

    if (c->n > 4u && (c->p[0] & 0x1Fu) == 27u
        && (c->p[1] | c->p[2] | c->p[3] | c->p[4]) == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    st = tfw_suit_head(c, major, val);
    if (st == TFW_OK && (uint16_t)(before - c->n) != shortest(*val)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    return st;
}

/* A head of type `major`; its argument in `*val`. */
static tfw_status_t typed(tfw_suit_cur_t *c, uint8_t major, uint32_t *val)
{
    uint8_t      m;
    tfw_status_t st = head(c, &m, val);

    if (st == TFW_OK && m != major) {
        st = TFW_ERR_UNSUPPORTED;
    }
    return st;
}

/* Exactly the unsigned integer `want`: a map key or the version. */
static tfw_status_t exactly(tfw_suit_cur_t *c, uint32_t want)
{
    uint32_t     v;
    tfw_status_t st = typed(c, TFW_SUIT_MT_UINT, &v);

    if (st == TFW_OK && v != want) {
        st = TFW_ERR_UNSUPPORTED;
    }
    return st;
}

/* An array of 1 to `max` members. */
static tfw_status_t array(tfw_suit_cur_t *c, uint8_t max, uint8_t *n)
{
    uint32_t     v  = 0u;
    tfw_status_t st = typed(c, TFW_SUIT_MT_ARRAY, &v);

    if (st == TFW_OK && (v == 0u || v > max)) {
        st = TFW_ERR_RANGE;
    }
    *n = (uint8_t)v;
    return st;
}

/* An array of exactly `members`: a fixed-shape group of the CDDL. */
static tfw_status_t group(tfw_suit_cur_t *c, uint32_t members)
{
    uint32_t     v;
    tfw_status_t st = typed(c, TFW_SUIT_MT_ARRAY, &v);

    if (st == TFW_OK && v != members) {
        st = TFW_ERR_UNSUPPORTED;
    }
    return st;
}

/* A signal S1-S6 or an action A1-A6: both enumerations run 1 to 6. */
static tfw_status_t known(tfw_suit_cur_t *c, uint8_t *out)
{
    uint32_t     v = 0u;
    tfw_status_t st = typed(c, TFW_SUIT_MT_UINT, &v);

    if (st == TFW_OK && (v == 0u || v > 6u)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    *out = (uint8_t)v;
    return st;
}

/* [1*16 action], the body of a response and of a script. */
static tfw_status_t actions(tfw_suit_cur_t *c, uint8_t *n, uint8_t act[TFW_HSM_ACTIONS_MAX])
{
    tfw_status_t st = array(c, TFW_HSM_ACTIONS_MAX, n);

    for (uint8_t i = 0u; st == TFW_OK && i < *n; i++) {
        st = known(c, &act[i]);
    }
    return st;
}

/* bstr of at most `max` bytes, copied to `out`. */
static tfw_status_t bytes(tfw_suit_cur_t *c, uint16_t max, uint8_t *out, uint8_t *n)
{
    uint32_t     v;
    tfw_status_t st = typed(c, TFW_SUIT_MT_BSTR, &v);

    if (st == TFW_OK && (v > max || v > c->n)) {
        st = TFW_ERR_RANGE;
    }
    if (st == TFW_OK) {
        memcpy(out, c->p, v);
        c->p += v;
        c->n  = (uint16_t)(c->n - v);
        *n    = (uint8_t)v;
    }
    return st;
}

/* response = [1*16 action] / script-ref, script-ref = bstr .size 32 */
static tfw_status_t response(tfw_suit_cur_t *c, tfw_hsm_rule_t *r)
{
    uint8_t      len;
    tfw_status_t st;

    if (c->n != 0u && (c->p[0] >> 5) == TFW_SUIT_MT_ARRAY) {
        return actions(c, &r->n, r->body);
    }
    st = bytes(c, TFW_SUIT_DIGEST_LEN, r->body, &len);
    if (st == TFW_OK && len != TFW_SUIT_DIGEST_LEN) {
        st = TFW_ERR_UNSUPPORTED;
    }
    r->n = 0u;
    return st;
}

/* rule = [signal, response, wipe: bool] */
static tfw_status_t rule(tfw_suit_cur_t *c, tfw_hsm_rule_t *r)
{
    uint8_t      m;
    uint32_t     v  = 0u;
    tfw_status_t st = group(c, RULE_MEMBERS);

    if (st == TFW_OK) st = known(c, &r->signal);
    if (st == TFW_OK) st = response(c, r);
    if (st == TFW_OK) st = head(c, &m, &v);
    if (st == TFW_OK && (m != TFW_SUIT_MT_SIMPLE || (v != SIMPLE_FALSE && v != SIMPLE_TRUE))) {
        st = TFW_ERR_UNSUPPORTED;
    }
    r->wipe = (uint8_t)(v == SIMPLE_TRUE);
    return st;
}

/* expected-values = { * uint => bstr }, keys ascending (deterministic order),
 * at most TFW_HSM_EXPECTED_MAX of them. */
static tfw_status_t expected(tfw_suit_cur_t *c, tfw_hsm_set_t *s)
{
    uint32_t     v;
    tfw_status_t st = typed(c, TFW_SUIT_MT_MAP, &v);

    if (st == TFW_OK && v > TFW_HSM_EXPECTED_MAX) {
        st = TFW_ERR_RANGE;
    }
    s->n_expected = (uint8_t)v;
    for (uint8_t i = 0u; st == TFW_OK && i < s->n_expected; i++) {
        tfw_hsm_expected_t *e = &s->expected[i];

        st = typed(c, TFW_SUIT_MT_UINT, &e->key);
        if (st == TFW_OK && i != 0u && e->key <= s->expected[i - 1u].key) {
            st = TFW_ERR_UNSUPPORTED;
        }
        if (st == TFW_OK) st = bytes(c, TFW_HSM_EXPECTED_LEN, e->value, &e->n);
    }
    return st;
}

tfw_status_t tfw_hsm_read_set(const uint8_t *p, uint16_t n, tfw_hsm_set_t *out)
{
    tfw_suit_cur_t c;
    uint32_t       v;
    tfw_status_t   st;

    c.p = p;
    c.n = n;
    st  = typed(&c, TFW_SUIT_MT_MAP, &v);
    if (st == TFW_OK && v != SET_MEMBERS) {
        st = TFW_ERR_UNSUPPORTED;
    }
    if (st == TFW_OK) st = exactly(&c, 1u);
    if (st == TFW_OK) st = exactly(&c, SET_VERSION);
    if (st == TFW_OK) st = exactly(&c, 2u);
    if (st == TFW_OK) st = array(&c, TFW_HSM_POLICIES_MAX, &out->n);
    for (uint8_t i = 0u; st == TFW_OK && i < out->n; i++) {
        tfw_hsm_policy_t *pol = &out->policy[i];

        st = array(&c, TFW_HSM_RULES_MAX, &pol->n);
        for (uint8_t j = 0u; st == TFW_OK && j < pol->n; j++) {
            st = rule(&c, &pol->rule[j]);
        }
    }
    if (st == TFW_OK) st = exactly(&c, 3u);
    if (st == TFW_OK) st = expected(&c, out);
    if (st == TFW_OK) st = exactly(&c, 4u);
    if (st == TFW_OK) st = typed(&c, TFW_SUIT_MT_UINT, &out->heartbeat_ms);
    if (st == TFW_OK) st = exactly(&c, 5u);
    if (st == TFW_OK) st = typed(&c, TFW_SUIT_MT_UINT, &out->window_ms);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

tfw_status_t tfw_hsm_read_script(const uint8_t *p, uint16_t n, tfw_hsm_script_t *out)
{
    tfw_suit_cur_t c;
    tfw_status_t   st;

    c.p = p;
    c.n = n;
    st  = actions(&c, &out->n, out->act);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}
