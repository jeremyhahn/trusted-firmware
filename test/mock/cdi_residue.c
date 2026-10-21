/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* See cdi_residue.h. */
#include <string.h>
#include "cdi_residue.h"
#include "core/secure_zero.h"

#define ZERO_MAX   512u
#define ZERO_COPY  512u
#define STACK_SPAN 16384u
#define PART       16u

static size_t  zero_len[ZERO_MAX];
static uint8_t zero_before[ZERO_MAX][ZERO_COPY];
static int     zero_after_ok[ZERO_MAX];
static unsigned zero_calls;

static uint8_t snap[STACK_SPAN];

void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t            i;
    int               ok = 1;

    if (zero_calls < ZERO_MAX) {
        zero_len[zero_calls] = n;
        memcpy(zero_before[zero_calls], p, n < ZERO_COPY ? n : ZERO_COPY);
    }
    for (i = 0; i < n; i++) { v[i] = 0u; }
    for (i = 0; i < n; i++) { ok = ok && v[i] == 0u; }
    if (zero_calls < ZERO_MAX) {
        zero_after_ok[zero_calls] = ok;
    }
    zero_calls++;
}

void tfw_residue_reset(void)
{
    zero_calls = 0u;
    memset(zero_len, 0, sizeof zero_len);
    memset(zero_after_ok, 0, sizeof zero_after_ok);
}

unsigned tfw_residue_zero_calls(void) { return zero_calls; }

int tfw_residue_in(const void *buf, size_t len, const void *needle, size_t n)
{
    const uint8_t *h = (const uint8_t *)buf;
    const uint8_t *k = (const uint8_t *)needle;
    size_t         part, i;

    for (part = 0; part + PART <= n; part += PART) {
        for (i = 0; i + PART <= len; i++) {
            if (memcmp(&h[i], &k[part], PART) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

int tfw_residue_cleared(const void *needle, size_t n)
{
    unsigned c;

    for (c = 0; c < zero_calls && c < ZERO_MAX; c++) {
        size_t held = zero_len[c] < ZERO_COPY ? zero_len[c] : ZERO_COPY;
        if (zero_after_ok[c] && tfw_residue_in(zero_before[c], held, needle, n)) {
            return 1;
        }
    }
    return 0;
}

int tfw_residue_last_cleared(unsigned back, size_t len, const void *needle, size_t n)
{
    unsigned c;
    size_t   held;

    if (back >= zero_calls || zero_calls > ZERO_MAX) {
        return 0;
    }
    c    = zero_calls - 1u - back;
    held = zero_len[c] < ZERO_COPY ? zero_len[c] : ZERO_COPY;
    return zero_len[c] == len && zero_after_ok[c] &&
           tfw_residue_in(zero_before[c], held, needle, n);
}

/* noinline so its frame sits where the callee's did, and the window is read
 * through a volatile pointer so the copy of uninitialised bytes is kept. */
__attribute__((noinline)) void tfw_residue_snapshot(void)
{
    uint8_t                 window[STACK_SPAN];
    const volatile uint8_t *w = window;
    size_t                  i;

    for (i = 0; i < STACK_SPAN; i++) {
        snap[i] = w[i];
    }
}

int tfw_residue_on_stack(const void *needle, size_t n)
{
    return tfw_residue_in(snap, sizeof snap, needle, n);
}
