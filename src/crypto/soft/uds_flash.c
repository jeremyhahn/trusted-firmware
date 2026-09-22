/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The device secret, as a real device holds it.
 *
 * In a provisioned deployment this lives in the boot section behind BLB lock
 * bits, where LPM from the application section cannot read it. Until the
 * trusted bootloader exists to place it there, this returns NULL and the
 * provider refuses to sign -- which is the correct behaviour for a device that
 * genuinely has no identity yet, and is a great deal safer than shipping a
 * placeholder that every build of this source would share.
 */

#include <stdint.h>

const uint8_t *ag_crypto_soft_uds(void);

const uint8_t *ag_crypto_soft_uds(void)
{
    return 0;   /* unprovisioned until ag-boot writes one */
}
