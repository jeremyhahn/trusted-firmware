/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TA's properties, read by the pinned optee_os's TA dev kit
 * (ta/user_ta_header.c). #defines only, as the design says. Not single-instance,
 * so each session has an instance of its own and the TA keeps no state across
 * sessions; TA_FLAG_MULTI_SESSION sets gpd.ta.multiSession.
 */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <tfw_ta.h>

#define TA_UUID        TFW_TA_UUID
#define TA_FLAGS       TA_FLAG_MULTI_SESSION
#define TA_STACK_SIZE  (2 * 1024)
#define TA_DATA_SIZE   (32 * 1024)
#define TA_VERSION     "1.0"
#define TA_DESCRIPTION "trusted-firmware TA for go-enclave's arm-trustzone backend"

#endif /* USER_TA_HEADER_DEFINES_H */
