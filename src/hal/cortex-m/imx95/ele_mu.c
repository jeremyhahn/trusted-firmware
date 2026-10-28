/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The EdgeLock Enclave's message layer over its MU (ele_mu.h, which cites
 * the source of every value). The order is U-Boot's ele_mu.c: the header
 * through TR 0, each further word through the next TR once it is empty; the
 * response's header from RR 0, its size, then each further word from the
 * next RR once it is full.
 */
#include <stddef.h>
#include <stdint.h>

#include "cm_regs.h"
#include "ele_mu.h"

#define MU(c, off) TFW_CM_REG((c)->mu + (off))

static int wait_bit(const struct tfw_ele_chan *c, uint32_t reg, uint32_t bit)
{
    uint32_t i;

    for (i = 0u; i < TFW_IMX95_ELE_POLLS; i++) {
        if ((MU(c, reg) & bit) != 0u) {
            return 1;
        }
    }
    return 0;
}

uint32_t tfw_ele_header(uint32_t command, uint32_t size, uint32_t tag)
{
    return TFW_ELE_VERSION | ((size & 0xFFu) << 8) | ((command & 0xFFu) << 16) |
           ((tag & 0xFFu) << 24);
}

int32_t tfw_ele_call(const struct tfw_ele_chan *c, const struct tfw_ele_msg *cmd,
                     uint32_t rsp_words, struct tfw_ele_msg *rsp, uint32_t *status)
{
    uint32_t par, ntr, nrr, size, h, k;

    size = TFW_ELE_HDR_SIZE(cmd->w[0]);
    if (size == 0u || size > TFW_ELE_MSG_WORDS || rsp_words < 2u || rsp_words > TFW_ELE_MSG_WORDS) {
        return TFW_ELE_E_LENGTH;
    }
    par = MU(c, TFW_IMX95_MU_PAR);
    ntr = par & 0xFFu;
    nrr = (par >> 8) & 0xFFu;
    if (ntr == 0u || nrr == 0u) {
        return TFW_ELE_E_MU;
    }

    for (k = 0u; k < size; k++) {
        if (!wait_bit(c, TFW_IMX95_MU_TSR, 1u << (k % ntr))) {
            return TFW_ELE_E_TIMEOUT;
        }
        MU(c, TFW_IMX95_MU_TR + 4u * (k % ntr)) = cmd->w[k];
    }

    if (!wait_bit(c, TFW_IMX95_MU_RSR, 1u)) {
        return TFW_ELE_E_TIMEOUT;
    }
    h = MU(c, TFW_IMX95_MU_RR);
    rsp->w[0] = h;
    if (TFW_ELE_HDR_VERSION(h) != TFW_ELE_VERSION) {
        return TFW_ELE_E_VERSION;
    }
    if (TFW_ELE_HDR_TAG(h) != TFW_ELE_TAG_RSP) {
        return TFW_ELE_E_TAG;
    }
    if (TFW_ELE_HDR_COMMAND(h) != TFW_ELE_HDR_COMMAND(cmd->w[0])) {
        return TFW_ELE_E_COMMAND;
    }
    if (TFW_ELE_HDR_SIZE(h) != rsp_words) {
        return TFW_ELE_E_SIZE;
    }

    for (k = 1u; k < rsp_words; k++) {
        if (!wait_bit(c, TFW_IMX95_MU_RSR, 1u << (k % nrr))) {
            return TFW_ELE_E_TIMEOUT;
        }
        rsp->w[k] = MU(c, TFW_IMX95_MU_RR + 4u * (k % nrr));
    }
    if (status != NULL) {
        *status = rsp->w[1];
    }
    return TFW_ELE_STATUS(rsp->w[1]) == TFW_ELE_STATUS_OK ? 0 : TFW_ELE_E_STATUS;
}

static void msg_start(struct tfw_ele_msg *m, uint32_t command, uint32_t size)
{
    uint32_t k;

    for (k = 0u; k < TFW_ELE_MSG_WORDS; k++) {
        m->w[k] = 0u;
    }
    m->w[0] = tfw_ele_header(command, size, TFW_ELE_TAG_CMD);
}

/* Ends a request longer than TFW_ELE_WORDS_WITHOUT_CRC words of `size` in
 * its CRC word, the XOR of every word before it. */
