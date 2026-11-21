/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Armv8-M Security Extension switch for src/tz/dice_service.h. It lives
 * in the cortex-m HAL because the CMSE test is a fact about the silicon, the
 * one place a preprocessor conditional is allowed (make lint-conditionals).
 */
#ifndef TFW_HAL_CM_NSC_H
#define TFW_HAL_CM_NSC_H

#include <stdint.h>
#include <stddef.h>

/* A non-secure-callable entry point. Under -mcmse the compiler emits the
 * secure gateway and clears the secure registers on return; on host it is a
 * plain function the tests call. */
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE & 2)
#include <arm_cmse.h>
#define TFW_NSC __attribute__((cmse_nonsecure_entry))
/* A buffer the non-secure caller hands in: it must lie wholly in memory the
 * non-secure world may write (outputs) or read (inputs), or it is refused. */
#define TFW_NS_CHECK(ptr, len) \
    (cmse_check_address_range((void *)(ptr), (len), CMSE_NONSECURE | CMSE_MPU_READWRITE) != NULL)
#define TFW_NS_CHECK_READ(ptr, len) \
    (cmse_check_address_range((void *)(ptr), (len), CMSE_NONSECURE | CMSE_MPU_READ) != NULL)
#else
#define TFW_NSC
/* Host: non-NULL, a non-empty length, and no wrap past the address space. */
#define TFW_NS_CHECK(ptr, len) \
    ((ptr) != NULL && (len) != 0u && \
     (uintptr_t)(ptr) <= UINTPTR_MAX - (uintptr_t)((len) - 1u))
#define TFW_NS_CHECK_READ(ptr, len) TFW_NS_CHECK(ptr, len)
#endif

#endif
