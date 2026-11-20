/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TF-M DICE partition's logic (#608 slice 3, ruling 1024 (a)), kept apart
 * from TF-M's headers so the host tests run it: the boot-time derivation of
 * the service's inputs, and the dispatch of one psa_call to the service
 * (src/tz/dice_service.c). src/tz/tfm/tfw_dice_partition.c is the TF-M side:
 * it fetches the UDS and the measurement and wraps psa_read and psa_write as
 * a tfw_dice_msg_t.
 *
 * Each operation is a psa_call type on the one stateless service
 * TFW_DICE_SERVICE (src/tz/tfm/tfw_dice.yaml), with these vectors:
 *
 *   TFW_DICE_OP_DEVICEID_PUB  no input                 out[0] X||Y (64)
 *   TFW_DICE_OP_ALIAS_PUB     no input                 out[0] X||Y (64)
 *   TFW_DICE_OP_ALIAS_CERT    in[0] nonce (32)         out[0] one page (1..255)
 *                             in[1] offset (uint32_t)  out[1] total (uint32_t)
 *   TFW_DICE_OP_ALIAS_SIGN    in[0] digest (32)        out[0] r||s (64)
 *
 * The answer is a tfw_status_t, which psa_call hands the client as is: TFW_OK
 * is PSA_SUCCESS and the others are positive, application-defined codes. A
 * refusal writes no out-vector, so the client sees every out length 0.
 */
#ifndef TFW_TZ_DICE_PARTITION_H
#define TFW_TZ_DICE_PARTITION_H

#include <stdint.h>
#include <stddef.h>
#include "tfw/status.h"
#include "tfw/types.h"

#define TFW_DICE_OP_DEVICEID_PUB 1
#define TFW_DICE_OP_ALIAS_PUB    2
#define TFW_DICE_OP_ALIAS_CERT   3
#define TFW_DICE_OP_ALIAS_SIGN   4

#define TFW_DICE_IOVEC 2u /* the most in- or out-vectors an operation takes */

/* The UEID is HMAC-SHA256(UDS, TFW_DICE_UEID_LABEL) cut to TFW_ATTEST_ID_LEN:
 * device-unique and stable, with no MMIO grant for a chip UID. */
#define TFW_DICE_UEID_LABEL     "tfw-dice-ueid-v1"
#define TFW_DICE_UEID_LABEL_LEN 16u

/* One psa_call as the partition sees it. */
typedef struct {
    size_t in_size[TFW_DICE_IOVEC];  /* 0 for a vector the client did not pass */
    size_t out_size[TFW_DICE_IOVEC];
    /* Copies up to len bytes of in-vector vec into buf; the bytes copied. */
    size_t (*read)(void *ctx, uint32_t vec, void *buf, size_t len);
    /* Writes len bytes to out-vector vec. */
    void (*write)(void *ctx, uint32_t vec, const void *buf, size_t len);
    void *ctx;
} tfw_dice_msg_t;

/* The partition's init: CDI = HMAC-SHA256(UDS, measurement), the DICE layer 0
 * form boot/boot_cdi.h uses, and the service started over the UDS, that CDI,
 * the UEID above and the measurement as the alias certificate's firmware hash.
 * TFW_ERR_RANGE on a NULL input; on any refusal the service is cleared and
 * answers TFW_ERR_NOT_PROVISIONED. The caller zeroes its UDS. */
tfw_status_t tfw_dice_partition_start(const uint8_t uds[32],
                                      const uint8_t measurement[32],
                                      tfw_crypto_tier_t tier);

/* One call. TFW_ERR_UNKNOWN_CMD for an operation not above, TFW_ERR_BAD_ARGC
 * for an input vector of the wrong size, else the service's answer. Nothing
 * is written unless it answers TFW_OK. */
tfw_status_t tfw_dice_dispatch(int32_t op, const tfw_dice_msg_t *m);

#endif
