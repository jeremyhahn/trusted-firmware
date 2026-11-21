/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SAU region table of a TZ=1 build (#590) and the checks it must pass.
 * tz_regions.c builds the table from the board's tz_map_<board>.h (passed
 * with -include); the secure start-up programs the SAU from it, and
 * test/unit/test_tz_regions.c checks it on the host.
 */
#ifndef TFW_TZ_REGIONS_H
#define TFW_TZ_REGIONS_H

#include <stddef.h>
#include <stdint.h>

/* One SAU region: base and limit inclusive, the SAU's 32-byte granule. */
struct tz_sau_region {
    uint32_t base;
    uint32_t limit;
    uint8_t  nsc;   /* 1: Non-secure callable, 0: Non-secure */
};

/* The board's Secure memory, which no region may make Non-secure. */
struct tz_layout {
    uint32_t s_code_base, s_code_size;
    uint32_t nsc_base, nsc_size;
    uint32_t s_ram_base, s_ram_size;
};

#define TZ_SAU_GRANULE 32u

extern const struct tz_sau_region tz_sau_regions[];
extern const size_t tz_sau_region_count;
extern const struct tz_layout tz_board_layout;

enum tz_check {
    TZ_OK = 0,
    TZ_ERR_EMPTY,       /* no region, or more than the SAU has */
    TZ_ERR_ALIGN,       /* a base or limit + 1 not on a 32-byte granule */
    TZ_ERR_BOUNDS,      /* a limit below its base */
    TZ_ERR_OVERLAP,     /* two regions share a byte */
    TZ_ERR_NSC_OUTSIDE, /* an NSC region not inside the Secure code */
    TZ_ERR_NSC_COUNT,   /* not exactly one NSC region */
    TZ_ERR_SECURE_NS,   /* an NS region over Secure code or Secure RAM */
};

/* The SAU's region count on the M33 (QEMU and the STM32H5 have 8). */
#define TZ_SAU_MAX_REGIONS 8u

enum tz_check tz_regions_check(const struct tz_sau_region *r, size_t n,
                               const struct tz_layout *l);

#endif
