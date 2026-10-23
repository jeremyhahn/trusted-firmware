/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The EdgeLock Enclave's message layer (src/hal/cortex-m/imx95/ele_mu.c),
 * against the register model's ELE MU and an enclave written here. Every
 * word is a literal written out from NXP's sources as ele_mu.h cites them:
 * the header's bytes are version, size in words, command, tag, so GET_INFO's
 * request header is 0x17DA0406 (version 0x06, 4 words, command 0xDA, tag
 * 0x17) and its response's 0xE1DA0206 (tag 0xE1, 2 words); a status word's
 * low byte is 0xD6 for success and 0x29 for failure, its rating above. The
 * MU's PAR is +0x004, TSR +0x124, RSR +0x12C, TR +0x200, RR +0x280 (Linux
 * imx-mailbox.c's ELE MU; unconfirmed against IMX95RM). The information
 * buffer's offsets are imx-secure-enclave's struct dev_info and struct
 * dev_addn_info. The wait's bound is pinned as 100000 reads. No value is
 * taken from the code under test.
 *
 * The MU's base and PAR are the test's: no document gives the M7 an ELE MU
 * (docs/multi-arch/imx95-design.md question 3).
 */
#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "ele_mu.h"

#define MU 0x60200000u

static struct tfw_imx95_model *m;
static struct tfw_ele_chan chan;

/* What the enclave below answers. */
static uint32_t reply[8];
static uint32_t reply_n;
static uint8_t  info_out[256];   /* the buffer the enclave writes, as DMA would */
static uint8_t  info_image[256];
static uint32_t write_info;
static uint8_t  pub_out[64];     /* the caller's public key buffer, written as DMA would */
static uint8_t  pub_image[64];
static uint32_t write_pub;
static uint8_t  sig_out[64];     /* the caller's signature buffer, written as DMA would */
static uint8_t  sig_image[64];
static uint32_t write_sig;

static void enclave(struct tfw_imx95_model *mm)
{
    uint32_t i;

    for (i = 0u; i < reply_n; i++) {
        mm->ele.rsp[i] = reply[i];
    }
    mm->ele.rsp_len = reply_n;
    mm->ele.rsp_pos = 0u;
    if (write_info) {
        memcpy(info_out, info_image, sizeof info_out);
    }
    if (write_pub) {
        memcpy(pub_out, pub_image, sizeof pub_out);
    }
    if (write_sig) {
        memcpy(sig_out, sig_image, sizeof sig_out);
    }
}

void setUp(void)
{
    tfw_imx95_model_reset();
    m = tfw_imx95_model_get();
    m->ele.mu_base = MU;
    m->ele.par     = 0x00000408u;   /* 8 transmit registers, 4 receive */
    m->ele.enclave = enclave;
    chan.mu = MU;
    reply[0] = 0xE1DA0206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
    write_info = 0u;
    memset(info_out, 0, sizeof info_out);
    memset(info_image, 0, sizeof info_image);
    write_pub = 0u;
    memset(pub_out, 0, sizeof pub_out);
    memset(pub_image, 0, sizeof pub_image);
    write_sig = 0u;
    memset(sig_out, 0, sizeof sig_out);
    memset(sig_image, 0, sizeof sig_image);
}

void tearDown(void) {}

static void test_get_info_is_four_words_through_tr0_to_tr3(void)
{
    uint32_t status = 0u;

    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_get_info(&chan, 0x0000000120345600ull, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(4u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17DA0406u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->ele.req[1]);   /* address, high word */
    TEST_ASSERT_EQUAL_HEX32(0x20345600u, m->ele.req[2]);   /* address, low word */
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, m->ele.req[3]);   /* 256 bytes */
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.requests);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);          /* both words taken */
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_words_wrap_round_the_registers_par_gives(void)
{
    m->ele.par = 0x00000102u;   /* 2 transmit registers, 1 receive */

    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_get_info(&chan, 0u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(4u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, m->ele.req[3]);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_failure_status_is_refused_with_its_word(void)
{
    uint32_t status = 0u;

    reply[1] = 0x00000D29u;   /* failure, rating 0x0D: command not supported */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS, tfw_ele_get_info(&chan, 0u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000D29u, status);
}

static void test_a_wrong_tag_is_refused(void)
{
    reply[0] = 0x17DA0206u;   /* a command's tag on the response */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TAG, tfw_ele_get_info(&chan, 0u, NULL));
}

static void test_a_wrong_version_is_refused(void)
{
    reply[0] = 0xE1DA0207u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_VERSION, tfw_ele_get_info(&chan, 0u, NULL));
}

static void test_a_wrong_size_is_refused_unread(void)
{
    reply[0] = 0xE1DA0306u;   /* three words where GET_INFO's response is two */
    reply[2] = 0u;
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_get_info(&chan, 0u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);   /* the header alone */
}

static void test_a_response_to_another_command_is_refused(void)
{
    reply[0] = 0xE1160206u;   /* command 0x16 */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_get_info(&chan, 0u, NULL));
}

static void test_a_response_that_never_comes_times_out(void)
{
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_get_info(&chan, 0u, NULL));
    TEST_ASSERT_EQUAL_UINT32(4u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_response_cut_short_times_out(void)
{
    reply_n = 1u;   /* the header, and no status word after it */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_get_info(&chan, 0u, NULL));
}

