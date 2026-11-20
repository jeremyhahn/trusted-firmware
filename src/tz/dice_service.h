/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure-world DICE service: the DeviceID key, the alias key and the
 * alias certificate. Written for #590 slice b (docs/plans/590b.md, ruling
 * 935) behind four NSC entry points; since #608 slice 3 (ruling 1024 (a)) it
 * is the core of the TF-M Application RoT partition, reached only through
 * src/tz/dice_partition.c's dispatcher. TF-M's SPM checks the non-secure
 * caller's buffers and the dispatcher hands this service partition-local
 * copies, so no CMSE check is made here.
 *
 * The UDS-derived seed, the CDI and every private scalar stay in this
 * service's static state: no entry point returns one. Every caller buffer is
 * checked against the caller's size before anything is written; a refusal
 * leaves the buffer untouched.
 */
#ifndef TFW_TZ_DICE_SERVICE_H
#define TFW_TZ_DICE_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include "tfw/status.h"
#include "tfw/types.h"

#define TFW_DICE_KEY_LEN    64u  /* X||Y */
#define TFW_DICE_SIG_LEN    64u  /* r||s */
#define TFW_DICE_DIGEST_LEN 32u
#define TFW_DICE_NONCE_LEN  32u  /* TFW_ALIAS_NONCE_LEN */
#define TFW_DICE_PAGE_MAX   255u /* the most one tfw_dice_alias_cert call returns */

/* The alias seed is HMAC-SHA256(CDI, TFW_DICE_ALIAS_LABEL): a key apart from
 * the DeviceID key, so no entry point reaches a DeviceID private-key operation. */
#define TFW_DICE_ALIAS_LABEL     "tfw-dice-alias-v1"
#define TFW_DICE_ALIAS_LABEL_LEN 17u

/* What secure boot hands the service. Copied in; the caller zeroes its own. */
typedef struct {
    const uint8_t    *uds;      /* 32 bytes */
    const uint8_t    *cdi;      /* 32 bytes */
    const uint8_t    *ueid;     /* TFW_ATTEST_ID_LEN */
    const uint8_t    *fw_hash;  /* 32 bytes, the alias certificate's claim 273 */
    tfw_crypto_tier_t tier;
} tfw_dice_inputs_t;

/* Partition-only, not a call type. TFW_ERR_RANGE on a NULL input. */
tfw_status_t tfw_dice_service_init(const tfw_dice_inputs_t *in);
/* Partition-only. Zeroes every secret; later calls answer TFW_ERR_NOT_PROVISIONED. */
void tfw_dice_service_clear(void);

/* The service API. Each answers TFW_ERR_RANGE for a NULL or short buffer
 * or a wrong digest length, TFW_ERR_NOT_PROVISIONED before init, and writes
 * nothing unless it answers TFW_OK. */
tfw_status_t tfw_dice_deviceid_pub(uint8_t *out, uint32_t cap);
tfw_status_t tfw_dice_alias_pub(uint8_t *out, uint32_t cap);
/* One page of the alias certificate, request and answer in one struct (the
 * shape the #590 NSC entry point needed, kept for the partition). The caller
 * fills nonce, off, out and cap; on TFW_OK the service has written bytes [off, off+got) to out and set total (the full
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

tfw_status_t tfw_dice_alias_cert(tfw_dice_cert_page_t *page);
tfw_status_t tfw_dice_alias_sign(const uint8_t *digest, uint32_t dlen,
                                 uint8_t *sig, uint32_t cap);

#endif
