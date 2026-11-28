/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TF-M DICE partition's logic (#608 slice 3); see dice_partition.h for
 * the call types and their vectors. Every input is copied into a local before
 * the service runs and every answer is built in a local before one write, so
 * the service never touches a client buffer and a refusal writes nothing.
 */
#include <string.h>
#include "dice_partition.h"
#include "dice_service.h"
#include "../crypto/sha256.h"
#include "../core/secure_zero.h"
#include "tfw/protocol.h"

/* The capacity the service sees: the client's, cut to the local buffer. */
static uint32_t min_cap(size_t n, uint32_t local)
{
    return n < local ? (uint32_t)n : local;
}

/* Reads in-vector vec, which must be exactly len bytes. */
static int read_exact(const tfw_dice_msg_t *m, uint32_t vec, void *buf,
                      size_t len)
{
    return m->in_size[vec] == len && m->read(m->ctx, vec, buf, len) == len;
}

tfw_status_t tfw_dice_partition_start(const uint8_t uds[32],
                                      const uint8_t measurement[32],
                                      tfw_crypto_tier_t tier)
{
    uint8_t cdi[32];
    uint8_t ueid[32];
    tfw_dice_inputs_t in;
    tfw_status_t st;

    tfw_dice_service_clear();
    if (uds == NULL || measurement == NULL) {
        return TFW_ERR_RANGE;
    }
    tfw_hmac_sha256(uds, 32u, measurement, 32u, cdi);
    tfw_hmac_sha256(uds, 32u, (const uint8_t *)TFW_DICE_UEID_LABEL,
                    TFW_DICE_UEID_LABEL_LEN, ueid);
    in.uds = uds;
    in.cdi = cdi;
    in.ueid = ueid; /* the first TFW_ATTEST_ID_LEN bytes */
    in.fw_hash = measurement;
    in.tier = tier;
    st = tfw_dice_service_init(&in);
    if (st != TFW_OK) {
        tfw_dice_service_clear();
    }
    tfw_secure_zero(cdi, sizeof(cdi));
    tfw_secure_zero(ueid, sizeof(ueid));
    return st;
}

static tfw_status_t op_pub(int32_t op, const tfw_dice_msg_t *m)
{
    uint8_t key[TFW_DICE_KEY_LEN];
    uint32_t cap = min_cap(m->out_size[0], sizeof(key));
    tfw_status_t st;

    if (m->in_size[0] != 0u || m->in_size[1] != 0u || m->out_size[1] != 0u) {
        return TFW_ERR_BAD_ARGC;
    }
    st = op == TFW_DICE_OP_DEVICEID_PUB ? tfw_dice_deviceid_pub(key, cap)
                                        : tfw_dice_alias_pub(key, cap);
    if (st == TFW_OK) {
        m->write(m->ctx, 0u, key, sizeof(key));
    }
    return st;
}

static tfw_status_t op_cert(const tfw_dice_msg_t *m)
{
    uint8_t page_buf[TFW_DICE_PAGE_MAX];
    tfw_dice_cert_page_t page;
    uint32_t off;
    tfw_status_t st;

    memset(&page, 0, sizeof(page));
    if (!read_exact(m, 0u, page.nonce, sizeof(page.nonce)) ||
        !read_exact(m, 1u, &off, sizeof(off))) {
        return TFW_ERR_BAD_ARGC;
    }
    if (m->out_size[1] < sizeof(page.total)) {
        return TFW_ERR_RANGE;
    }
    page.off = off;
    page.out = page_buf;
    page.cap = min_cap(m->out_size[0], sizeof(page_buf));
    st = tfw_dice_alias_cert(&page);
    if (st == TFW_OK) {
        m->write(m->ctx, 0u, page_buf, page.got);
        m->write(m->ctx, 1u, &page.total, sizeof(page.total));
    }
    return st;
}

static tfw_status_t op_sign(const tfw_dice_msg_t *m)
{
    uint8_t digest[TFW_DICE_DIGEST_LEN];
    uint8_t sig[TFW_DICE_SIG_LEN];
    tfw_status_t st;

    if (!read_exact(m, 0u, digest, sizeof(digest)) || m->in_size[1] != 0u ||
        m->out_size[1] != 0u) {
        return TFW_ERR_BAD_ARGC;
    }
    st = tfw_dice_alias_sign(digest, sizeof(digest), sig,
                             min_cap(m->out_size[0], sizeof(sig)));
    if (st == TFW_OK) {
        m->write(m->ctx, 0u, sig, sizeof(sig));
    }
    return st;
}

tfw_status_t tfw_dice_dispatch(int32_t op, const tfw_dice_msg_t *m)
{
    if (m == NULL || m->read == NULL || m->write == NULL) {
        return TFW_ERR_RANGE;
    }
    /* No operation reads a third or fourth in-vector: a client that passes
     * one is refused, not served with it unread. */
    if (m->in_size[2] != 0u || m->in_size[3] != 0u) {
        return TFW_ERR_BAD_ARGC;
    }
    /* Nor writes a third or fourth out-vector: a client that passes one is
     * refused, not answered TFW_OK with nothing in it. */
    if (m->out_size[2] != 0u || m->out_size[3] != 0u) {
        return TFW_ERR_BAD_ARGC;
    }

    switch (op) {
    case TFW_DICE_OP_DEVICEID_PUB:
    case TFW_DICE_OP_ALIAS_PUB:
        return op_pub(op, m);
    case TFW_DICE_OP_ALIAS_CERT:
        return op_cert(m);
    case TFW_DICE_OP_ALIAS_SIGN:
        return op_sign(m);
    default:
        return TFW_ERR_UNKNOWN_CMD;
    }
}
