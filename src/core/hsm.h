/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response engine of docs/hsm-self-defence/ (slice 2): the reader
 * of the signed policy set and script (adr-response-policy-format.md D5, D6)
 * and the decision engine of architecture.md section 5, rules R1 to R7.
 *
 * Portable C with no HAL call (architecture.md question 16, decided): time
 * comes in as an argument, and every action goes out through hsm_port.h, which
 * the SoC implements and a test's mock replaces at link time. Which builds
 * carry it is decided by the object list: today only its tests link
 * src/core/hsm_payload.c and src/core/hsm_engine.c.
 *
 * No dynamic allocation: every bound below is fixed, and a set or script past
 * one is refused whole.
 */
#ifndef TFW_CORE_HSM_H
#define TFW_CORE_HSM_H

#include <stdint.h>

#include "suit.h"
#include "tfw/status.h"

/* The bounds of ADR D5, and the engine's own. */
#define TFW_HSM_POLICIES_MAX 8u   /* policies in a set */
#define TFW_HSM_RULES_MAX    16u  /* rules in a policy */
#define TFW_HSM_ACTIONS_MAX  16u  /* actions in a response or a script */
#define TFW_HSM_EXPECTED_MAX 8u   /* expected values (S3) in a set */
#define TFW_HSM_EXPECTED_LEN 64u  /* bytes in one expected value */
#define TFW_HSM_SCRIPTS_MAX  8u   /* scripts held at once */

/* The signals S1-S6 (architecture.md section 2) and the actions A1-A6
 * (section 3), numbered as ADR D5's CDDL. They extend, never renumber. */
#define TFW_HSM_SIG_TAMPER_PIN     1u
#define TFW_HSM_SIG_AHAB_FAILURE   2u
#define TFW_HSM_SIG_MEASUREMENT    3u
#define TFW_HSM_SIG_IMA_VIOLATION  4u
#define TFW_HSM_SIG_HOTPLUG        5u
#define TFW_HSM_SIG_HEARTBEAT_LOSS 6u

#define TFW_HSM_ACT_PHY_OFF   1u
#define TFW_HSM_ACT_POWER_OFF 2u
#define TFW_HSM_ACT_KEYS_LOCK 3u
#define TFW_HSM_ACT_KEYS_WIPE 4u
#define TFW_HSM_ACT_SHUTDOWN  5u
#define TFW_HSM_ACT_RECORD    6u

/* The model's verdict V. Anything but COMPROMISED is not "compromised". */
#define TFW_HSM_VERDICT_CLEAR       0u
#define TFW_HSM_VERDICT_COMPROMISED 1u

/* rule = [signal, response, wipe: bool]. The response is `n` actions in
 * `body`, or, when `n` is 0, a script-ref: the SHA-256 of a script's payload
 * in `body`. */
typedef struct {
    uint8_t signal;
    uint8_t wipe;
    uint8_t n;
    uint8_t body[TFW_SUIT_DIGEST_LEN];
} tfw_hsm_rule_t;

typedef struct {
    uint8_t        n;
    tfw_hsm_rule_t rule[TFW_HSM_RULES_MAX];
} tfw_hsm_policy_t;

typedef struct {
    uint32_t key;
    uint8_t  n;
    uint8_t  value[TFW_HSM_EXPECTED_LEN];
} tfw_hsm_expected_t;

/* policy-set = {1: 1, 2: [1*8 policy], 3: expected-values, 4: uint, 5: uint} */
typedef struct {
    uint8_t            n;            /* policies; entry 0 the tenant's default */
    uint8_t            n_expected;
    uint32_t           heartbeat_ms; /* S6's period */
    uint32_t           window_ms;    /* the corroboration window W */
    tfw_hsm_policy_t   policy[TFW_HSM_POLICIES_MAX];
    tfw_hsm_expected_t expected[TFW_HSM_EXPECTED_MAX];
} tfw_hsm_set_t;

/* script = [1*16 action], held under the SHA-256 of its payload. */
typedef struct {
    uint8_t n;
    uint8_t act[TFW_HSM_ACTIONS_MAX];
    uint8_t digest[TFW_SUIT_DIGEST_LEN];
} tfw_hsm_script_t;