static void msg_crc(struct tfw_ele_msg *m, uint32_t size)
{
    uint32_t k, crc = 0u;

    for (k = 0u; k < size - 1u; k++) {
        crc ^= m->w[k];
    }
    m->w[size - 1u] = crc;
}

void tfw_ele_get_info_cmd(struct tfw_ele_msg *m, uint64_t buf_addr)
{
    msg_start(m, TFW_ELE_GET_INFO, TFW_ELE_GET_INFO_CMD_WORDS);
    m->w[1] = (uint32_t)(buf_addr >> 32);
    m->w[2] = (uint32_t)(buf_addr & 0xFFFFFFFFu);
    m->w[3] = TFW_ELE_GET_INFO_LEN;   /* buf_sz in bits 15 to 0, the rest reserved */
}

int32_t tfw_ele_get_info(const struct tfw_ele_chan *c, uint64_t buf_addr, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;

    tfw_ele_get_info_cmd(&cmd, buf_addr);
    return tfw_ele_call(c, &cmd, TFW_ELE_GET_INFO_RSP_WORDS, &rsp, status);
}

void tfw_ele_session_open_cmd(struct tfw_ele_msg *m, uint8_t interrupt_idx)
{
    msg_start(m, TFW_ELE_SESSION_OPEN, TFW_ELE_SESSION_OPEN_CMD_WORDS);
    m->w[1] = (uint32_t)interrupt_idx << 8;   /* rsv, interrupt_idx, rsv1 */
    /* w[2]: priority, operating_mode, rsv2, all zero */
}

void tfw_ele_session_close_cmd(struct tfw_ele_msg *m, uint32_t handle)
{
    msg_start(m, TFW_ELE_SESSION_CLOSE, TFW_ELE_SESSION_CLOSE_CMD_WORDS);
    m->w[1] = handle;
}

int32_t tfw_ele_session_open(const struct tfw_ele_chan *c, uint8_t interrupt_idx,
                             uint32_t *handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_session_open_cmd(&cmd, interrupt_idx);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SESSION_OPEN_RSP_WORDS, &rsp, status);
    if (rc == 0) {
        *handle = rsp.w[2];
    }
    return rc;
}

int32_t tfw_ele_session_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_session_close_cmd(&cmd, handle);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SESSION_CLOSE_RSP_WORDS, &rsp, status);
    return rc == TFW_ELE_E_STATUS ? TFW_ELE_E_HANDLE : rc;
}

void tfw_ele_key_store_open_cmd(struct tfw_ele_msg *m, uint32_t session, uint32_t id,
                                uint32_t nonce, uint8_t create)
{
    const uint32_t flags = (create != 0u) ? TFW_ELE_KEY_STORE_CREATE : TFW_ELE_KEY_STORE_LOAD;

    msg_start(m, TFW_ELE_KEY_STORE_OPEN, TFW_ELE_KEY_STORE_OPEN_CMD_WORDS);
    m->w[1] = session;
    m->w[2] = id;
    m->w[3] = nonce;
    m->w[4] = flags << 16;   /* rsv1, flags, rsv2 */
    msg_crc(m, TFW_ELE_KEY_STORE_OPEN_CMD_WORDS);
}

void tfw_ele_key_store_close_cmd(struct tfw_ele_msg *m, uint32_t handle)
{
    msg_start(m, TFW_ELE_KEY_STORE_CLOSE, TFW_ELE_KEY_STORE_CLOSE_CMD_WORDS);
    m->w[1] = handle;
}

int32_t tfw_ele_key_store_open(const struct tfw_ele_chan *c, uint32_t session, uint32_t id,
                               uint32_t nonce, uint8_t create, uint32_t *handle,
                               uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (session == 0u) {
        return TFW_ELE_E_SESSION;
    }
    tfw_ele_key_store_open_cmd(&cmd, session, id, nonce, create);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_KEY_STORE_OPEN_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        *handle = rsp.w[2];
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_SESSION;
    }
    return rc;
}

int32_t tfw_ele_key_store_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_key_store_close_cmd(&cmd, handle);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_KEY_STORE_CLOSE_RSP_WORDS, &rsp, status);
    return rc == TFW_ELE_E_STATUS ? TFW_ELE_E_HANDLE : rc;
}

