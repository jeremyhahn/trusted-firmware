/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure-world DICE service (#590 slice b, ruling 935): the DeviceID key,
 * the alias key and the alias certificate behind four non-secure-callable
 * entry points. docs/plans/590b.md is the contract.
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
/* TFW_NSC, the lengths, tfw_dice_cert_page_t and the four non-secure
 * calls: the API the non-secure image links against. */
#include "tfw/tz_s_api.h"

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE & 2)
#include <arm_cmse.h>
/* A buffer the non-secure caller hands in: it must lie wholly in memory the
 * non-secure world may write (outputs) or read (inputs), or it is refused. */
#define TFW_NS_CHECK(ptr, len) \
    (cmse_check_address_range((void *)(ptr), (len), CMSE_NONSECURE | CMSE_MPU_READWRITE) != NULL)
#define TFW_NS_CHECK_READ(ptr, len) \
    (cmse_check_address_range((void *)(ptr), (len), CMSE_NONSECURE | CMSE_MPU_READ) != NULL)
#else
/* Host: non-NULL, a non-empty length, and no wrap past the address space. */
#define TFW_NS_CHECK(ptr, len) \
    ((ptr) != NULL && (len) != 0u && \
     (uintptr_t)(ptr) <= UINTPTR_MAX - (uintptr_t)((len) - 1u))
#define TFW_NS_CHECK_READ(ptr, len) TFW_NS_CHECK(ptr, len)
#endif

/* What secure boot hands the service. Copied in; the caller zeroes its own. */
typedef struct {
    const uint8_t    *uds;      /* 32 bytes */
    const uint8_t    *cdi;      /* 32 bytes */
    const uint8_t    *ueid;     /* TFW_ATTEST_ID_LEN */
    const uint8_t    *fw_hash;  /* 32 bytes, the alias certificate's claim 273 */
    tfw_crypto_tier_t tier;
} tfw_dice_inputs_t;

/* Secure-only (not NSC). TFW_ERR_RANGE on a NULL input. */
tfw_status_t tfw_dice_service_init(const tfw_dice_inputs_t *in);
/* Secure-only. Zeroes every secret; later calls answer TFW_ERR_NOT_PROVISIONED. */
void tfw_dice_service_clear(void);


#endif