/* The payload reader. Reads `n` bytes at `p` as ADR D5's policy-set (or D6's
 * script) in deterministic CBOR (RFC 8949 section 4.2.1) and nothing else;
 * `out` is complete only on TFW_OK, and partly written on a refusal.
 *
 *   TFW_ERR_UNSUPPORTED  a type, map key, version, signal or action the CDDL
 *                        lacks; a longer form than the shortest; expected
 *                        values out of order; trailing bytes
 *   TFW_ERR_RANGE        truncated; a count past its bound or below 1; an
 *                        expected value too long; an integer past 32 bits */
tfw_status_t tfw_hsm_read_set(const uint8_t *p, uint16_t n, tfw_hsm_set_t *out);
tfw_status_t tfw_hsm_read_script(const uint8_t *p, uint16_t n, tfw_hsm_script_t *out);

/* The engine's state, all of it in TCM (engineering.md, "Persistence"). A
 * reset clears it: tfw_hsm_init. */
typedef struct {
    tfw_hsm_set_t    set;       /* P's set; no P while set.n is 0 */
    tfw_hsm_set_t    next;      /* the reader's scratch: a refusal leaves `set` */
    tfw_hsm_script_t script[TFW_HSM_SCRIPTS_MAX];
    uint8_t          n_scripts;
    uint8_t          selected;  /* P = set.policy[selected] */
    uint8_t          locked;    /* the lockdown latch (R6) */
    uint8_t          wiped;     /* A4 done and acknowledged */
    uint8_t          verdict;   /* the last V */
    uint8_t          floor_set; /* a verified set, not the built-in minimum, set the floor */
    uint32_t         verdict_at;
    uint32_t         beat_at;
    uint32_t         floor;
    uint8_t          floor_digest[TFW_SUIT_DIGEST_LEN];
    uint32_t         refused;   /* envelopes refused since reset (R7) */
} tfw_hsm_t;

/* Reset: no set, no script, the latch open, the floor at the minimum built
 * into the image (ADR D4). */
void tfw_hsm_init(tfw_hsm_t *h, uint32_t floor_min);

/* An envelope off the channel, verified under the tenant `key` (ADR D7). A
 * policy set at or above the floor replaces the set whole, moves the floor to
 * its sequence number and the selection to entry 0; one equal to a floor a
 * set raised is a re-send, accepted only with that set's manifest digest.
 * Scripts the new set does not name are dropped. A script is held only while
 * the set names its digest. On any refusal nothing changes but the count.
 *
 *   as tfw_suit_read_integrated, then
 *   TFW_ERR_PROTECTED_REGION  a set below the floor, or a different set at it
 *   as tfw_hsm_read_set and tfw_hsm_read_script
 *   TFW_ERR_UNSUPPORTED       a script the set does not name
 *   TFW_ERR_NO_SLOT           a new script with every slot held */
tfw_status_t tfw_hsm_load(tfw_hsm_t *h, const uint8_t *env, uint16_t len,
                          const uint8_t key[TFW_SUIT_KEY_LEN], uint32_t now);

/* The model's selection: an entry of the verified set by index (ADR D4).
 * TFW_ERR_RANGE, and the selection unchanged, past the set or with none. */
tfw_status_t tfw_hsm_select(tfw_hsm_t *h, uint8_t index);

/* The model's verdict at `now`: recorded, never acted on (R4). */
void tfw_hsm_verdict(tfw_hsm_t *h, uint8_t verdict, uint32_t now);

/* A signal at `now`: decides and acts, through hsm_port.h (R1-R3, R5, R6). */
void tfw_hsm_signal(tfw_hsm_t *h, uint8_t signal, uint32_t now);

/* The detector's heartbeat, and the timer that turns its absence past the
 * set's period into S6. With no set there is no period, and no S6 by time. */
void tfw_hsm_heartbeat(tfw_hsm_t *h, uint32_t now);
void tfw_hsm_tick(tfw_hsm_t *h, uint32_t now);

#endif
