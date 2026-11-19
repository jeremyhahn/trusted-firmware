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
#include "tfw/status.h"

/* A non-secure-callable entry point. Under -mcmse (the secure image) the
 * compiler emits the secure gateway and clears the secure registers on
 * return; the non-secure image and the host see a plain declaration. */
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE & 2)
#define TFW_NSC __attribute__((cmse_nonsecure_entry))
#else
#define TFW_NSC
#endif

/* The secure API's version, major in the high half. */
#define TFW_S_VERSION 0x00010000u

uint32_t tfw_s_version(void);

/* The DICE service (src/tz/dice_service.c, ruling 935): the DeviceID and
 * alias keys stay in the secure world; these four calls are all the
 * non-secure side sees of them. */
#define TFW_DICE_KEY_LEN    64u  /* X||Y */
#define TFW_DICE_SIG_LEN    64u  /* r||s */
#define TFW_DICE_DIGEST_LEN 32u
#define TFW_DICE_NONCE_LEN  32u  /* TFW_ALIAS_NONCE_LEN */
#define TFW_DICE_PAGE_MAX   255u /* the most one tfw_dice_alias_cert call returns */

/* Each answers TFW_ERR_RANGE for a NULL or short buffer, a buffer outside
 * non-secure memory or a wrong digest length, TFW_ERR_NOT_PROVISIONED before
 * init, and writes nothing unless it answers TFW_OK. */
TFW_NSC tfw_status_t tfw_dice_deviceid_pub(uint8_t *out, uint32_t cap);
TFW_NSC tfw_status_t tfw_dice_alias_pub(uint8_t *out, uint32_t cap);
/* One page of the alias certificate. A struct because an NSC entry point
 * takes at most four register arguments (cmse_nonsecure_entry refuses
 * stack-passed ones). The caller fills nonce, off, out and cap; on TFW_OK the
 * service has written bytes [off, off+got) to out and set total (the full
 * length) and got (<= min(cap, TFW_DICE_PAGE_MAX)). Past the end is
 * TFW_ERR_RANGE. */
typedef struct {
    uint8_t   nonce[TFW_DICE_NONCE_LEN];
    uint32_t  off;
    uint8_t  *out;
    uint32_t  cap;
    uint32_t  total;
    uint32_t  got;
} tfw_dice_cert_page_t;

TFW_NSC tfw_status_t tfw_dice_alias_cert(tfw_dice_cert_page_t *page);
TFW_NSC tfw_status_t tfw_dice_alias_sign(const uint8_t *digest, uint32_t dlen,
                                         uint8_t *sig, uint32_t cap);

#endif
