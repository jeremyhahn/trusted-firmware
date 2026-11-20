/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure world's non-secure-callable API (#590). Each function is an
 * NSC veneer in the secure image (src/hal/cortex-m/tz/tz_veneer.c); the
 * non-secure image links the import library the secure link writes
 * (--cmse-implib), so it calls the SG stub, never secure code directly.
 */
#ifndef TFW_TZ_S_API_H
#define TFW_TZ_S_API_H

#include <stdint.h>

/* The secure API's version, major in the high half. */
#define TFW_S_VERSION 0x00010000u

uint32_t tfw_s_version(void);

#endif
