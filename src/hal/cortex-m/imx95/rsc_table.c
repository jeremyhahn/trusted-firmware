/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The remoteproc resource table (rsc_table.h): what Linux's remoteproc on the
 * A55 reads from the image before it loads the M7's TCM and starts the core.
 * The boot container (AHAB) ignores it: to that loader it is 16 bytes of the
 * image like any other.
 *
 * No entry, because the image needs none of the four types the kernel
 * defines (remoteproc.rst, enum fw_resource_type):
 *
 *   RSC_CARVEOUT, RSC_DEVMEM  the image runs from the TCM, which the i.MX
 *                             remoteproc driver maps from its own address
 *                             table, and drives its peripherals at their
 *                             physical addresses: it asks the kernel to
 *                             allocate or map nothing;
 *   RSC_TRACE                 the image writes no log: its console is the wire
 *                             protocol on the LPUART;
 *   RSC_VDEV                  the image speaks no rpmsg: nothing in row
 *                             `imx95` talks to the A55 through virtio.
 *
 * The table is in no other image: this file is in src/hal/cortex-m/imx95/,
 * which only an i.MX 95 board links, and no code reads it. `used` and the
 * linker script's KEEP hold it through -flto and --gc-sections.
 */

#include <stdint.h>

#include "rsc_table.h"

const struct tfw_imx95_rsc_table tfw_imx95_rsc_table
    __attribute__((section(".resource_table"), used, aligned(4))) = {
    TFW_IMX95_RSC_VERSION,   /* ver */
    0u,                      /* num: no entry */
    { 0u, 0u },              /* reserved */
};