void tfw_ele_key_mgmt_open_cmd(struct tfw_ele_msg *m, uint32_t store)
{
    msg_start(m, TFW_ELE_KEY_MGMT_OPEN, TFW_ELE_KEY_MGMT_OPEN_CMD_WORDS);
    m->w[1] = store;
    /* w[2] input_address_ext, w[3] output_address_ext, w[4] flags and rsv:
     * all zero, as the library sends them */
    msg_crc(m, TFW_ELE_KEY_MGMT_OPEN_CMD_WORDS);
}

void tfw_ele_key_mgmt_close_cmd(struct tfw_ele_msg *m, uint32_t handle)
{
    msg_start(m, TFW_ELE_KEY_MGMT_CLOSE, TFW_ELE_KEY_MGMT_CLOSE_CMD_WORDS);
    m->w[1] = handle;
}

int32_t tfw_ele_key_mgmt_open(const struct tfw_ele_chan *c, uint32_t store, uint32_t *handle,
                              uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (store == 0u) {
        return TFW_ELE_E_KEY_STORE;
    }
    tfw_ele_key_mgmt_open_cmd(&cmd, store);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_KEY_MGMT_OPEN_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        *handle = rsp.w[2];
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_KEY_STORE;
    }
    return rc;
}

int32_t tfw_ele_key_mgmt_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_key_mgmt_close_cmd(&cmd, handle);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_KEY_MGMT_CLOSE_RSP_WORDS, &rsp, status);
    return rc == TFW_ELE_E_STATUS ? TFW_ELE_E_HANDLE : rc;
}

void tfw_ele_key_generate_cmd(struct tfw_ele_msg *m, uint32_t mgmt, const struct tfw_ele_key_gen *k)
{
    msg_start(m, TFW_ELE_KEY_GENERATE, TFW_ELE_KEY_GENERATE_CMD_WORDS);
    m->w[1] = mgmt;
    m->w[2] = k->key_id;
    m->w[3] = (uint32_t)k->pub_size | ((uint32_t)k->group << 16);
    m->w[4] = (k->key_type & 0xFFFFu) | (TFW_ELE_KEY_BITS_P256 << 16);
    m->w[5] = k->lifetime;
    m->w[6] = k->usage;
    m->w[7] = k->algo;
    /* w[8] key_lifecycle 0, the current one; w[9] flags, rsv and
     * priv_key_sz all zero: no STRICT, no plaintext export */
    m->w[10] = k->pub_addr;
    msg_crc(m, TFW_ELE_KEY_GENERATE_CMD_WORDS);
}

int32_t tfw_ele_key_generate(const struct tfw_ele_chan *c, uint32_t mgmt,
                             const struct tfw_ele_key_gen *k, uint32_t *key_id,
                             uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (mgmt == 0u) {
        return TFW_ELE_E_KEY_MGMT;
    }
    if (k->key_type != TFW_ELE_KEY_TYPE_ECC_NIST || k->lifetime != TFW_ELE_LIFETIME_VOLATILE) {
        return TFW_ELE_E_KEY_TYPE;
    }
    if (k->pub_size < TFW_ELE_P256_PUB_LEN) {
        return TFW_ELE_E_BUFFER;
    }
    tfw_ele_key_generate_cmd(&cmd, mgmt, k);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_KEY_GENERATE_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        *key_id = rsp.w[2];
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_KEY_MGMT;
    }
    return rc;
}

void tfw_ele_sign_gen_open_cmd(struct tfw_ele_msg *m, uint32_t store)
{
    msg_start(m, TFW_ELE_SIGN_GEN_OPEN, TFW_ELE_SIGN_GEN_OPEN_CMD_WORDS);
    m->w[1] = store;
    /* w[2] input_address_ext, w[3] output_address_ext, w[4] flags and
     * reserved: all zero, as the PSA_COMPLIANT library sends them */
    msg_crc(m, TFW_ELE_SIGN_GEN_OPEN_CMD_WORDS);
}

void tfw_ele_sign_gen_close_cmd(struct tfw_ele_msg *m, uint32_t handle)
{
    msg_start(m, TFW_ELE_SIGN_GEN_CLOSE, TFW_ELE_SIGN_GEN_CLOSE_CMD_WORDS);
    m->w[1] = handle;
}

