/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "dispatch.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "../caps/sys.h"
#include "../caps/gpio.h"
#include "../caps/sched.h"
#include "../caps/analog.h"
#include "../caps/storage.h"

typedef struct {
    uint8_t          id;
    ag_cmd_handler_t fn;
    uint8_t          min_payload;
    uint8_t          max_payload;
} ag_cmd_entry_t;

/* Built from the one table. A linear scan rather than a 256-entry jump table:
 * the table is small, the scan is a handful of comparisons, and a sparse array
 * indexed by command id would cost 256 pointers of flash to save them. */
#define AG_CMD_ENTRY(id, name, handler, lo, hi) { (id), (handler), (lo), (hi) },
static const ag_cmd_entry_t TABLE[] = {
#include "commands.def"
};
#undef AG_CMD_ENTRY

#define TABLE_LEN ((uint8_t)(sizeof TABLE / sizeof TABLE[0]))

static const ag_cmd_entry_t *find(uint8_t cmd)
{
    uint8_t i;
    for (i = 0u; i < TABLE_LEN; i++) {
        if (TABLE[i].id == cmd) {
            return &TABLE[i];
        }
    }
    return 0;
}

uint8_t ag_dispatch_knows(uint8_t cmd)
{
    return (uint8_t)(find(cmd) != 0);
}

ag_status_t ag_dispatch(uint8_t cmd, ag_reader_t *r, ag_writer_t *w)
{
    const ag_cmd_entry_t *e = find(cmd);
    uint8_t               n;
    ag_status_t           st;

    if (!e) {
        return AG_ERR_UNKNOWN_CMD;
    }

    /* Arity is checked HERE, once, for every command. A handler that had to
     * check its own would eventually have one that did not, and the symptom
     * would be a read past the payload rather than a clean refusal. */
    n = ag_reader_remaining(r);
    if (n < e->min_payload || n > e->max_payload) {
        return AG_ERR_BAD_ARGC;
    }

    st = e->fn(r, w);
    if (st != AG_OK) {
        return st;
    }

    /* A handler can be given a well-formed request and still not fit its reply
     * in the buffer. Checked once, here, rather than in every handler -- which
     * is the point of the writer's sticky overflow. */
    if (!ag_writer_ok(w)) {
        return AG_ERR_TOO_LONG;
    }
    /* Likewise, a handler that read past its own arguments has misparsed them
     * and whatever it computed is not trustworthy. */
    if (!ag_reader_ok(r)) {
        return AG_ERR_BAD_ARGC;
    }
    return AG_OK;
}
