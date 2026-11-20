/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for TF-M's interface/include/psa/client.h (PSA Firmware
 * Framework 1.1): the types and the one call src/tz/tz_s_client.c uses.
 * test/unit/test_dice_partition.c defines psa_call.
 */
#ifndef TFW_TEST_PSA_CLIENT_H
#define TFW_TEST_PSA_CLIENT_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t psa_status_t;
typedef int32_t psa_handle_t;

#define PSA_SUCCESS                 ((psa_status_t)0)
#define PSA_ERROR_PROGRAMMER_ERROR  ((psa_status_t)-129)
#define PSA_ERROR_CONNECTION_REFUSED ((psa_status_t)-130)

typedef struct psa_invec {
    const void *base;
    size_t      len;
} psa_invec;

typedef struct psa_outvec {
    void  *base;
    size_t len;
} psa_outvec;

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec *in_vec, size_t in_len,
                      psa_outvec *out_vec, size_t out_len);

#endif