int32_t tfw_ele_sign_gen_open(const struct tfw_ele_chan *c, uint32_t store, uint32_t *handle,
                              uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (store == 0u) {
        return TFW_ELE_E_KEY_STORE;
    }
    tfw_ele_sign_gen_open_cmd(&cmd, store);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_GEN_OPEN_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        *handle = rsp.w[2];
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_KEY_STORE;
    }
    return rc;
}

int32_t tfw_ele_sign_gen_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_sign_gen_close_cmd(&cmd, handle);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_GEN_CLOSE_RSP_WORDS, &rsp, status);
    return rc == TFW_ELE_E_STATUS ? TFW_ELE_E_HANDLE : rc;
}

void tfw_ele_sign_generate_cmd(struct tfw_ele_msg *m, uint32_t sgen, const struct tfw_ele_sign *s)
{
    msg_start(m, TFW_ELE_SIGN_GENERATE, TFW_ELE_SIGN_GENERATE_CMD_WORDS);
    m->w[1] = sgen;
    m->w[2] = s->key_id;
    m->w[3] = s->digest_addr;
    m->w[4] = s->sig_addr;
    m->w[5] = TFW_ELE_SHA256_LEN;
    m->w[6] = (uint32_t)s->sig_size | ((uint32_t)TFW_ELE_SIGN_INPUT_DIGEST << 16);
    m->w[7] = s->scheme;
    /* w[8] salt_len and key_type, w[9] priv_key_size and key_security_size:
     * zero, for a key the enclave holds, not a plaintext one; w[10] to w[12]
     * the SM2 public key and identity: zero, not SM2 */
    msg_crc(m, TFW_ELE_SIGN_GENERATE_CMD_WORDS);
}

int32_t tfw_ele_sign_generate(const struct tfw_ele_chan *c, uint32_t sgen,
                              const struct tfw_ele_sign *s, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (sgen == 0u) {
        return TFW_ELE_E_SIGN_GEN;
    }
    if (s->key_type != TFW_ELE_KEY_TYPE_ECC_NIST || s->key_bits != TFW_ELE_KEY_BITS_P256 ||
        s->scheme != TFW_ELE_SIG_ECDSA_SHA256) {
        return TFW_ELE_E_KEY_TYPE;
    }
    if (s->sig_size < TFW_ELE_P256_SIG_LEN) {
        return TFW_ELE_E_BUFFER;
    }
    tfw_ele_sign_generate_cmd(&cmd, sgen, s);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_GENERATE_RSP_WORDS, &rsp, sp);
    if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_SIGN_GEN;
    }
    return rc;
}

void tfw_ele_pub_key_export_cmd(struct tfw_ele_msg *m, uint32_t store,
                                const struct tfw_ele_key_export *x)
{
    msg_start(m, TFW_ELE_PUB_KEY_EXPORT, TFW_ELE_PUB_KEY_EXPORT_CMD_WORDS);
    m->w[1] = store;
    m->w[2] = x->key_id;
    /* w[3] out_key_addr_ext: zero, as the library sends it */
    m->w[4] = x->out_addr;
    m->w[5] = (uint32_t)x->out_size;
    msg_crc(m, TFW_ELE_PUB_KEY_EXPORT_CMD_WORDS);
}

int32_t tfw_ele_pub_key_export(const struct tfw_ele_chan *c, uint32_t store,
                               const struct tfw_ele_key_export *x, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (store == 0u) {
        return TFW_ELE_E_KEY_STORE;
    }
    if (x->key_type != TFW_ELE_KEY_TYPE_ECC_NIST || x->key_bits != TFW_ELE_KEY_BITS_P256) {
        return TFW_ELE_E_KEY_TYPE;
    }
    if (x->out_size < TFW_ELE_P256_PUB_LEN) {
        return TFW_ELE_E_BUFFER;
    }
    tfw_ele_pub_key_export_cmd(&cmd, store, x);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_PUB_KEY_EXPORT_RSP_WORDS, &rsp, sp);
    if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_KEY_STORE;
    }
    return rc;
}

