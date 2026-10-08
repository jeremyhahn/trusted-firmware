/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "dispatch.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "../caps/sys.h"
#include "../caps/gpio.h"
#include "../caps/sched.h"
#include "../caps/analog.h"
#include "../caps/pwm.h"
#include "../caps/pulse.h"
#include "../caps/bus.h"
#include "../caps/provision.h"
#include "../caps/storage.h"
#include "../caps/attest.h"
#include "../caps/alias.h"
#include "auth_session.h"

typedef struct {
    uint8_t          id;
    tfw_cmd_handler_t fn;
    uint8_t          min_payload;
    uint8_t          max_payload;
} tfw_cmd_entry_t;

/* Built from the one table. A linear scan rather than a 256-entry jump table:
 * the table is small, the scan is a handful of comparisons, and a sparse array
 * indexed by command id would cost 256 pointers of flash to save them. */
#define TFW_CMD_ENTRY(id, name, handler, lo, hi) { (id), (handler), (lo), (hi) },
static const tfw_cmd_entry_t TABLE[] = {
#include "commands.def"
};
#undef TFW_CMD_ENTRY

#define TABLE_LEN ((uint8_t)(sizeof TABLE / sizeof TABLE[0]))

static const tfw_cmd_entry_t *find(uint8_t cmd)
{
    uint8_t i;
    for (i = 0u; i < TABLE_LEN; i++) {
        if (TABLE[i].id == cmd) {
            return &TABLE[i];
        }
    }
    return 0;
}

uint8_t tfw_dispatch_knows(uint8_t cmd)
{
    return (uint8_t)(find(cmd) != 0);
}

tfw_status_t tfw_dispatch(uint8_t cmd, tfw_reader_t *r, tfw_writer_t *w)
{
    const tfw_cmd_entry_t *e = find(cmd);
    uint8_t               n;
    tfw_status_t           st;

    if (!e) {
        return TFW_ERR_UNKNOWN_CMD;
    }

    /* Arity is checked HERE, once, for every command. A handler that had to
     * check its own would eventually have one that did not, and the symptom
     * would be a read past the payload rather than a clean refusal. */
    n = tfw_reader_remaining(r);
    if (n < e->min_payload || n > e->max_payload) {
        return TFW_ERR_BAD_ARGC;
    }

    st = e->fn(r, w);
    if (st != TFW_OK) {
        return st;
    }

    /* A handler can be given a well-formed request and still not fit its reply
     * in the buffer. Checked once, here, rather than in every handler -- which
     * is the point of the writer's sticky overflow. */
    if (!tfw_writer_ok(w)) {
        return TFW_ERR_TOO_LONG;
    }
    /* Likewise, a handler that read past its own arguments has misparsed them
     * and whatever it computed is not trustworthy. */
    if (!tfw_reader_ok(r)) {
        return TFW_ERR_BAD_ARGC;
    }
    return TFW_OK;
}
