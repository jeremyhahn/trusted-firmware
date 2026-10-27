/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's TEE: test/mock/gp_bridge.c implements the TEE Client API calls
 * tfw-ta-check makes by calling the TA's entry points directly, and the
 * Internal Core API's TEE_GenerateRandom the TA calls. The knobs below make it
 * answer what a broken TEE or TA would, so every path of the check is taken.
 */
#ifndef TFW_GP_BRIDGE_H
#define TFW_GP_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

struct tfw_gp_mock {
    uint32_t init_result;  /* TEEC_InitializeContext answers this */
    uint32_t open_result;  /* nonzero: TEEC_OpenSession answers this, origin TEE */
    int open_always;       /* nonzero: an open the TA refused succeeds anyway */
    int force;             /* nonzero: an invoke of force_cmd answers force_rc */
    uint32_t force_cmd;
    uint32_t force_rc;
    uint32_t force_origin;
    uint32_t value_a_xor;  /* xored into param 0's value.a after the TA answers */
    int stuck_random;      /* TEE_GenerateRandom writes zeros */
    int contexts;          /* contexts initialised and not yet finalised */
    int sessions;          /* sessions opened and not yet closed */
    size_t random_bytes;   /* bytes TEE_GenerateRandom has written */
};

extern struct tfw_gp_mock tfw_gp_mock;

/* Every knob off, every count zero, the random stream from its start. */
void tfw_gp_mock_reset(void);

#endif /* TFW_GP_BRIDGE_H */