void tfw_ele_sign_ver_open_cmd(struct tfw_ele_msg *m, uint32_t session)
{
    msg_start(m, TFW_ELE_SIGN_VER_OPEN, TFW_ELE_SIGN_VER_OPEN_CMD_WORDS);
    m->w[1] = session;
    /* w[2] input_address_ext, w[3] output_address_ext, w[4] flags and
     * reserved: all zero, as the PSA_COMPLIANT library sends them */
    msg_crc(m, TFW_ELE_SIGN_VER_OPEN_CMD_WORDS);
}

void tfw_ele_sign_ver_close_cmd(struct tfw_ele_msg *m, uint32_t handle)
{
    msg_start(m, TFW_ELE_SIGN_VER_CLOSE, TFW_ELE_SIGN_VER_CLOSE_CMD_WORDS);
    m->w[1] = handle;
}

int32_t tfw_ele_sign_ver_open(const struct tfw_ele_chan *c, uint32_t session, uint32_t *handle,
                              uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (session == 0u) {
        return TFW_ELE_E_SESSION;
    }
    tfw_ele_sign_ver_open_cmd(&cmd, session);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_VER_OPEN_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        *handle = rsp.w[2];
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_SESSION;
    }
    return rc;
}

int32_t tfw_ele_sign_ver_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    int32_t rc;

    tfw_ele_sign_ver_close_cmd(&cmd, handle);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_VER_CLOSE_RSP_WORDS, &rsp, status);
    return rc == TFW_ELE_E_STATUS ? TFW_ELE_E_HANDLE : rc;
}

void tfw_ele_sign_verify_cmd(struct tfw_ele_msg *m, uint32_t sver, const struct tfw_ele_verify *v)
{
    msg_start(m, TFW_ELE_SIGN_VERIFY, TFW_ELE_SIGN_VERIFY_CMD_WORDS);
    m->w[1] = sver;
    m->w[2] = v->key_addr;
    m->w[3] = v->digest_addr;
    m->w[4] = v->sig_addr;
    m->w[5] = TFW_ELE_SHA256_LEN;
    m->w[6] = TFW_ELE_P256_SIG_LEN | (TFW_ELE_P256_PUB_LEN << 16);
    m->w[7] = TFW_ELE_KEY_BITS_P256 | (TFW_ELE_PUBKEY_TYPE_ECC_NIST << 16);
    m->w[8] = TFW_ELE_VERIFY_INPUT_DIGEST;
    m->w[9] = v->scheme;
    /* w[10] salt_len: zero, no RSA-PSS; w[11] and w[12] the SM2 identity:
     * zero, not SM2 */
    msg_crc(m, TFW_ELE_SIGN_VERIFY_CMD_WORDS);
}

int32_t tfw_ele_sign_verify(const struct tfw_ele_chan *c, uint32_t sver,
                            const struct tfw_ele_verify *v, uint32_t *verdict, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (sver == 0u) {
        return TFW_ELE_E_SIGN_VER;
    }
    if (v->key_type != TFW_ELE_KEY_TYPE_ECC_NIST || v->key_bits != TFW_ELE_KEY_BITS_P256 ||
        v->scheme != TFW_ELE_SIG_ECDSA_SHA256) {
        return TFW_ELE_E_KEY_TYPE;
    }
    if (v->key_size < TFW_ELE_P256_PUB_LEN || v->sig_size < TFW_ELE_P256_SIG_LEN) {
        return TFW_ELE_E_BUFFER;
    }
    tfw_ele_sign_verify_cmd(&cmd, sver, v);
    rc = tfw_ele_call(c, &cmd, TFW_ELE_SIGN_VERIFY_RSP_WORDS, &rsp, sp);
    if (rc == 0) {
        if (verdict != NULL) {
            *verdict = rsp.w[2];
        }
        if (rsp.w[2] != TFW_ELE_VERIFY_PASSED) {
            rc = TFW_ELE_E_SIG_BAD;
        }
    } else if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_SIGN_VER;
    }
    return rc;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        dst[i] = src[i];
    }
}

/* Byte offsets in the buffer: dev_info's cmd, ver and length take 0 to 3,
 * then soc_id 4, soc_rev 6, lmda_val 8, ssm_state 10, uid 12, sha_rom_patch
 * 28, sha_fw 60; dev_addn_info's oem_srkh 92, trng_state 156, csal_state
 * 157, imem_state 158. */
