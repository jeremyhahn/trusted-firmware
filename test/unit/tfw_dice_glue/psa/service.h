/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for TF-M's psa/service.h (interface/include, v2.3.1), only what
 * src/tz/tfm/tfw_dice_partition.c uses, for test_tfw_dice_glue (#608). The
 * names and signatures are TF-M's; the test file implements psa_read and
 * psa_write.
 */
#ifndef TFW_STUB_PSA_SERVICE_H
#define TFW_STUB_PSA_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#ifndef TFW_STUB_PSA_STATUS_T
#define TFW_STUB_PSA_STATUS_T
typedef int32_t psa_status_t;
#define PSA_SUCCESS ((psa_status_t)0)
#endif

typedef int32_t psa_handle_t;

#define PSA_MAX_IOVEC 4
#define PSA_IPC_CALL  (0)

typedef struct psa_msg_t {
    int32_t type;
    psa_handle_t handle;
    int32_t client_id;
    void *rhandle;
    size_t in_size[PSA_MAX_IOVEC];
    size_t out_size[PSA_MAX_IOVEC];
} psa_msg_t;

size_t psa_read(psa_handle_t msg_handle, uint32_t invec_idx, void *buffer,
                size_t num_bytes);
void psa_write(psa_handle_t msg_handle, uint32_t outvec_idx,
               const void *buffer, size_t num_bytes);

#endif
