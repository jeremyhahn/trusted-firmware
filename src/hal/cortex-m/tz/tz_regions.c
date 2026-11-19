/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SAU table, from the board's map header (-include tz_map_<board>.h),
 * and its checker (#590). The checker runs on the host (test_tz_regions.c);
 * the secure start-up trusts the table the test passed.
 */
#include "tz_regions.h"

#define TZ_LIMIT(base, size) ((uint32_t)(base) + (uint32_t)(size) - 1u)

const struct tz_sau_region tz_sau_regions[] = {
    { TZ_NS_CODE_BASE,   TZ_LIMIT(TZ_NS_CODE_BASE, TZ_NS_CODE_SIZE),     0 },
    { TZ_NS_RAM_BASE,    TZ_LIMIT(TZ_NS_RAM_BASE, TZ_NS_RAM_SIZE),       0 },
    { TZ_NS_PERIPH_BASE, TZ_LIMIT(TZ_NS_PERIPH_BASE, TZ_NS_PERIPH_SIZE), 0 },
    { TZ_NSC_BASE,       TZ_LIMIT(TZ_NSC_BASE, TZ_NSC_SIZE),             1 },
};

const size_t tz_sau_region_count = sizeof tz_sau_regions / sizeof tz_sau_regions[0];

const struct tz_layout tz_board_layout = {
    TZ_S_CODE_BASE, TZ_S_CODE_SIZE,
    TZ_NSC_BASE, TZ_NSC_SIZE,
    TZ_S_RAM_BASE, TZ_S_RAM_SIZE,
};

/* Whether [b0, l0] and [b1, l1], both inclusive, share a byte. */
static int tz_overlaps(uint32_t b0, uint32_t l0, uint32_t b1, uint32_t l1)
{
    return b0 <= l1 && b1 <= l0;
}

enum tz_check tz_regions_check(const struct tz_sau_region *r, size_t n,
                               const struct tz_layout *l)
{
    size_t i, j, nsc = 0;
    /* The Secure code region holds the NSC region at its end: the secure
     * image's whole flash runs from s_code_base through the NSC's limit. */
    uint32_t s_lim = TZ_LIMIT(l->s_code_base, l->s_code_size);
    uint32_t sec_lim = TZ_LIMIT(l->nsc_base, l->nsc_size);
    uint32_t ram_lim = TZ_LIMIT(l->s_ram_base, l->s_ram_size);

    if (n == 0 || n > TZ_SAU_MAX_REGIONS)
        return TZ_ERR_EMPTY;
    for (i = 0; i < n; i++) {
        if (r[i].limit < r[i].base)
            return TZ_ERR_BOUNDS;
        if (r[i].base % TZ_SAU_GRANULE != 0 || (r[i].limit + 1u) % TZ_SAU_GRANULE != 0)
            return TZ_ERR_ALIGN;
        for (j = i + 1; j < n; j++)
            if (tz_overlaps(r[i].base, r[i].limit, r[j].base, r[j].limit))
                return TZ_ERR_OVERLAP;
    }
    for (i = 0; i < n; i++) {
        if (r[i].nsc) {
            nsc++;
            if (r[i].base < l->s_code_base || r[i].limit > sec_lim ||
                r[i].base <= s_lim)
                return TZ_ERR_NSC_OUTSIDE;
        } else if (tz_overlaps(r[i].base, r[i].limit, l->s_code_base, sec_lim) ||
                   tz_overlaps(r[i].base, r[i].limit, l->s_ram_base, ram_lim)) {
            return TZ_ERR_SECURE_NS;
        }
    }
    return nsc == 1 ? TZ_OK : TZ_ERR_NSC_COUNT;
}