static void test_a_transmit_register_never_empty_times_out_unsent(void)
{
    m->ele.tx_busy = 1u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_get_info(&chan, 0u, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_an_mu_without_registers_is_refused_unsent(void)
{
    m->ele.par = 0x00000008u;   /* no receive register */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_MU, tfw_ele_get_info(&chan, 0u, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_request_longer_than_the_client_takes_is_not_sent(void)
{
    struct tfw_ele_msg cmd, rsp;

    memset(&cmd, 0, sizeof cmd);
    cmd.w[0] = 0x17DA0F06u;   /* fifteen words */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_LENGTH, tfw_ele_call(&chan, &cmd, 2u, &rsp, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_the_information_buffer_parses(void)
{
    struct tfw_ele_info info;
    uint32_t i;

    info_image[0]  = 0xDA;                       /* dev_info's cmd, ver, length */
    info_image[4]  = 0x34; info_image[5]  = 0x12;   /* soc_id 0x1234 */
    info_image[6]  = 0xA1; info_image[7]  = 0x00;   /* soc_rev 0x00A1 */
    info_image[8]  = 0x40; info_image[9]  = 0x00;   /* lmda: OEM closed */
    info_image[10] = 0x05;                       /* ssm_state */
    for (i = 0u; i < 16u; i++) {
        info_image[12u + i] = (uint8_t)(0x10u + i);   /* uid */
    }
    for (i = 0u; i < 32u; i++) {
        info_image[28u + i] = 0xEE;                   /* sha_rom_patch, not kept */
        info_image[60u + i] = (uint8_t)(0x60u + i);   /* sha_fw */
    }
    info_image[156] = 0x02;                      /* trng_state */
    info_image[157] = 0x03;                      /* csal_state */
    info_image[158] = 0x04;                      /* imem_state */
    write_info = 1u;

    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_get_info(&chan, 0u, NULL));
    tfw_ele_get_info_parse(info_out, &info);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, info.soc_id);
    TEST_ASSERT_EQUAL_HEX16(0x00A1u, info.soc_rev);
    TEST_ASSERT_EQUAL_HEX16(0x0040u, info.lmda);
    TEST_ASSERT_EQUAL_HEX8(0x05u, info.ssm_state);
    TEST_ASSERT_EQUAL_HEX8(0x10u, info.uid[0]);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, info.uid[15]);
    TEST_ASSERT_EQUAL_HEX8(0x60u, info.sha_fw[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, info.sha_fw[31]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, info.trng_state);
    TEST_ASSERT_EQUAL_HEX8(0x03u, info.csal_state);
    TEST_ASSERT_EQUAL_HEX8(0x04u, info.imem_state);
}

/* SESSION_OPEN, sab_session.h's struct sab_cmd_session_open_msg in the ELE's
 * PSA_COMPLIANT layout: header 0x17100306 (3 words, command 0x10), then
 * rsv, interrupt_idx, rsv1, then priority, operating_mode, rsv2. Its response
 * 0xE1100306 is the header, the status word and the session handle.
 * SESSION_CLOSE is 0x17110206 and the handle; its response 0xE1110206. */
static void open_reply(uint32_t handle)
{
    reply[0] = 0xE1100306u;
    reply[1] = 0x000000D6u;
    reply[2] = handle;
    reply_n  = 3u;
}

static void close_reply(void)
{
    reply[0] = 0xE1110206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
}

static void test_a_good_open_returns_the_handle(void)
{
    uint32_t handle = 0u, status = 0u;

    open_reply(0x8A3C1D02u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 5u, &handle, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17100306u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00000500u, m->ele.req[1]);   /* interrupt_idx 5 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[2]);   /* priority, mode */
    TEST_ASSERT_EQUAL_HEX32(0x8A3C1D02u, handle);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_open_leaves_the_handle(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    open_reply(0x8A3C1D02u);
    reply[1] = 0x00000429u;   /* failure, rating 0x04: invalid parameter */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS, tfw_ele_session_open(&chan, 5u, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_an_open_answered_short_is_refused_unread(void)
{
    uint32_t handle = 0x0BADu;

    open_reply(0x8A3C1D02u);
    reply[0] = 0xE1100206u;   /* two words, no handle */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_session_open(&chan, 5u, &handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_an_open_answered_as_a_close_is_refused(void)
{
    uint32_t handle = 0x0BADu;

    open_reply(0x8A3C1D02u);
    reply[0] = 0xE1110306u;   /* command 0x11 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_session_open(&chan, 5u, &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_an_open_never_answered_times_out(void)
{
    uint32_t handle = 0x0BADu;

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_session_open(&chan, 5u, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_good_close_sends_the_handle(void)
{
    uint32_t status = 0u;

    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, 0x8A3C1D02u, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x8A3C1D02u, m->ele.req[1]);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_close_the_enclave_refuses_is_a_handle_error(void)
{
    uint32_t status = 0u;

    close_reply();
    reply[1] = 0x00000429u;   /* failure, rating 0x04: invalid parameter */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_HANDLE, tfw_ele_session_close(&chan, 0x12345678u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
}

static void test_a_close_answered_long_is_refused_unread(void)
{
    close_reply();
    reply[0] = 0xE1110306u;   /* three words where the close's response is two */
    reply[2] = 0u;
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_session_close(&chan, 0x8A3C1D02u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_close_answered_as_an_open_is_refused(void)
{
    close_reply();
    reply[0] = 0xE1100206u;   /* command 0x10 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_session_close(&chan, 0x8A3C1D02u, NULL));
}

static void test_a_close_never_answered_times_out(void)
{
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_session_close(&chan, 0x8A3C1D02u, NULL));
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);   /* sent in full */
}

static void test_the_handle_the_open_returns_is_the_one_closed(void)
{
    uint32_t handle = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &handle, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);
}

/* KEY_STORE_OPEN, sab_key_store.h's struct sab_cmd_key_store_open_msg in
 * the PSA_COMPLIANT layout: header 0x17300606 (6 words, command 0x30), the
 * session handle, the key store id, the nonce, then rsv1, flags, rsv2 (flags
 * in the third byte: CREATE 0x01, LOAD 0x00, hsm_key_store.h), then the CRC
 * word, the XOR of the five before it (ele_utils.c plat_add_msg_crc). For
 * session 0x8A3C1D02, id 0x95 and nonce 0x1234ABCD that is 0x8F39B05C when
 * creating and 0x8F38B05C when loading. Its response 0xE1300306 is the
 * header, the status word and the key store handle. KEY_STORE_CLOSE is
 * 0x17310206 and the handle; its response 0xE1310206. A failure rated 0x07
 * is SAB_UNKNOWN_HANDLE (err.h), 0x08 SAB_UNKNOWN_KEY_STORE. */
static void ks_open_reply(uint32_t handle)
{
    reply[0] = 0xE1300306u;
    reply[1] = 0x000000D6u;
    reply[2] = handle;
    reply_n  = 3u;
}

static void ks_close_reply(void)
{
    reply[0] = 0xE1310206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
}

static void test_a_good_key_store_create_returns_the_handle(void)
{
    uint32_t handle = 0u, status = 0u;

    ks_open_reply(0x5EC0057Au);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                      &handle, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17300606u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x8A3C1D02u, m->ele.req[1]);   /* session handle */
    TEST_ASSERT_EQUAL_HEX32(0x00000095u, m->ele.req[2]);   /* key store id */
    TEST_ASSERT_EQUAL_HEX32(0x1234ABCDu, m->ele.req[3]);   /* nonce */
    TEST_ASSERT_EQUAL_HEX32(0x00010000u, m->ele.req[4]);   /* flags: CREATE */
    TEST_ASSERT_EQUAL_HEX32(0x8F39B05Cu, m->ele.req[5]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x5EC0057Au, handle);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_key_store_load_sends_no_flag(void)
{
    uint32_t handle = 0u;

    ks_open_reply(0x5EC0057Au);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 0u,
                                                      &handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[4]);   /* flags: LOAD */
    TEST_ASSERT_EQUAL_HEX32(0x8F38B05Cu, m->ele.req[5]);
    TEST_ASSERT_EQUAL_HEX32(0x5EC0057Au, handle);
}

static void test_a_refused_key_store_open_leaves_the_handle(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    ks_open_reply(0x5EC0057Au);
    reply[1] = 0x00000829u;   /* failure, rating 0x08: no such key store */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 0u,
                                                   &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000829u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_on_a_closed_session_is_a_session_error(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    ks_open_reply(0x5EC0057Au);
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SESSION,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                   &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_on_a_closed_session_without_status_is_a_session_error(void)
{
    uint32_t handle = 0x0BADu;

    ks_open_reply(0x5EC0057Au);
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SESSION,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                   &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_with_no_session_is_not_sent(void)
{
    uint32_t handle = 0x0BADu;

    ks_open_reply(0x5EC0057Au);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SESSION,
                            tfw_ele_key_store_open(&chan, 0u, 0x95u, 0x1234ABCDu, 1u, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_answered_short_is_refused_unread(void)
{
    uint32_t handle = 0x0BADu;

    ks_open_reply(0x5EC0057Au);
    reply[0] = 0xE1300206u;   /* two words, no handle */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                   &handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_answered_as_a_close_is_refused(void)
{
    uint32_t handle = 0x0BADu;

    ks_open_reply(0x5EC0057Au);
    reply[0] = 0xE1310306u;   /* command 0x31 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                   &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_store_open_never_answered_times_out(void)
{
    uint32_t handle = 0x0BADu;

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT,
                            tfw_ele_key_store_open(&chan, 0x8A3C1D02u, 0x95u, 0x1234ABCDu, 1u,
                                                   &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_good_key_store_close_sends_the_handle(void)
{
    uint32_t status = 0u;

    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, 0x5EC0057Au, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17310206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5EC0057Au, m->ele.req[1]);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_key_store_close_the_enclave_refuses_is_a_handle_error(void)
{
    uint32_t status = 0u;

    ks_close_reply();
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_HANDLE, tfw_ele_key_store_close(&chan, 0x12345678u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_key_store_close_answered_long_is_refused_unread(void)
{
    ks_close_reply();
    reply[0] = 0xE1310306u;   /* three words where the close's response is two */
    reply[2] = 0u;
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_key_store_close(&chan, 0x5EC0057Au, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_key_store_close_answered_as_a_session_close_is_refused(void)
{
    ks_close_reply();
    reply[0] = 0xE1110206u;   /* command 0x11 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_key_store_close(&chan, 0x5EC0057Au, NULL));
}

static void test_a_key_store_close_never_answered_times_out(void)
{
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_key_store_close(&chan, 0x5EC0057Au, NULL));
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);   /* sent in full */
}

static void test_the_handles_flow_session_to_key_store_and_back(void)
{
    uint32_t session = 0u, store = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &session, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    ks_open_reply(0x0000BEEFu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, session, 0x95u, 0x1234ABCDu, 1u,
                                                      &store, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);   /* the session's handle */
    m->ele.req_len = 0u;
    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, store, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x17310206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);   /* the store's handle */
    m->ele.req_len = 0u;
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, session, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(4u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);
}

static void km_open_reply(uint32_t handle)
{
    reply[0] = 0xE1400306u;
    reply[1] = 0x000000D6u;
    reply[2] = handle;
    reply_n  = 3u;
}

static void km_close_reply(void)
{
    reply[0] = 0xE1410206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
}

static void test_a_good_key_management_open_returns_the_handle(void)
{
    uint32_t handle = 0u, status = 0u;

    km_open_reply(0x4B3D0001u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17400606u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5EC0057Au, m->ele.req[1]);   /* key store handle */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[2]);   /* input_address_ext */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[3]);   /* output_address_ext */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[4]);   /* flags, rsv */
    TEST_ASSERT_EQUAL_HEX32(0x4980037Cu, m->ele.req[5]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x4B3D0001u, handle);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_key_management_open_leaves_the_handle(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    km_open_reply(0x4B3D0001u);
    reply[1] = 0x00000429u;   /* failure, rating 0x04 */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS,
                            tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_on_a_closed_store_is_a_key_store_error(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    km_open_reply(0x4B3D0001u);
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE,
                            tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_on_a_closed_store_without_status_is_a_key_store_error(void)
{
    uint32_t handle = 0x0BADu;

    km_open_reply(0x4B3D0001u);
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE,
                            tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_with_no_store_is_not_sent(void)
{
    uint32_t handle = 0x0BADu;

    km_open_reply(0x4B3D0001u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE, tfw_ele_key_mgmt_open(&chan, 0u, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_answered_short_is_refused_unread(void)
{
    uint32_t handle = 0x0BADu;

    km_open_reply(0x4B3D0001u);
    reply[0] = 0xE1400206u;   /* two words, no handle */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_answered_as_a_key_store_open_is_refused(void)
{
    uint32_t handle = 0x0BADu;

    km_open_reply(0x4B3D0001u);
    reply[0] = 0xE1300306u;   /* command 0x30 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND,
                            tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_key_management_open_never_answered_times_out(void)
{
    uint32_t handle = 0x0BADu;

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT,
                            tfw_ele_key_mgmt_open(&chan, 0x5EC0057Au, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_good_key_management_close_sends_the_handle(void)
{
    uint32_t status = 0u;

    km_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_close(&chan, 0x4B3D0001u, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17410206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x4B3D0001u, m->ele.req[1]);   /* key management handle */
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);
}

static void test_a_key_management_close_the_enclave_refuses_is_a_handle_error(void)
{
    uint32_t status = 0u;

    km_close_reply();
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_HANDLE, tfw_ele_key_mgmt_close(&chan, 0x12345678u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_key_management_close_answered_long_is_refused_unread(void)
{
    km_close_reply();
    reply[0] = 0xE1410306u;   /* three words where the close's response is two */
    reply[2] = 0u;
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_key_mgmt_close(&chan, 0x4B3D0001u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_key_management_close_answered_as_a_key_store_close_is_refused(void)
{
    km_close_reply();
    reply[0] = 0xE1310206u;   /* command 0x31 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_key_mgmt_close(&chan, 0x4B3D0001u, NULL));
}

static void test_a_key_management_close_never_answered_times_out(void)
{
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_key_mgmt_close(&chan, 0x4B3D0001u, NULL));
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);   /* sent in full */
}

static void test_the_handles_flow_session_to_key_management_and_back(void)
{
    uint32_t session = 0u, store = 0u, mgmt = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &session, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    ks_open_reply(0x0000BEEFu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, session, 0x95u, 0x1234ABCDu, 1u,
                                                      &store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    km_open_reply(0x0000CAFEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_open(&chan, store, &mgmt, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x17400606u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);   /* the store's handle */
    TEST_ASSERT_EQUAL_HEX32(0x1740B8E9u, m->ele.req[5]);   /* CRC */
    m->ele.req_len = 0u;
    km_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_close(&chan, mgmt, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x17410206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000CAFEu, m->ele.req[1]);   /* the service's handle */
    m->ele.req_len = 0u;
    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, store, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);
    m->ele.req_len = 0u;
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, session, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);
}

/* KEY_GENERATE's request is twelve words, 0x17420C06; its response four,
 * 0xE1420406, the last word's low half the public key size the enclave
 * expected (sab_key_generate.h, PSA_COMPLIANT). The key asked for: id 0,
 * group 7, NIST P-256 (type 0x7112, 256 bits), volatile, usage sign and
 * verify hash (0x00003000), ECDSA SHA-256 (0x06000609), into a 64-byte
 * buffer the enclave sees at 0x20480000. */
static struct tfw_ele_key_gen p256(void)
{
    struct tfw_ele_key_gen k;

    k.key_id   = 0x00000000u;
    k.key_type = 0x7112u;
    k.lifetime = 0x00000000u;
    k.usage    = 0x00003000u;
    k.algo     = 0x06000609u;
    k.group    = 7u;
    k.pub_size = 64u;
    k.pub_addr = 0x20480000u;
    return k;
}

static void gen_reply(uint32_t key_id)
{
    uint32_t i;

    reply[0] = 0xE1420406u;
    reply[1] = 0x000000D6u;
    reply[2] = key_id;
    reply[3] = 0x00000040u;   /* exp_pub_key_sz 64, exp_priv_key_sz 0 */
    reply_n  = 4u;
    for (i = 0u; i < sizeof pub_image; i++) {
        pub_image[i] = (uint8_t)(0xA0u + i);
    }
    write_pub = 1u;
}

static void test_a_good_key_generate_returns_the_id_and_the_public_key(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0u, status = 0u, i;

    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(12u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17420C06u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x4B3D0001u, m->ele.req[1]);    /* key management handle */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[2]);    /* key id */
    TEST_ASSERT_EQUAL_HEX32(0x00070040u, m->ele.req[3]);    /* pub_key_sz 64, group 7 */
    TEST_ASSERT_EQUAL_HEX32(0x01007112u, m->ele.req[4]);    /* type 0x7112, 256 bits */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[5]);    /* lifetime: volatile */
    TEST_ASSERT_EQUAL_HEX32(0x00003000u, m->ele.req[6]);    /* usage */
    TEST_ASSERT_EQUAL_HEX32(0x06000609u, m->ele.req[7]);    /* permitted algorithm */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[8]);    /* lifecycle: current */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[9]);    /* flags, rsv, priv_key_sz */
    TEST_ASSERT_EQUAL_HEX32(0x20480000u, m->ele.req[10]);   /* public key buffer */
    TEST_ASSERT_EQUAL_HEX32(0x7B304B5Cu, m->ele.req[11]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, id);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(4u, m->ele.rsp_pos);
    for (i = 0u; i < 64u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u + i), pub_out[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_key_generate_leaves_the_id(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu, status = 0u;

    gen_reply(0x00010042u);
    reply[1] = 0x00000429u;   /* failure, rating 0x04 */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_on_a_closed_service_is_a_key_management_error(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu, status = 0u;

    gen_reply(0x00010042u);
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_MGMT,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_on_a_closed_service_without_status_is_a_key_management_error(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    gen_reply(0x00010042u);
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_MGMT,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_with_no_service_is_not_sent(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_MGMT, tfw_ele_key_generate(&chan, 0u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_into_a_buffer_too_small_is_not_sent(void)
{
    struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    k.pub_size = 63u;
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_BUFFER,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
    TEST_ASSERT_EQUAL_HEX8(0x00u, pub_out[0]);
}

static void test_a_key_type_other_than_p256_is_not_sent(void)
{
    struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    k.key_type = 0x2400u;   /* HSM_KEY_TYPE_AES */
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_persistent_key_is_not_sent(void)
{
    struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    k.lifetime = 0x00000001u;   /* HSM_SE_KEY_STORAGE_PERSISTENT */
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    k.lifetime = 0x00000101u;   /* the library's persistent test vectors' value */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_answered_short_is_refused_unread(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    gen_reply(0x00010042u);
    reply[0] = 0xE1420306u;   /* three words, as the non-PSA layout */
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_answered_as_a_key_management_open_is_refused(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    gen_reply(0x00010042u);
    reply[0] = 0xE1400406u;   /* KEY_MANAGEMENT_OPEN's id */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_a_key_generate_never_answered_times_out(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t id = 0x0BADu;

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT,
                            tfw_ele_key_generate(&chan, 0x4B3D0001u, &k, &id, NULL));
    TEST_ASSERT_EQUAL_UINT32(12u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, id);
}

static void test_the_handles_flow_session_to_a_generated_key_and_back(void)
{
    const struct tfw_ele_key_gen k = p256();
    uint32_t session = 0u, store = 0u, mgmt = 0u, id = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &session, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    ks_open_reply(0x0000BEEFu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, session, 0x95u, 0x1234ABCDu, 1u,
                                                      &store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    km_open_reply(0x0000CAFEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_open(&chan, store, &mgmt, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_generate(&chan, mgmt, &k, &id, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x17420C06u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000CAFEu, m->ele.req[1]);    /* the service's handle */
    TEST_ASSERT_EQUAL_HEX32(0x300D81A3u, m->ele.req[11]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, id);
    TEST_ASSERT_EQUAL_HEX8(0xDFu, pub_out[63]);
    write_pub = 0u;
    m->ele.req_len = 0u;
    km_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_close(&chan, mgmt, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000CAFEu, m->ele.req[1]);
    m->ele.req_len = 0u;
    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, store, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);
    m->ele.req_len = 0u;
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, session, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(7u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);
}

/* --- signature generation service (0x70, 0x71) ------------------------------ */

static void sg_open_reply(uint32_t handle)
{
    reply[0] = 0xE1700306u;
    reply[1] = 0x000000D6u;
    reply[2] = handle;
    reply_n  = 3u;
}

static void sg_close_reply(void)
{
    reply[0] = 0xE1710206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
}

static void test_a_good_signature_open_returns_the_handle(void)
{
    uint32_t handle = 0u, status = 0u;

    sg_open_reply(0x5163A001u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17700606u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5EC0057Au, m->ele.req[1]);   /* key store handle */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[2]);   /* input_address_ext */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[3]);   /* output_address_ext */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[4]);   /* flags, reserved */
    TEST_ASSERT_EQUAL_HEX32(0x49B0037Cu, m->ele.req[5]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x5163A001u, handle);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_signature_open_leaves_the_handle(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    sg_open_reply(0x5163A001u);
    reply[1] = 0x00000429u;   /* failure, rating 0x04: invalid parameters */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS,
                            tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_signature_open_on_a_closed_store_is_a_key_store_error(void)
{
    uint32_t handle = 0x0BADu, status = 0u;

    sg_open_reply(0x5163A001u);
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE,
                            tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_signature_open_with_no_store_is_not_sent(void)
{
    uint32_t handle = 0x0BADu;

    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE, tfw_ele_sign_gen_open(&chan, 0u, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_signature_open_answered_short_is_refused_unread(void)
{
    uint32_t handle = 0x0BADu;

    sg_open_reply(0x5163A001u);
    reply[0] = 0xE1700206u;   /* two words where the open's response is three */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE,
                            tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_signature_open_answered_as_a_key_management_open_is_refused(void)
{
    uint32_t handle = 0x0BADu;

    sg_open_reply(0x5163A001u);
    reply[0] = 0xE1400306u;   /* command 0x40 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND,
                            tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, NULL));
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_signature_open_never_answered_times_out(void)
{
    uint32_t handle = 0x0BADu;

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT,
                            tfw_ele_sign_gen_open(&chan, 0x5EC0057Au, &handle, NULL));
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX32(0x0BADu, handle);
}

static void test_a_good_signature_close_sends_the_handle(void)
{
    uint32_t status = 0u;

    sg_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_close(&chan, 0x5163A001u, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17710206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5163A001u, m->ele.req[1]);   /* signature generation handle */
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.rsp_pos);
}

static void test_a_signature_close_on_a_closed_handle_is_a_handle_error(void)
{
    uint32_t status = 0u;

    sg_close_reply();
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_HANDLE, tfw_ele_sign_gen_close(&chan, 0x5163A001u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_signature_close_answered_long_is_refused_unread(void)
{
    sg_close_reply();
    reply[0] = 0xE1710306u;   /* three words where the close's response is two */
    reply[2] = 0u;
    reply_n  = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_sign_gen_close(&chan, 0x5163A001u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_signature_close_answered_as_a_key_management_close_is_refused(void)
{
    sg_close_reply();
    reply[0] = 0xE1410206u;   /* command 0x41 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_sign_gen_close(&chan, 0x5163A001u, NULL));
}

/* --- SIGNATURE_GENERATE (0x72) ---------------------------------------------- */

static struct tfw_ele_sign es256(void)
{
    struct tfw_ele_sign s;

    s.key_id      = 0x00010042u;
    s.key_type    = 0x7112u;
    s.key_bits    = 256u;
    s.scheme      = 0x06000609u;
    s.digest_addr = 0x20481000u;
    s.sig_addr    = 0x20482000u;
    s.sig_size    = 64u;
    return s;
}

static void sign_reply(void)
{
    uint32_t i;

    reply[0] = 0xE1720306u;
    reply[1] = 0x000000D6u;
    reply[2] = 0x00000040u;   /* signature_size 64, reserved 0 */
    reply_n  = 3u;
    for (i = 0u; i < sizeof sig_image; i++) {
        sig_image[i] = (uint8_t)(0x40u + i);
    }
    write_sig = 1u;
}

static void test_a_good_sign_returns_r_and_s_in_the_callers_buffer(void)
{
    const struct tfw_ele_sign s = es256();
    uint32_t status = 0u, i;

    sign_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(14u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17720E06u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5163A001u, m->ele.req[1]);    /* signature generation handle */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, m->ele.req[2]);    /* key id */
    TEST_ASSERT_EQUAL_HEX32(0x20481000u, m->ele.req[3]);    /* digest */
    TEST_ASSERT_EQUAL_HEX32(0x20482000u, m->ele.req[4]);    /* signature buffer */
    TEST_ASSERT_EQUAL_HEX32(0x00000020u, m->ele.req[5]);    /* message_size 32 */
    TEST_ASSERT_EQUAL_HEX32(0x00000040u, m->ele.req[6]);    /* signature_size 64, flags: digest */
    TEST_ASSERT_EQUAL_HEX32(0x06000609u, m->ele.req[7]);    /* ECDSA with SHA-256 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[8]);    /* salt_len, key_type */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[9]);    /* priv_key_size, security size */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[10]);   /* SM2 public key */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[11]);   /* SM2 identity */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[12]);   /* SM2 sizes */
    TEST_ASSERT_EQUAL_HEX32(0x4010982Cu, m->ele.req[13]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    for (i = 0u; i < 64u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x40u + i), sig_out[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_sign_is_a_status_error(void)
{
    const struct tfw_ele_sign s = es256();
    uint32_t status = 0u;

    sign_reply();
    write_sig = 0u;
    reply[1] = 0x00000429u;   /* failure, rating 0x04: invalid parameters */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX8(0x00u, sig_out[0]);
}

static void test_a_sign_on_a_closed_service_is_a_signature_service_error(void)
{
    const struct tfw_ele_sign s = es256();
    uint32_t status = 0u;

    sign_reply();
    write_sig = 0u;
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIGN_GEN,
                            tfw_ele_sign_generate(&chan, 0x5163A001u, &s, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_sign_with_no_service_is_not_sent(void)
{
    const struct tfw_ele_sign s = es256();

    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIGN_GEN, tfw_ele_sign_generate(&chan, 0u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_sign_with_a_key_type_other_than_nist_is_not_sent(void)
{
    struct tfw_ele_sign s = es256();

    s.key_type = 0x7130u;   /* HSM_KEY_TYPE_ECC_BP_R1 */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_sign_with_a_key_other_than_p256_is_not_sent(void)
{
    struct tfw_ele_sign s = es256();

    s.key_bits = 384u;   /* a P-384 key */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_sign_into_a_buffer_too_small_is_not_sent(void)
{
    struct tfw_ele_sign s = es256();

    s.sig_size = 63u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_BUFFER, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_sign_with_another_scheme_is_not_sent(void)
{
    struct tfw_ele_sign s = es256();

    s.scheme = 0x0600060Au;   /* HSM_SIGNATURE_SCHEME_ECDSA_SHA384 */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.req_len);
}

static void test_a_sign_answered_short_is_refused_unread(void)
{
    const struct tfw_ele_sign s = es256();

    sign_reply();
    reply[0] = 0xE1720206u;   /* two words, as the non-PSA layout */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_sign_answered_as_a_key_generate_is_refused(void)
{
    const struct tfw_ele_sign s = es256();

    sign_reply();
    reply[0] = 0xE1420306u;   /* command 0x42 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
}

static void test_a_sign_never_answered_times_out(void)
{
    const struct tfw_ele_sign s = es256();

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_sign_generate(&chan, 0x5163A001u, &s, NULL));
    TEST_ASSERT_EQUAL_UINT32(14u, m->ele.req_len);   /* sent in full */
    TEST_ASSERT_EQUAL_HEX8(0x00u, sig_out[0]);
}

static void test_the_handles_flow_session_to_a_signature_and_back(void)
{
    const struct tfw_ele_key_gen k = p256();
    struct tfw_ele_sign s = es256();
    uint32_t session = 0u, store = 0u, mgmt = 0u, id = 0u, sgen = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &session, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    ks_open_reply(0x0000BEEFu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, session, 0x95u, 0x1234ABCDu, 1u,
                                                      &store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    km_open_reply(0x0000CAFEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_open(&chan, store, &mgmt, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_generate(&chan, mgmt, &k, &id, NULL));
    write_pub = 0u;
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    sg_open_reply(0x0000F00Du);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_open(&chan, store, &sgen, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);    /* the key store's handle */
    m->ele.req_len = 0u;
    s.key_id = id;
    sign_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_generate(&chan, sgen, &s, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x17720E06u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000F00Du, m->ele.req[1]);    /* the service's handle */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, m->ele.req[2]);    /* the generated key */
    TEST_ASSERT_EQUAL_HEX32(0x1173C820u, m->ele.req[13]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, sig_out[63]);
    write_sig = 0u;
    m->ele.req_len = 0u;
    sg_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_close(&chan, sgen, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000F00Du, m->ele.req[1]);
    m->ele.req_len = 0u;
    km_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_close(&chan, mgmt, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, session, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(10u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);
}

/* PUB_KEY_EXPORT (0x32), on the key store handle */
static struct tfw_ele_key_export p256_export(void)
{
    struct tfw_ele_key_export x;

    x.key_id   = 0x00010042u;
    x.key_type = 0x7112u;
    x.key_bits = 256u;
    x.out_addr = 0x20483000u;
    x.out_size = 64u;
    return x;
}

static void export_reply(void)
{
    uint32_t i;

    reply[0] = 0xE1320306u;
    reply[1] = 0x000000D6u;
    reply[2] = 0x00000040u;   /* out_key_size 64, reserved 0 */
    reply_n  = 3u;
    for (i = 0u; i < sizeof pub_image; i++) {
        pub_image[i] = (uint8_t)(0xA0u + i);
    }
    write_pub = 1u;
}

static void test_a_good_export_returns_x_and_y_in_the_callers_buffer(void)
{
    const struct tfw_ele_key_export x = p256_export();
    uint32_t status = 0u, i;

    export_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(7u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17320706u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);    /* key store handle */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, m->ele.req[2]);    /* key id */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[3]);    /* address extension */
    TEST_ASSERT_EQUAL_HEX32(0x20483000u, m->ele.req[4]);    /* the caller's buffer */
    TEST_ASSERT_EQUAL_HEX32(0x00000040u, m->ele.req[5]);    /* out_key_size 64 */
    TEST_ASSERT_EQUAL_HEX32(0x377B89EBu, m->ele.req[6]);    /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    for (i = 0u; i < 64u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u + i), pub_out[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_refused_export_is_a_status_error(void)
{
    const struct tfw_ele_key_export x = p256_export();
    uint32_t status = 0u;

    export_reply();
    write_pub = 0u;
    reply[1] = 0x00000429u;   /* failure, rating 0x04: invalid parameters */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX8(0x00u, pub_out[0]);
}

static void test_an_export_on_a_closed_store_is_a_key_store_error(void)
{
    const struct tfw_ele_key_export x = p256_export();
    uint32_t status = 0u;

    export_reply();
    write_pub = 0u;
    reply[1] = 0x00000729u;   /* failure, rating 0x07: unknown handle */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE,
                            tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_an_export_with_no_store_is_not_sent(void)
{
    const struct tfw_ele_key_export x = p256_export();

    export_reply();
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_STORE, tfw_ele_pub_key_export(&chan, 0u, &x, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_an_export_of_a_key_type_other_than_nist_is_not_sent(void)
{
    struct tfw_ele_key_export x = p256_export();

    x.key_type = 0x7141u;   /* HSM_KEY_TYPE_ECC_MONTGOMERY */
    export_reply();
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_an_export_of_a_key_other_than_p256_is_not_sent(void)
{
    struct tfw_ele_key_export x = p256_export();

    x.key_bits = 384u;
    export_reply();
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_an_export_into_a_buffer_too_small_is_not_sent(void)
{
    struct tfw_ele_key_export x = p256_export();

    x.out_size = 63u;
    export_reply();
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_BUFFER, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX8(0x00u, pub_out[0]);
}

static void test_an_export_answered_short_is_refused_unread(void)
{
    const struct tfw_ele_key_export x = p256_export();

    export_reply();
    reply[0] = 0xE1320206u;   /* two words, as the non-PSA layout */
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_an_export_answered_as_a_key_generate_is_refused(void)
{
    const struct tfw_ele_key_export x = p256_export();

    export_reply();
    reply[0] = 0xE1420306u;   /* command 0x42 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
}

static void test_an_export_never_answered_times_out(void)
{
    const struct tfw_ele_key_export x = p256_export();

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_pub_key_export(&chan, 0x0000BEEFu, &x, NULL));
    TEST_ASSERT_EQUAL_UINT32(7u, m->ele.req_len);   /* sent in full */
}

/* SIGNATURE_VERIFICATION_OPEN (0x80) and CLOSE (0x81), on the session */
static void sv_open_reply(uint32_t handle)
{
    reply[0] = 0xE1800306u;
    reply[1] = 0x000000D6u;
    reply[2] = handle;
    reply_n  = 3u;
}

static void sv_close_reply(void)
{
    reply[0] = 0xE1810206u;
    reply[1] = 0x000000D6u;
    reply_n  = 2u;
}

static void test_a_good_verification_open_returns_the_handle(void)
{
    uint32_t handle = 0u, status = 0u;

    sv_open_reply(0x5E7A0001u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_ver_open(&chan, 0x00C0FFEEu, &handle, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(6u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17800606u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);    /* session handle */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[2]);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[3]);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[4]);    /* flags */
    TEST_ASSERT_EQUAL_HEX32(0x1740F9E8u, m->ele.req[5]);    /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x5E7A0001u, handle);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
}

static void test_a_verification_open_on_a_closed_session_is_a_session_error(void)
{
    uint32_t handle = 0x11111111u, status = 0u;

    sv_open_reply(0x5E7A0001u);
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SESSION,
                            tfw_ele_sign_ver_open(&chan, 0x00C0FFEEu, &handle, &status));
    TEST_ASSERT_EQUAL_HEX32(0x11111111u, handle);
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_verification_open_with_no_session_is_not_sent(void)
{
    uint32_t handle = 0x11111111u;

    sv_open_reply(0x5E7A0001u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SESSION, tfw_ele_sign_ver_open(&chan, 0u, &handle, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x11111111u, handle);
}

static void test_a_good_verification_close_sends_the_handle(void)
{
    sv_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_ver_close(&chan, 0x5E7A0001u, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(2u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17810206u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5E7A0001u, m->ele.req[1]);
}

static void test_a_verification_close_on_a_closed_handle_is_a_handle_error(void)
{
    uint32_t status = 0u;

    sv_close_reply();
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_HANDLE, tfw_ele_sign_ver_close(&chan, 0x5E7A0001u, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

/* SIGNATURE_VERIFY (0x82) */
static struct tfw_ele_verify es256_verify(void)
{
    struct tfw_ele_verify v;

    v.key_type    = 0x7112u;
    v.key_bits    = 256u;
    v.scheme      = 0x06000609u;
    v.key_addr    = 0x20483000u;
    v.digest_addr = 0x20481000u;
    v.sig_addr    = 0x20482000u;
    v.key_size    = 64u;
    v.sig_size    = 64u;
    return v;
}

static void verify_reply(uint32_t verdict)
{
    reply[0] = 0xE1820306u;
    reply[1] = 0x000000D6u;
    reply[2] = verdict;
    reply_n  = 3u;
}

static void test_a_good_verify_passes(void)
{
    const struct tfw_ele_verify v = es256_verify();
    uint32_t verdict = 0u, status = 0u;

    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, &verdict, &status));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(14u, m->ele.req_len);
    TEST_ASSERT_EQUAL_HEX32(0x17820E06u, m->ele.req[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5E7A0001u, m->ele.req[1]);    /* verification handle */
    TEST_ASSERT_EQUAL_HEX32(0x20483000u, m->ele.req[2]);    /* public key */
    TEST_ASSERT_EQUAL_HEX32(0x20481000u, m->ele.req[3]);    /* digest */
    TEST_ASSERT_EQUAL_HEX32(0x20482000u, m->ele.req[4]);    /* signature */
    TEST_ASSERT_EQUAL_HEX32(0x00000020u, m->ele.req[5]);    /* message_size 32 */
    TEST_ASSERT_EQUAL_HEX32(0x00400040u, m->ele.req[6]);    /* sig_size 64, key_size 64 */
    TEST_ASSERT_EQUAL_HEX32(0x41120100u, m->ele.req[7]);    /* 256 bits, ECC NIST public */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[8]);    /* flags: digest, plaintext key */
    TEST_ASSERT_EQUAL_HEX32(0x06000609u, m->ele.req[9]);    /* ECDSA with SHA-256 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[10]);   /* salt_len */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[11]);   /* SM2 identity */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->ele.req[12]);   /* its size */
    TEST_ASSERT_EQUAL_HEX32(0x2EE2096Eu, m->ele.req[13]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x5A3CC3A5u, verdict);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
    TEST_ASSERT_EQUAL_UINT32(3u, m->ele.rsp_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a_verify_that_fails_is_a_bad_signature_not_a_transport_error(void)
{
    const struct tfw_ele_verify v = es256_verify();
    uint32_t verdict = 0u, status = 0u;

    verify_reply(0x2B4DD4B2u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIG_BAD,
                            tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, &verdict, &status));
    TEST_ASSERT_EQUAL_HEX32(0x2B4DD4B2u, verdict);
    TEST_ASSERT_EQUAL_HEX32(0x000000D6u, status);
}

static void test_a_verify_status_neither_passed_nor_failed_is_a_bad_signature(void)
{
    const struct tfw_ele_verify v = es256_verify();

    verify_reply(0x00000000u);   /* the firmware's default, as the library says */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIG_BAD, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
}

static void test_a_refused_verify_is_a_status_error(void)
{
    const struct tfw_ele_verify v = es256_verify();
    uint32_t verdict = 0x11111111u, status = 0u;

    verify_reply(0x5A3CC3A5u);
    reply[1] = 0x00000429u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_STATUS,
                            tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, &verdict, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000429u, status);
    TEST_ASSERT_EQUAL_HEX32(0x11111111u, verdict);
}

static void test_a_verify_on_a_closed_service_is_a_verification_service_error(void)
{
    const struct tfw_ele_verify v = es256_verify();
    uint32_t status = 0u;

    verify_reply(0x5A3CC3A5u);
    reply[1] = 0x00000729u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIGN_VER,
                            tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, &status));
    TEST_ASSERT_EQUAL_HEX32(0x00000729u, status);
}

static void test_a_verify_with_no_service_is_not_sent(void)
{
    const struct tfw_ele_verify v = es256_verify();

    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIGN_VER, tfw_ele_sign_verify(&chan, 0u, &v, NULL, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a_verify_with_another_scheme_is_not_sent(void)
{
    struct tfw_ele_verify v = es256_verify();

    v.scheme = 0x06000610u;   /* ECDSA with SHA-512 */
    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a_verify_with_a_key_other_than_p256_is_not_sent(void)
{
    struct tfw_ele_verify v = es256_verify();

    v.key_bits = 384u;
    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_KEY_TYPE, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a_verify_with_a_short_key_is_not_sent(void)
{
    struct tfw_ele_verify v = es256_verify();

    v.key_size = 33u;   /* a compressed point */
    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_BUFFER, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a_verify_with_a_short_signature_is_not_sent(void)
{
    struct tfw_ele_verify v = es256_verify();

    v.sig_size = 63u;
    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_BUFFER, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a_verify_answered_short_is_refused_unread(void)
{
    const struct tfw_ele_verify v = es256_verify();

    verify_reply(0x5A3CC3A5u);
    reply[0] = 0xE1820206u;
    reply_n  = 2u;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_SIZE, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(1u, m->ele.rsp_pos);
}

static void test_a_verify_answered_as_a_sign_is_refused(void)
{
    const struct tfw_ele_verify v = es256_verify();

    verify_reply(0x5A3CC3A5u);
    reply[0] = 0xE1720306u;   /* command 0x72 echoed */
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_COMMAND, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
}

static void test_a_verify_never_answered_times_out(void)
{
    const struct tfw_ele_verify v = es256_verify();

    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT32(TFW_ELE_E_TIMEOUT, tfw_ele_sign_verify(&chan, 0x5E7A0001u, &v, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT32(14u, m->ele.req_len);   /* sent in full */
}

static void test_a_generated_key_signs_exports_and_verifies(void)
{
    const struct tfw_ele_key_gen k = p256();
    struct tfw_ele_sign s = es256();
    struct tfw_ele_key_export x = p256_export();
    const struct tfw_ele_verify v = es256_verify();
    uint32_t session = 0u, store = 0u, mgmt = 0u, id = 0u, sgen = 0u, sver = 0u, verdict = 0u;

    open_reply(0x00C0FFEEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_open(&chan, 0u, &session, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;   /* the model records one request */
    ks_open_reply(0x0000BEEFu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_open(&chan, session, 0x95u, 0x1234ABCDu, 1u,
                                                      &store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    km_open_reply(0x0000CAFEu);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_open(&chan, store, &mgmt, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    gen_reply(0x00010042u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_generate(&chan, mgmt, &k, &id, NULL));
    write_pub = 0u;
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    sg_open_reply(0x0000F00Du);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_open(&chan, store, &sgen, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    s.key_id = id;
    sign_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_generate(&chan, sgen, &s, NULL));
    write_sig = 0u;
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    sg_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_gen_close(&chan, sgen, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    x.key_id = id;
    export_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_pub_key_export(&chan, store, &x, NULL));
    write_pub = 0u;
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, m->ele.req[1]);    /* the key store's handle */
    TEST_ASSERT_EQUAL_HEX32(0x00010042u, m->ele.req[2]);    /* the generated key */
    TEST_ASSERT_EQUAL_HEX8(0xDFu, pub_out[63]);
    m->ele.req_len = 0u;
    sv_open_reply(0x0000D00Du);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_ver_open(&chan, session, &sver, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00C0FFEEu, m->ele.req[1]);    /* the session's handle */
    m->ele.req_len = 0u;
    verify_reply(0x5A3CC3A5u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_verify(&chan, sver, &v, &verdict, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000D00Du, m->ele.req[1]);    /* the service's handle */
    TEST_ASSERT_EQUAL_HEX32(0x20483000u, m->ele.req[2]);    /* the exported key */
    TEST_ASSERT_EQUAL_HEX32(0x20482000u, m->ele.req[4]);    /* the signature made */
    TEST_ASSERT_EQUAL_HEX32(0x7098D962u, m->ele.req[13]);   /* CRC */
    TEST_ASSERT_EQUAL_HEX32(0x5A3CC3A5u, verdict);
    m->ele.req_len = 0u;
    sv_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_sign_ver_close(&chan, sver, NULL));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0000D00Du, m->ele.req[1]);
    m->ele.req_len = 0u;
    km_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_mgmt_close(&chan, mgmt, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    ks_close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_key_store_close(&chan, store, NULL));
    m = tfw_imx95_model_get();
    m->ele.req_len = 0u;
    close_reply();
    TEST_ASSERT_EQUAL_INT32(0, tfw_ele_session_close(&chan, session, NULL));
    m = tfw_imx95_model_get();   /* the last read lands */
    TEST_ASSERT_EQUAL_UINT32(14u, m->ele.requests);
    TEST_ASSERT_EQUAL_HEX32(0x17110206u, m->ele.req[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_get_info_is_four_words_through_tr0_to_tr3);
    RUN_TEST(test_words_wrap_round_the_registers_par_gives);
    RUN_TEST(test_a_failure_status_is_refused_with_its_word);
    RUN_TEST(test_a_wrong_tag_is_refused);
    RUN_TEST(test_a_wrong_version_is_refused);
    RUN_TEST(test_a_wrong_size_is_refused_unread);
    RUN_TEST(test_a_response_to_another_command_is_refused);
    RUN_TEST(test_a_response_that_never_comes_times_out);
    RUN_TEST(test_a_response_cut_short_times_out);
    RUN_TEST(test_a_transmit_register_never_empty_times_out_unsent);
    RUN_TEST(test_an_mu_without_registers_is_refused_unsent);
    RUN_TEST(test_a_request_longer_than_the_client_takes_is_not_sent);
    RUN_TEST(test_the_information_buffer_parses);
    RUN_TEST(test_a_good_open_returns_the_handle);
    RUN_TEST(test_a_refused_open_leaves_the_handle);
    RUN_TEST(test_an_open_answered_short_is_refused_unread);
    RUN_TEST(test_an_open_answered_as_a_close_is_refused);
    RUN_TEST(test_an_open_never_answered_times_out);
    RUN_TEST(test_a_good_close_sends_the_handle);
    RUN_TEST(test_a_close_the_enclave_refuses_is_a_handle_error);
    RUN_TEST(test_a_close_answered_long_is_refused_unread);
    RUN_TEST(test_a_close_answered_as_an_open_is_refused);
    RUN_TEST(test_a_close_never_answered_times_out);
    RUN_TEST(test_the_handle_the_open_returns_is_the_one_closed);
    RUN_TEST(test_a_good_key_store_create_returns_the_handle);
    RUN_TEST(test_a_key_store_load_sends_no_flag);
    RUN_TEST(test_a_refused_key_store_open_leaves_the_handle);
    RUN_TEST(test_a_key_store_open_on_a_closed_session_is_a_session_error);
    RUN_TEST(test_a_key_store_open_on_a_closed_session_without_status_is_a_session_error);
    RUN_TEST(test_a_key_store_open_with_no_session_is_not_sent);
    RUN_TEST(test_a_key_store_open_answered_short_is_refused_unread);
    RUN_TEST(test_a_key_store_open_answered_as_a_close_is_refused);
    RUN_TEST(test_a_key_store_open_never_answered_times_out);
    RUN_TEST(test_a_good_key_store_close_sends_the_handle);
    RUN_TEST(test_a_key_store_close_the_enclave_refuses_is_a_handle_error);
    RUN_TEST(test_a_key_store_close_answered_long_is_refused_unread);
    RUN_TEST(test_a_key_store_close_answered_as_a_session_close_is_refused);
    RUN_TEST(test_a_key_store_close_never_answered_times_out);
    RUN_TEST(test_the_handles_flow_session_to_key_store_and_back);
    RUN_TEST(test_a_good_key_management_open_returns_the_handle);
    RUN_TEST(test_a_refused_key_management_open_leaves_the_handle);
    RUN_TEST(test_a_key_management_open_on_a_closed_store_is_a_key_store_error);
    RUN_TEST(test_a_key_management_open_on_a_closed_store_without_status_is_a_key_store_error);
    RUN_TEST(test_a_key_management_open_with_no_store_is_not_sent);
    RUN_TEST(test_a_key_management_open_answered_short_is_refused_unread);
    RUN_TEST(test_a_key_management_open_answered_as_a_key_store_open_is_refused);
    RUN_TEST(test_a_key_management_open_never_answered_times_out);
    RUN_TEST(test_a_good_key_management_close_sends_the_handle);
    RUN_TEST(test_a_key_management_close_the_enclave_refuses_is_a_handle_error);
    RUN_TEST(test_a_key_management_close_answered_long_is_refused_unread);
    RUN_TEST(test_a_key_management_close_answered_as_a_key_store_close_is_refused);
    RUN_TEST(test_a_key_management_close_never_answered_times_out);
    RUN_TEST(test_the_handles_flow_session_to_key_management_and_back);
    RUN_TEST(test_a_good_key_generate_returns_the_id_and_the_public_key);
    RUN_TEST(test_a_refused_key_generate_leaves_the_id);
    RUN_TEST(test_a_key_generate_on_a_closed_service_is_a_key_management_error);
    RUN_TEST(test_a_key_generate_on_a_closed_service_without_status_is_a_key_management_error);
    RUN_TEST(test_a_key_generate_with_no_service_is_not_sent);
    RUN_TEST(test_a_key_generate_into_a_buffer_too_small_is_not_sent);
    RUN_TEST(test_a_key_type_other_than_p256_is_not_sent);
    RUN_TEST(test_a_persistent_key_is_not_sent);
    RUN_TEST(test_a_key_generate_answered_short_is_refused_unread);
    RUN_TEST(test_a_key_generate_answered_as_a_key_management_open_is_refused);
    RUN_TEST(test_a_key_generate_never_answered_times_out);
    RUN_TEST(test_the_handles_flow_session_to_a_generated_key_and_back);
    RUN_TEST(test_a_good_signature_open_returns_the_handle);
    RUN_TEST(test_a_refused_signature_open_leaves_the_handle);
    RUN_TEST(test_a_signature_open_on_a_closed_store_is_a_key_store_error);
    RUN_TEST(test_a_signature_open_with_no_store_is_not_sent);
    RUN_TEST(test_a_signature_open_answered_short_is_refused_unread);
    RUN_TEST(test_a_signature_open_answered_as_a_key_management_open_is_refused);
    RUN_TEST(test_a_signature_open_never_answered_times_out);
    RUN_TEST(test_a_good_signature_close_sends_the_handle);
    RUN_TEST(test_a_signature_close_on_a_closed_handle_is_a_handle_error);
    RUN_TEST(test_a_signature_close_answered_long_is_refused_unread);
    RUN_TEST(test_a_signature_close_answered_as_a_key_management_close_is_refused);
    RUN_TEST(test_a_good_sign_returns_r_and_s_in_the_callers_buffer);
    RUN_TEST(test_a_refused_sign_is_a_status_error);
    RUN_TEST(test_a_sign_on_a_closed_service_is_a_signature_service_error);
    RUN_TEST(test_a_sign_with_no_service_is_not_sent);
    RUN_TEST(test_a_sign_with_a_key_type_other_than_nist_is_not_sent);
    RUN_TEST(test_a_sign_with_a_key_other_than_p256_is_not_sent);
    RUN_TEST(test_a_sign_into_a_buffer_too_small_is_not_sent);
    RUN_TEST(test_a_sign_with_another_scheme_is_not_sent);
    RUN_TEST(test_a_sign_answered_short_is_refused_unread);
    RUN_TEST(test_a_sign_answered_as_a_key_generate_is_refused);
    RUN_TEST(test_a_sign_never_answered_times_out);
    RUN_TEST(test_the_handles_flow_session_to_a_signature_and_back);
    RUN_TEST(test_a_good_export_returns_x_and_y_in_the_callers_buffer);
    RUN_TEST(test_a_refused_export_is_a_status_error);
    RUN_TEST(test_an_export_on_a_closed_store_is_a_key_store_error);
    RUN_TEST(test_an_export_with_no_store_is_not_sent);
    RUN_TEST(test_an_export_of_a_key_type_other_than_nist_is_not_sent);
    RUN_TEST(test_an_export_of_a_key_other_than_p256_is_not_sent);
    RUN_TEST(test_an_export_into_a_buffer_too_small_is_not_sent);
    RUN_TEST(test_an_export_answered_short_is_refused_unread);
    RUN_TEST(test_an_export_answered_as_a_key_generate_is_refused);
    RUN_TEST(test_an_export_never_answered_times_out);
    RUN_TEST(test_a_good_verification_open_returns_the_handle);
    RUN_TEST(test_a_verification_open_on_a_closed_session_is_a_session_error);
    RUN_TEST(test_a_verification_open_with_no_session_is_not_sent);
    RUN_TEST(test_a_good_verification_close_sends_the_handle);
    RUN_TEST(test_a_verification_close_on_a_closed_handle_is_a_handle_error);
    RUN_TEST(test_a_good_verify_passes);
    RUN_TEST(test_a_verify_that_fails_is_a_bad_signature_not_a_transport_error);
    RUN_TEST(test_a_verify_status_neither_passed_nor_failed_is_a_bad_signature);
    RUN_TEST(test_a_refused_verify_is_a_status_error);
    RUN_TEST(test_a_verify_on_a_closed_service_is_a_verification_service_error);
    RUN_TEST(test_a_verify_with_no_service_is_not_sent);
    RUN_TEST(test_a_verify_with_another_scheme_is_not_sent);
    RUN_TEST(test_a_verify_with_a_key_other_than_p256_is_not_sent);
    RUN_TEST(test_a_verify_with_a_short_key_is_not_sent);
    RUN_TEST(test_a_verify_with_a_short_signature_is_not_sent);
    RUN_TEST(test_a_verify_answered_short_is_refused_unread);
    RUN_TEST(test_a_verify_answered_as_a_sign_is_refused);
    RUN_TEST(test_a_verify_never_answered_times_out);
    RUN_TEST(test_a_generated_key_signs_exports_and_verifies);
    return UNITY_END();
}
