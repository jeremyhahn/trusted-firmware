/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "flash_span.h"

void tfw_flash_span(uint32_t addr, uint32_t want, uint32_t cap, tfw_flash_span_t *s)
{
    s->from = addr & ~3u;
    s->skip = addr - s->from;
    s->take = want;
    if (s->take > cap - s->skip) {
        s->take = cap - s->skip;
    }
    s->len = (s->skip + s->take + 3u) & ~3u;
}
