/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DICE partition's TF-M side (#608 slice 3, ruling 1024 (a)): the only
 * file that includes TF-M headers. tfw_dice_partition_init is the manifest's
 * entry_init (src/tz/tfm/tfw_dice.yaml): it derives the UDS from the HUK
 * through TF-M's crypto service, reads the non-secure image's measurement
 * from tf-m-extras' Measured Boot partition and starts the portable core
 * (src/tz/dice_partition.c). tfw_dice_service_sfn is TF-M's secure function
 * for the service TFW_DICE_SERVICE (the SFN model names it <service>_sfn):
 * it wraps psa_read and psa_write as a tfw_dice_msg_t for tfw_dice_dispatch.
 *
 * Names and signatures are TF-M v2.3.1's and tf-m-extras' at
 * scripts/tfm-pins.sh TFM_COMMIT and EXTRAS_COMMIT.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "psa/service.h"
#include "psa/crypto.h"
#include "tfm_builtin_key_ids.h"
#include "measured_boot_api.h"
#include "tfw/status.h"
#include "tfw/types.h"
#include "../dice_partition.h"
#include "../dice_service.h"
#include "../../core/secure_zero.h"
#include "../../third_party/micro-ecc/uECC.h"

/* The UDS is HKDF-SHA256(HUK, info) (no salt): bound to this partition by
 * TF-M's builtin key loader, never leaving the secure side. */
#define TFW_DICE_UDS_INFO     "tfw-dice-uds-v1"
#define TFW_DICE_UDS_INFO_LEN 15u
#define TFW_DICE_UDS_LEN      32u

/* Measured Boot slot 3: RT_1, the non-secure image MCUboot measured. */
#define TFW_DICE_NS_SLOT 3u
#define TFW_DICE_MEAS_LEN 32u

psa_status_t tfw_dice_partition_init(void);
psa_status_t tfw_dice_service_sfn(const psa_msg_t *msg);

/* Static, so it lives in the partition's private data, never on a stack a
 * later call reuses; zero outside init. */
static uint8_t tfw_dice_uds[TFW_DICE_UDS_LEN];

/* Blinding for micro-ecc's point arithmetic; never the nonce. Fails closed:
 * on Arm micro-ecc has no default RNG, and without one a sign runs its
 * variable-time inverse on the RFC 6979 nonce unblinded. */
static int tfw_dice_blind_rng(uint8_t *dest, unsigned size)
{
    return psa_generate_random(dest, (size_t)size) == PSA_SUCCESS;
}

static psa_status_t tfw_dice_derive_uds(uint8_t uds[TFW_DICE_UDS_LEN])
{
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t st;

    st = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_key(&op, PSA_KEY_DERIVATION_INPUT_SECRET,
                                          (psa_key_id_t)TFM_BUILTIN_KEY_ID_HUK);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO,
                                            (const uint8_t *)TFW_DICE_UDS_INFO,
                                            TFW_DICE_UDS_INFO_LEN);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_output_bytes(&op, uds, TFW_DICE_UDS_LEN);
    }
    (void)psa_key_derivation_abort(&op);
    return st;
}

/* The NS image's measurement, only if Measured Boot locked the slot and it
 * holds a SHA-256: an unlocked slot could have been extended after boot. */
static bool tfw_dice_read_ns_measurement(uint8_t meas[TFW_DICE_MEAS_LEN])
{
    uint8_t signer_id[SIGNER_ID_MAX_SIZE];
    uint8_t version[VERSION_MAX_SIZE];
    uint8_t sw_type[SW_TYPE_MAX_SIZE];
    uint8_t value[MEASUREMENT_VALUE_MAX_SIZE];
    size_t signer_id_len = 0u;
    size_t version_len = 0u;
    size_t sw_type_len = 0u;
    size_t value_len = 0u;
    uint32_t algo = 0u;
    bool locked = false;
    psa_status_t st;
    size_t i;

    st = tfm_measured_boot_read_measurement((uint8_t)TFW_DICE_NS_SLOT,
                                            signer_id, sizeof(signer_id), &signer_id_len,
                                            version, sizeof(version), &version_len,
                                            &algo,
                                            sw_type, sizeof(sw_type), &sw_type_len,
                                            value, sizeof(value), &value_len,
                                            &locked);
    if (st != PSA_SUCCESS || !locked || algo != (uint32_t)PSA_ALG_SHA_256 ||
        value_len != TFW_DICE_MEAS_LEN) {
        return false;
    }
    for (i = 0; i < TFW_DICE_MEAS_LEN; i++) {
        meas[i] = value[i];
    }
    return true;
}

psa_status_t tfw_dice_partition_init(void)
{
    uint8_t meas[TFW_DICE_MEAS_LEN];
    tfw_status_t st = TFW_ERR_NOT_PROVISIONED;

    /* Every refusal leaves the service answering TFW_ERR_NOT_PROVISIONED and
     * still returns PSA_SUCCESS: TF-M boots and the client sees why. */
    tfw_dice_service_clear();
    uECC_set_rng(tfw_dice_blind_rng);
    if (tfw_dice_derive_uds(tfw_dice_uds) == PSA_SUCCESS &&
        tfw_dice_read_ns_measurement(meas)) {
        st = tfw_dice_partition_start(tfw_dice_uds, meas, TFW_CRYPTO_SOFTWARE);
    }
    if (st != TFW_OK) {
        tfw_dice_service_clear();
    }
    tfw_secure_zero(tfw_dice_uds, sizeof(tfw_dice_uds));
    return PSA_SUCCESS;
}

static size_t tfw_dice_msg_read(void *ctx, uint32_t vec, void *buf, size_t len)
{
    return psa_read(*(const psa_handle_t *)ctx, vec, buf, len);
}

static void tfw_dice_msg_write(void *ctx, uint32_t vec, const void *buf, size_t len)
{
    psa_write(*(const psa_handle_t *)ctx, vec, buf, len);
}

psa_status_t tfw_dice_service_sfn(const psa_msg_t *msg)
{
    tfw_dice_msg_t m;
    psa_handle_t handle;
    uint32_t i;

    if (msg == NULL) {
        return (psa_status_t)TFW_ERR_RANGE;
    }
    handle = msg->handle;
    for (i = 0; i < TFW_DICE_IOVEC; i++) {
        m.in_size[i] = i < (uint32_t)PSA_MAX_IOVEC ? msg->in_size[i] : 0u;
        m.out_size[i] = i < (uint32_t)PSA_MAX_IOVEC ? msg->out_size[i] : 0u;
    }
    m.read = tfw_dice_msg_read;
    m.write = tfw_dice_msg_write;
    m.ctx = &handle;
    /* The tfw_status_t is the psa_call status (plan, "Decided in slice 3"). */
    return (psa_status_t)tfw_dice_dispatch(msg->type, &m);
}
