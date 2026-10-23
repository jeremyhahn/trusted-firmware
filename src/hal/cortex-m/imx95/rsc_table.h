/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The remoteproc resource table of the i.MX 95 M7 image (SPEC row `imx95`).
 *
 * The format is the Linux kernel's, implemented as its documentation defines
 * it and designed in nothing: "Remote Processor Framework", section "Binary
 * Firmware Structure" (Documentation/staging/remoteproc.rst; title and URL in
 * docs/reference/README.md). The table begins with this header, little-endian
 * u32 words with no padding:
 *
 *   offset 0   ver          the version; the kernel's ELF loader accepts 1 only
 *   offset 4   num          the number of resource entries
 *   offset 8   reserved[2]  must be zero
 *   offset 16  offset[num]  each entry's offset from the table's start
 *
 * and the entries follow the offset array. The kernel finds the table by its
 * ELF section's name, `.resource_table`, which mk/arch/cortex-m/imx95.ld
 * places.
 *
 * This image carries no entry, so the table is the header alone and num is 0.
 * rsc_table.c says why each entry type is absent.
 */
#ifndef TFW_CM_IMX95_RSC_TABLE_H
#define TFW_CM_IMX95_RSC_TABLE_H

#include <stdint.h>

#define TFW_IMX95_RSC_VERSION 1u

/* struct resource_table with an empty offset[]: ISO C has no zero-length
 * array, and an image with entries gives the array its length here. */
struct tfw_imx95_rsc_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
};

_Static_assert(sizeof(struct tfw_imx95_rsc_table) == 16u,
               "the resource table header is four u32 words, unpadded");

extern const struct tfw_imx95_rsc_table tfw_imx95_rsc_table;

#endif /* TFW_CM_IMX95_RSC_TABLE_H */