void tfw_ele_get_info_parse(const uint8_t buf[TFW_ELE_GET_INFO_LEN], struct tfw_ele_info *out)
{
    out->soc_id    = le16(&buf[4]);
    out->soc_rev   = le16(&buf[6]);
    out->lmda      = le16(&buf[8]);
    out->ssm_state = buf[10];
    copy(out->uid, &buf[12], 16u);
    copy(out->sha_fw, &buf[60], 32u);
    out->trng_state = buf[156];
    out->csal_state = buf[157];
    out->imem_state = buf[158];
}

int32_t tfw_ele_key_delete_cmd(struct tfw_ele_msg *m, const struct tfw_ele_del_layout *l,
                               uint32_t mgmt, uint32_t key_id)
{
    /* The last word a field may take: the CRC word is not one. */
    uint32_t last = (l->cmd_words > TFW_ELE_WORDS_WITHOUT_CRC) ? l->cmd_words - 2u
                                                                : l->cmd_words - 1u;

    if (l->command == 0u || l->cmd_words < 2u || l->cmd_words > TFW_ELE_MSG_WORDS
        || l->rsp_words < 2u || l->rsp_words > TFW_ELE_MSG_WORDS || l->flags_shift > 24u
        || l->w_handle == 0u || l->w_handle > last || l->w_key == 0u || l->w_key > last
        || l->w_flags == 0u || l->w_flags > last) {
        return TFW_ELE_E_LAYOUT;
    }
    msg_start(m, l->command, l->cmd_words);
    m->w[l->w_handle] = mgmt;
    m->w[l->w_key]    = key_id;
    m->w[l->w_flags] |= (uint32_t)TFW_ELE_DEL_KEY_STRICT << l->flags_shift;
    if (l->cmd_words > TFW_ELE_WORDS_WITHOUT_CRC) {
        msg_crc(m, l->cmd_words);
    }
    return 0;
}

int32_t tfw_ele_key_delete(const struct tfw_ele_chan *c, const struct tfw_ele_del_layout *l,
                           uint32_t mgmt, uint32_t key_id, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (mgmt == 0u) {
        return TFW_ELE_E_KEY_MGMT;
    }
    rc = tfw_ele_key_delete_cmd(&cmd, l, mgmt, key_id);
    if (rc != 0) {
        return rc;
    }
    rc = tfw_ele_call(c, &cmd, l->rsp_words, &rsp, sp);
    if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_KEY_MGMT;
    }
    return rc;
}

/* The last word a field may take in a message of `words`: the CRC word is not
 * one. */
static uint32_t last_field(uint32_t words)
{
    return (words > TFW_ELE_WORDS_WITHOUT_CRC) ? words - 2u : words - 1u;
}

int32_t tfw_ele_get_event_cmd(struct tfw_ele_msg *m, const struct tfw_ele_event_layout *l,
                              uint32_t session)
{
    if (l->command == 0u || l->cmd_words < 2u || l->cmd_words > TFW_ELE_MSG_WORDS
        || l->rsp_words < 3u || l->rsp_words > TFW_ELE_MSG_WORDS
        || l->w_session == 0u || l->w_session > last_field(l->cmd_words)
        || l->w_count < 2u || l->w_first < 2u || l->w_first == l->w_count
        || l->w_count > last_field(l->rsp_words) || l->w_first > last_field(l->rsp_words)) {
        return TFW_ELE_E_LAYOUT;
    }
    msg_start(m, l->command, l->cmd_words);
    m->w[l->w_session] = session;
    if (l->cmd_words > TFW_ELE_WORDS_WITHOUT_CRC) {
        msg_crc(m, l->cmd_words);
    }
    return 0;
}

