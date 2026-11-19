/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure world's NSC entry points (#590, include/tfw/tz_s_api.h). Each is
 * cmse_nonsecure_entry: the compiler gives it the __acle_se_ symbol and the
 * BXNS return that clears the registers, and the secure link places its SG
 * veneer in the NSC region (tz_s.ld) and lists it in the import library.
 * Slice (b) puts the DICE layer behind this API.
 */
#include <arm_cmse.h>
#include "tfw/tz_s_api.h"

uint32_t __attribute__((cmse_nonsecure_entry)) tfw_s_version(void)
{
    return TFW_S_VERSION;
}
