/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * One slot (BOOT_AB=0): QEMU loads the application and the platform block
 * where they live, so there is nothing to copy in (boot_factory.c).
 */

#include "boot_soc.h"

void tfw_cm_boot_factory(void)
{
}