int32_t tfw_ele_get_event(const struct tfw_ele_chan *c, const struct tfw_ele_event_layout *l,
                          uint32_t session, uint32_t *ev, uint32_t max, uint32_t *n,
                          uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    uint32_t count, room, k;
    int32_t rc;

    *n = 0u;
    if (session == 0u) {
        return TFW_ELE_E_SESSION;
    }
    rc = tfw_ele_get_event_cmd(&cmd, l, session);
    if (rc != 0) {
        return rc;
    }
    rc = tfw_ele_call(c, &cmd, l->rsp_words, &rsp, sp);
    if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        return TFW_ELE_E_SESSION;
    }
    if (rc != 0) {
        return rc;
    }
    /* A count past the response, the cap or the caller's room is cut to them,
     * never read past: the count word and the CRC word are not events. */
    count = rsp.w[l->w_count];
    room  = last_field(l->rsp_words) + 1u - l->w_first;
    if (l->w_count > l->w_first && (uint32_t)l->w_count - l->w_first < room) {
        room = (uint32_t)l->w_count - l->w_first;
    }
    if (count > room) {
        count = room;
    }
    if (count > TFW_ELE_EVENTS_MAX) {
        count = TFW_ELE_EVENTS_MAX;
    }
    if (count > max) {
        count = max;
    }
    for (k = 0u; k < count; k++) {
        ev[k] = rsp.w[l->w_first + k];
    }
    *n = count;
    return 0;
}

int32_t tfw_ele_dev_attest_cmd(struct tfw_ele_msg *m, const struct tfw_ele_attest_layout *l,
                               uint32_t session, const uint8_t *nonce, uint32_t nonce_len,
                               uint64_t buf_addr)
{
    /* The last word a field may take: the CRC word is not one. */
    uint32_t last = (l->cmd_words > TFW_ELE_WORDS_WITHOUT_CRC) ? l->cmd_words - 2u
                                                                : l->cmd_words - 1u;
    uint32_t nonce_last = (uint32_t)l->w_nonce + (nonce_len + 3u) / 4u - 1u;
    uint32_t i;

    if (nonce == NULL
        || (nonce_len != TFW_ELE_ATTEST_NONCE_SHORT && nonce_len != TFW_ELE_ATTEST_NONCE_LONG)) {
        return TFW_ELE_E_NONCE;
    }
    if (l->command == 0u || l->report_len == 0u || l->cmd_words < 2u
        || l->cmd_words > TFW_ELE_MSG_WORDS || l->rsp_words < 2u
        || l->rsp_words > TFW_ELE_MSG_WORDS || l->w_session > last || l->w_nonce == 0u
        || nonce_last > last || l->w_addr_hi == 0u || l->w_addr_hi > last
        || l->w_addr_lo == 0u || l->w_addr_lo > last || l->w_size == 0u || l->w_size > last) {
        return TFW_ELE_E_LAYOUT;
    }
    msg_start(m, l->command, l->cmd_words);
    if (l->w_session != 0u) {
        m->w[l->w_session] = session;
    }
    for (i = 0u; i < nonce_len; i++) {
        m->w[l->w_nonce + i / 4u] |= (uint32_t)nonce[i] << (8u * (i % 4u));
    }
    m->w[l->w_addr_hi] = (uint32_t)(buf_addr >> 32);
    m->w[l->w_addr_lo] = (uint32_t)(buf_addr & 0xFFFFFFFFu);
    m->w[l->w_size]    = l->report_len;
    if (l->cmd_words > TFW_ELE_WORDS_WITHOUT_CRC) {
        msg_crc(m, l->cmd_words);
    }
    return 0;
}

int32_t tfw_ele_dev_attest(const struct tfw_ele_chan *c, const struct tfw_ele_attest_layout *l,
                           uint32_t session, const uint8_t *nonce, uint32_t nonce_len,
                           uint64_t buf_addr, uint32_t buf_len, uint32_t *status)
{
    struct tfw_ele_msg cmd, rsp;
    uint32_t word = 0u;
    uint32_t *const sp = (status != NULL) ? status : &word;
    int32_t rc;

    if (l->w_session != 0u && session == 0u) {
        return TFW_ELE_E_SESSION;
    }
    rc = tfw_ele_dev_attest_cmd(&cmd, l, session, nonce, nonce_len, buf_addr);
    if (rc != 0) {
        return rc;
    }
    if (buf_len < l->report_len) {
        return TFW_ELE_E_BUFFER;
    }
    rc = tfw_ele_call(c, &cmd, l->rsp_words, &rsp, sp);
    if (rc == TFW_ELE_E_STATUS && TFW_ELE_RATING(*sp) == TFW_ELE_RATING_UNKNOWN_HANDLE) {
        rc = TFW_ELE_E_SESSION;
    }
    return rc;
}
