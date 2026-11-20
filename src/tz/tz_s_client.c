/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The non-secure client of the TF-M DICE partition (#608 slice 3): one
 * psa_call per tfw_s_dice_* call on the stateless TFW_DICE_SERVICE, whose
 * handle TF-M generates into psa_manifest/sid.h from src/tz/tfm/tfw_dice.yaml
 * and installs with api_ns. See include/tfw/tz_s_api.h.
 */
#include "psa/client.h"
#include "psa_manifest/sid.h"
#include "tfw/tz_s_api.h"
#include "dice_partition.h"

/* The partition answers a tfw_status_t (>= 0); a negative status is the PSA
 * framework's own error. */
static tfw_status_t answer(psa_status_t st)
{
    return st < 0 ? TFW_ERR_INTERNAL : (tfw_status_t)st;
}

static tfw_status_t key_call(int32_t op, uint8_t pub[64])
{
    psa_outvec out[1] = { { pub, 64u } };
    tfw_status_t st;

    if (pub == NULL) {
        return TFW_ERR_RANGE;
    }
    st = answer(psa_call(TFW_DICE_SERVICE_HANDLE, op, NULL, 0u, out, 1u));
    if (st == TFW_OK && out[0].len != 64u) {
        return TFW_ERR_INTERNAL;
    }
    return st;
}

tfw_status_t tfw_s_dice_deviceid_pub(uint8_t pub_out[64])
{
    return key_call(TFW_DICE_OP_DEVICEID_PUB, pub_out);
}

tfw_status_t tfw_s_dice_alias_pub(uint8_t pub_out[64])
{
    return key_call(TFW_DICE_OP_ALIAS_PUB, pub_out);
}

tfw_status_t tfw_s_dice_alias_cert(const uint8_t nonce[32], uint32_t off,
                                   uint8_t *page, size_t cap, size_t *got,
                                   uint32_t *total)
{
    psa_invec in[2] = { { nonce, 32u }, { &off, sizeof(off) } };
    psa_outvec out[2] = { { page, cap }, { total, sizeof(*total) } };
    tfw_status_t st;

    if (nonce == NULL || page == NULL || got == NULL || total == NULL) {
        return TFW_ERR_RANGE;
    }
    st = answer(psa_call(TFW_DICE_SERVICE_HANDLE, TFW_DICE_OP_ALIAS_CERT,
                         in, 2u, out, 2u));
    if (st != TFW_OK) {
        return st;
    }
    if (out[0].len == 0u || out[0].len > cap || out[1].len != sizeof(*total)) {
        return TFW_ERR_INTERNAL;
    }
    *got = out[0].len;
    return TFW_OK;
}

tfw_status_t tfw_s_dice_alias_sign(const uint8_t digest[32], uint8_t sig[64])
{
    psa_invec in[1] = { { digest, 32u } };
    psa_outvec out[1] = { { sig, 64u } };
    tfw_status_t st;

    if (digest == NULL || sig == NULL) {
        return TFW_ERR_RANGE;
    }
    st = answer(psa_call(TFW_DICE_SERVICE_HANDLE, TFW_DICE_OP_ALIAS_SIGN,
                         in, 1u, out, 1u));
    if (st == TFW_OK && out[0].len != 64u) {
        return TFW_ERR_INTERNAL;
    }
    return st;
}
