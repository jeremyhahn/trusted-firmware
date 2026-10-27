/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The trusted application's literal vectors (docs/optee-trustzone/README.md,
 * "The trusted application"). Every value is written out by hand from the
 * design's table and the GlobalPlatform specifications, never computed from
 * optee/ta/: a vector computed from the code it pins moves whenever the code
 * moves. A failure against this file is an interface break, not a test to
 * update.
 */
#ifndef TFW_TA_VECTORS_H
#define TFW_TA_VECTORS_H

#include <stdint.h>

/* The UUID 23751b49-3a76-4e49-b63c-e5853742fa1f, in RFC 9562's byte order. */
static const uint8_t TFW_TA_VEC_UUID[16] = {
    0x23, 0x75, 0x1b, 0x49, 0x3a, 0x76, 0x4e, 0x49,
    0xb6, 0x3c, 0xe5, 0x85, 0x37, 0x42, 0xfa, 0x1f,
};

/* Command ids. 0x0 is reserved; 0x3 and 0x4 are held, unimplemented. */
#define TFW_TA_VEC_CMD_RESERVED   0x00000000u
#define TFW_TA_VEC_CMD_GET_INFO   0x00000001u
#define TFW_TA_VEC_CMD_GET_RANDOM 0x00000002u
#define TFW_TA_VEC_CMD_SEAL       0x00000003u
#define TFW_TA_VEC_CMD_UNSEAL     0x00000004u
#define TFW_TA_VEC_CMD_LAST       0xFFFFFFFFu

/*
 * paramTypes, four bits a slot, slot 0 lowest (TEE Client API
 * TEEC_PARAM_TYPES; Internal Core API TEE_PARAM_TYPES). NONE is 0,
 * VALUE_INPUT 1, VALUE_OUTPUT 2, MEMREF_TEMP_INPUT / MEMREF_INPUT 5,
 * MEMREF_TEMP_OUTPUT / MEMREF_OUTPUT 6.
 */
#define TFW_TA_VEC_PT_NONE       0x00000000u
#define TFW_TA_VEC_PT_GET_INFO   0x00000002u /* VALUE_OUTPUT, NONE, NONE, NONE */
#define TFW_TA_VEC_PT_GET_RANDOM 0x00000006u /* MEMREF_TEMP_OUTPUT, NONE, NONE, NONE */
#define TFW_TA_VEC_PT_VALUE_IN   0x00000001u /* VALUE_INPUT, NONE, NONE, NONE */

/* GET_INFO's answer: interface version 1, flags 0. */
#define TFW_TA_VEC_INFO_VERSION 1u
#define TFW_TA_VEC_INFO_FLAGS   0u

/* GET_RANDOM's buffer bounds, in bytes. */
#define TFW_TA_VEC_RANDOM_MIN 1u
#define TFW_TA_VEC_RANDOM_MAX 4096u

/* Return codes and the origin, as GlobalPlatform numbers them. */
#define TFW_TA_VEC_SUCCESS          0x00000000u
#define TFW_TA_VEC_BAD_PARAMETERS   0xFFFF0006u
#define TFW_TA_VEC_NOT_SUPPORTED    0xFFFF000Au
#define TFW_TA_VEC_ORIGIN_TRUSTED_APP 0x00000004u

/* The session's login: TEEC_LOGIN_PUBLIC. */
#define TFW_TA_VEC_LOGIN_PUBLIC 0x00000000u

#endif /* TFW_TA_VECTORS_H */
