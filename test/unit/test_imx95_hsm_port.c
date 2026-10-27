/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's A1 and A2 (src/hal/cortex-m/imx95/hsm_port.c;
 * docs/hsm-self-defence/m7-design.md section 7, slice 5) against the register
 * model's MU and shared memory: the SCMI bytes each action leaves there, the
 * statuses it reports, and the board's own port, whose ids are all zero until
 * docs/multi-arch/imx95-design.md question 21, answering unsupported with
 * nothing sent. The ids below are test ids, not the board's.
 *
 * Slice 6 adds A4 and S3 against the model's enclave MU: each key deleted
 * strictly and counted done only when acknowledged (refused, no answer), and
 * GET_INFO's Q17 fields compared, match and mismatch, with each expected value.
 */

#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "core/hsm.h"
#include "core/hsm_port.h"
#include "hsm_act.h"

#define MU   0x60000000u
#define SHM  0x60100000u

#define MSGS 4u

static struct tfw_imx95_model  *m;
static struct tfw_scmi_chan     chan;
static struct tfw_imx95_hsm_ids ids;

/* What the platform answers, one status word per message, and what it saw:
 * channel_flags, length, header and three payload words of each message. */
static uint32_t reply_words[MSGS];
static uint32_t reply_status;
static uint32_t seen[MSGS][6];
static uint32_t n_seen;

static void platform(struct tfw_imx95_model *mm)
{
    uint32_t i;

    if (n_seen < MSGS) {
        for (i = 0u; i < 6u; i++) {
            seen[n_seen][i] = mm->scmi.shm[4u + i];
        }
        mm->scmi.shm[7] = reply_words[n_seen];
        n_seen++;
    }
    mm->scmi.shm[5] = 8u;   /* header and status */
    mm->scmi.shm[1] = reply_status;
}

/* The enclave's MU, apart from the System Manager's, and what it answers: a
 * success with a fresh handle to each request, a failure to request number
 * `ele_fail_at` (from 1), nothing at all to `ele_silent_at`. GET_INFO's
 * buffer is written as DMA would. The delete's id and layout are test values:
 * no source names the enclave's (board_profile.h). */
#define ELE      0x60200000u
#define ELE_MSGS 12u

static const struct tfw_ele_del_layout del5 = { 0x99u, 5u, 2u, 1u, 2u, 3u, 0u };

static struct tfw_ele_chan   ele;
static struct tfw_imx95_wipe wipe;
static uint32_t ele_cmd[ELE_MSGS];
static uint32_t ele_req[ELE_MSGS][5];
static uint32_t ele_n;
static uint32_t ele_fail_at;
static uint32_t ele_silent_at;
static uint8_t  info[TFW_ELE_GET_INFO_LEN];
static uint8_t  info_image[TFW_ELE_GET_INFO_LEN];

static uint32_t ele_rsp_words(uint32_t cmd)
{
    switch (cmd) {
    case TFW_ELE_SESSION_OPEN:   return TFW_ELE_SESSION_OPEN_RSP_WORDS;
    case TFW_ELE_KEY_STORE_OPEN: return TFW_ELE_KEY_STORE_OPEN_RSP_WORDS;
    case TFW_ELE_KEY_MGMT_OPEN:  return TFW_ELE_KEY_MGMT_OPEN_RSP_WORDS;
    default:                     return 2u;   /* closes, GET_INFO, del5 */
    }
}

static void enclave(struct tfw_imx95_model *mm)
{
    uint32_t cmd = (mm->ele.req[0] >> 16) & 0xFFu;
    uint32_t n   = ele_rsp_words(cmd);
    uint32_t i;

    if (ele_n < ELE_MSGS) {
        ele_cmd[ele_n] = cmd;
        for (i = 0u; i < 5u && i < mm->ele.req_len; i++) {
            ele_req[ele_n][i] = mm->ele.req[i];
        }
    }
    ele_n++;
    mm->ele.req_len = 0u;
    mm->ele.rsp_len = 0u;
    mm->ele.rsp_pos = 0u;
    if (ele_n == ele_silent_at) {
        return;
    }
    mm->ele.rsp[0] = 0xE1000006u | (cmd << 16) | (n << 8);
    mm->ele.rsp[1] = (ele_n == ele_fail_at) ? 0x29u : 0xD6u;
    mm->ele.rsp[2] = 0x100u + ele_n;   /* the handle an open returns */
    mm->ele.rsp_len = n;
    if (cmd == TFW_ELE_GET_INFO && ele_n != ele_fail_at) {
        memcpy(info, info_image, sizeof info);
    }
}

void setUp(void)
{
    uint32_t k;

    tfw_imx95_model_reset();
    m = tfw_imx95_model_get();
    m->scmi.mu_base   = MU;
    m->scmi.shm_base  = SHM;
    m->scmi.shm_words = 32u;      /* 128 bytes */
    m->scmi.shm[1]    = 0x1u;     /* free */
    m->scmi.platform  = platform;
    memset(&chan, 0, sizeof chan);
    chan.mu       = MU;
    chan.shm      = SHM;
    chan.shm_len  = 128u;
    chan.doorbell = 1u;
    memset(&ids, 0, sizeof ids);
    memset(reply_words, 0, sizeof reply_words);   /* SUCCESS */
    reply_status = 0x1u;                          /* free, no error */
    memset(seen, 0, sizeof seen);
    n_seen = 0u;
    m->ele.mu_base = ELE;
    m->ele.par     = 0x00000408u;   /* 8 transmit registers, 4 receive */
    m->ele.enclave = enclave;
    ele.mu = ELE;
    memset(&wipe, 0, sizeof wipe);
    wipe.mu_irq      = 1u;
    wipe.store_id    = 7u;
    wipe.store_nonce = 0x1234u;
    wipe.keys[0]     = 0x51u;
    wipe.keys[1]     = 0x52u;
    wipe.del         = del5;
    memset(ele_cmd, 0, sizeof ele_cmd);
    memset(ele_req, 0, sizeof ele_req);
    ele_n = 0u;
    ele_fail_at = 0u;
    ele_silent_at = 0u;
    memset(info, 0xFF, sizeof info);
    for (k = 0u; k < TFW_ELE_GET_INFO_LEN; k++) {
        info_image[k] = (uint8_t)(k ^ 0xA5u);
    }
}

void tearDown(void) {}

/* One POWER_STATE_SET, synchronous, switching `domain` off, as message `i`. */
static void assert_power_off(uint32_t i, uint32_t token, uint32_t domain)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[i][0]);                  /* flags: polled */
    TEST_ASSERT_EQUAL_HEX32(0x00000010u, seen[i][1]);                  /* header + 12 */
    TEST_ASSERT_EQUAL_HEX32(0x00004404u | (token << 18), seen[i][2]);  /* power 0x11, 0x4 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[i][3]);                  /* synchronous */
    TEST_ASSERT_EQUAL_HEX32(domain, seen[i][4]);
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, seen[i][5]);                  /* off */
}

static void test_a2_switches_the_a55_domain_off(void)
{
    ids.a55_power = 9u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
    assert_power_off(0u, 0u, 9u);
    TEST_ASSERT_EQUAL_UINT32(1u, m->scmi.rings);
    TEST_ASSERT_EQUAL_HEX32(0x2u, m->scmi.rung);   /* GCR bit 1 */
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a1_holds_the_phys_reset(void)
{
    ids.phy_reset = 3u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[0][0]);   /* flags: polled */
    TEST_ASSERT_EQUAL_HEX32(0x00000010u, seen[0][1]);   /* header + 12 */
    TEST_ASSERT_EQUAL_HEX32(0x00005804u, seen[0][2]);   /* reset 0x16, RESET 0x4, token 0 */
    TEST_ASSERT_EQUAL_HEX32(0x00000003u, seen[0][3]);   /* domain_id */
    TEST_ASSERT_EQUAL_HEX32(0x00000002u, seen[0][4]);   /* explicit assert, synchronous */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[0][5]);   /* cold reset */
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a1_switches_the_phys_domain_off(void)
{
    ids.phy_power = 5u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
    assert_power_off(0u, 0u, 5u);
}

static void test_a1_resets_then_powers_off_on_the_next_token(void)
{
    ids.phy_reset = 3u;
    ids.phy_power = 5u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    TEST_ASSERT_EQUAL_UINT32(2u, n_seen);
    TEST_ASSERT_EQUAL_HEX32(0x00005804u, seen[0][2]);
    assert_power_off(1u, 1u, 5u);
    TEST_ASSERT_EQUAL_UINT32(2u, m->scmi.rings);
}

static void test_a1_tries_the_power_domain_when_the_reset_fails(void)
{
    ids.phy_reset  = 3u;
    ids.phy_power  = 5u;
    reply_words[0] = 0xFFFFFFFDu;   /* DENIED */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    TEST_ASSERT_EQUAL_UINT32(2u, n_seen);
    assert_power_off(1u, 1u, 5u);
}

static void test_a1_reports_a_power_domain_failure(void)
{
    ids.phy_reset  = 3u;
    ids.phy_power  = 5u;
    reply_words[1] = 0xFFFFFFFEu;   /* INVALID_PARAMETERS */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    TEST_ASSERT_EQUAL_UINT32(2u, n_seen);
}

static void test_each_status_maps_as_scmi_c_maps_it(void)
{
    static const uint32_t words[6]    = { 0x00000000u, 0xFFFFFFFEu, 0xFFFFFFFFu,
                                          0xFFFFFFFDu, 0xFFFFFFFCu, 0xFFFFFFF4u };
    static const tfw_status_t want[6] = { TFW_OK, TFW_ERR_RANGE, TFW_ERR_UNSUPPORTED,
                                          TFW_ERR_PROTECTED_REGION, TFW_ERR_INTERNAL,
                                          TFW_ERR_INTERNAL };
    uint32_t i;

    ids.a55_power = 9u;
    for (i = 0u; i < 6u; i++) {
        n_seen         = 0u;
        reply_words[0] = words[i];
        TEST_ASSERT_EQUAL_INT(want[i], tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
        TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
    }
}

static void test_a_channel_left_busy_is_a_timeout(void)
{
    ids.a55_power = 9u;
    reply_status  = 0x0u;   /* never freed */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
}

static void test_a_channel_error_is_unrecognised(void)
{
    ids.a55_power = 9u;
    reply_status  = 0x3u;   /* free, channel error */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
}

static void test_an_id_left_zero_is_unsupported_and_sends_nothing(void)
{
    ids.a55_power = 9u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    ids.a55_power = 0u;
    ids.phy_reset = 3u;
    ids.phy_power = 5u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
    TEST_ASSERT_EQUAL_UINT32(0u, n_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
}

static void test_a_channel_left_zero_is_unsupported_and_sends_nothing(void)
{
    ids.phy_reset = 3u;
    ids.a55_power = 9u;
    chan.mu = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_PHY_OFF));
    chan.mu  = MU;
    chan.shm = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_POWER_OFF));
    TEST_ASSERT_EQUAL_UINT32(0u, n_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
}

static void test_every_other_action_is_unsupported_and_sends_nothing(void)
{
    static const uint8_t others[5] = { 0u, TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_KEYS_WIPE,
                                       TFW_HSM_ACT_RECORD, 0xFFu };
    uint32_t i;

    ids.phy_reset = 3u;
    ids.phy_power = 5u;
    ids.a55_power = 9u;
    ids.shutdown  = 1u;
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_act(&chan, &ids, others[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, n_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
}

/* A5's frame, as DEN0056E section 4.4.2.5 lays it out. */
static void assert_shutdown(uint32_t i, uint32_t token)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[i][0]);                  /* flags: polled */
    TEST_ASSERT_EQUAL_HEX32(0x0000000Cu, seen[i][1]);                  /* header + 8 */
    TEST_ASSERT_EQUAL_HEX32(0x00004803u | (token << 18), seen[i][2]);  /* system 0x12, 0x3 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[i][3]);                  /* forceful */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[i][4]);                  /* shutdown */
}

static void test_a5_asks_a_forceful_shutdown(void)
{
    ids.shutdown = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
    assert_shutdown(0u, 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, m->scmi.rings);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a5_each_refusal_is_not_done(void)
{
    static const uint32_t words[5]    = { 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFDu,
                                          0xFFFFFFFCu, 0xFFFFFFF4u };
    static const tfw_status_t want[5] = { TFW_ERR_RANGE, TFW_ERR_UNSUPPORTED,
                                          TFW_ERR_PROTECTED_REGION, TFW_ERR_INTERNAL,
                                          TFW_ERR_INTERNAL };
    uint32_t i;

    ids.shutdown = 1u;
    for (i = 0u; i < 5u; i++) {
        n_seen         = 0u;
        reply_words[0] = words[i];
        TEST_ASSERT_EQUAL_INT(want[i], tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
        TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
        assert_shutdown(0u, i);
    }
}

static void test_a5_no_answer_is_a_timeout(void)
{
    ids.shutdown = 1u;
    reply_status = 0x0u;   /* never freed */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
    TEST_ASSERT_EQUAL_UINT32(1u, n_seen);
}

static void test_a5_a_channel_error_is_unrecognised(void)
{
    ids.shutdown = 1u;
    reply_status = 0x3u;   /* free, channel error */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
}

static void test_a5_unpermitted_or_no_channel_is_unsupported_and_sends_nothing(void)
{
    ids.phy_reset = 3u;
    ids.phy_power = 5u;
    ids.a55_power = 9u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
    ids.shutdown = 1u;
    chan.mu      = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
    chan.mu  = MU;
    chan.shm = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_act(&chan, &ids, TFW_HSM_ACT_SHUTDOWN));
    TEST_ASSERT_EQUAL_UINT32(0u, n_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
}

/* The board's own port: its ids and channel are zero (imx95 Q21). */
static void test_the_boards_port_is_unsupported_and_sends_nothing(void)
{
    uint32_t a;

    for (a = 0u; a <= 7u; a++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hsm_port_act((uint8_t)a));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, n_seen);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

/* The board's S3: no enclave MU, so nothing is asked and nothing raised. */
static void test_the_boards_measure_is_unsupported_and_sends_nothing(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    set.n_expected = 1u;
    set.expected[0].key = 1u;
    set.expected[0].n   = 2u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hsm_port_measure(&set));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

/* ---- A4: the keys wiped by the enclave, strict ---- */

static const uint32_t wipe_seq[8] = {
    TFW_ELE_SESSION_OPEN, TFW_ELE_KEY_STORE_OPEN, TFW_ELE_KEY_MGMT_OPEN, 0x99u, 0x99u,
    TFW_ELE_KEY_MGMT_CLOSE, TFW_ELE_KEY_STORE_CLOSE, TFW_ELE_SESSION_CLOSE
};

/* Request `i` deletes `key` on management handle 0x103, strict, with its CRC. */
static void assert_strict_delete(uint32_t i, uint32_t key)
{
    TEST_ASSERT_EQUAL_HEX32(0x17990506u, ele_req[i][0]);   /* tag 0x17, 0x99, 5 words */
    TEST_ASSERT_EQUAL_HEX32(0x00000103u, ele_req[i][1]);   /* the third open's handle */
    TEST_ASSERT_EQUAL_HEX32(key, ele_req[i][2]);
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_DEL_KEY_STRICT, ele_req[i][3]);
    TEST_ASSERT_EQUAL_HEX32(ele_req[i][0] ^ ele_req[i][1] ^ ele_req[i][2] ^ ele_req[i][3],
                            ele_req[i][4]);
}

static void test_a4_acknowledged_deletes_every_key_strictly_and_closes(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_wipe(&ele, &wipe));
    TEST_ASSERT_EQUAL_UINT32(8u, ele_n);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(wipe_seq, ele_cmd, 8);
    assert_strict_delete(3u, 0x51u);
    assert_strict_delete(4u, 0x52u);
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.out_of_turn);
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_a4_a_refused_delete_is_not_done_and_the_rest_are_tried(void)
{
    uint32_t at;

    for (at = 4u; at <= 5u; at++) {
        setUp();
        ele_fail_at = at;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_imx95_hsm_wipe(&ele, &wipe));
        TEST_ASSERT_EQUAL_UINT32(8u, ele_n);
        TEST_ASSERT_EQUAL_HEX32_ARRAY(wipe_seq, ele_cmd, 8);
    }
}

static void test_a4_a_refused_open_deletes_nothing_and_closes_what_opened(void)
{
    static const uint32_t sent[3] = { 1u, 3u, 5u };
    uint32_t at;

    for (at = 1u; at <= 3u; at++) {
        setUp();
        ele_fail_at = at;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_imx95_hsm_wipe(&ele, &wipe));
        TEST_ASSERT_EQUAL_UINT32(sent[at - 1u], ele_n);
        TEST_ASSERT_NOT_EQUAL(0x99u, ele_cmd[3]);
    }
}

static void test_a4_no_answer_to_a_delete_is_a_timeout_not_done(void)
{
    ele_silent_at = 4u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_imx95_hsm_wipe(&ele, &wipe));
    TEST_ASSERT_EQUAL_UINT32(8u, ele_n);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(wipe_seq, ele_cmd, 8);
}

static void test_a4_no_enclave_at_all_is_a_timeout(void)
{
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_imx95_hsm_wipe(&ele, &wipe));
}

static void test_a4_no_mu_command_or_key_is_unsupported_and_sends_nothing(void)
{
    ele.mu = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_wipe(&ele, &wipe));
    ele.mu = ELE;
    wipe.del.command = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_wipe(&ele, &wipe));
    wipe.del.command = 0x99u;
    wipe.keys[0] = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_wipe(&ele, &wipe));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

static void test_a4_a_bad_layout_is_unsupported_and_deletes_nothing(void)
{
    wipe.del.w_key = 9u;   /* past the message */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_imx95_hsm_wipe(&ele, &wipe));
    TEST_ASSERT_EQUAL_UINT32(6u, ele_n);
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_KEY_MGMT_CLOSE, ele_cmd[3]);
}

/* ---- S3: GET_INFO against the set's expected values ---- */

/* The five Q17 fields under test keys 1 to 5. The ROM patch hash's, the OEM
 * SRK hash's and the revocation bits' places are test values: the board's
 * are placeholders. */
static const struct tfw_imx95_s3 s3 = { {
    { 1u, TFW_ELE_INFO_LMDA_OFF, TFW_ELE_INFO_LMDA_LEN },
    { 2u, 96u, 32u },
    { 3u, TFW_ELE_INFO_SHA_FW_OFF, TFW_ELE_INFO_SHA_FW_LEN },
    { 4u, 164u, 64u },
    { 5u, 160u, 4u },
} };

static void expect(tfw_hsm_set_t *set, uint32_t key, const uint8_t *v, uint32_t n)
{
    tfw_hsm_expected_t *e = &set->expected[set->n_expected++];

    e->key = key;
    e->n   = (uint8_t)n;
    memcpy(e->value, v, n);
}

static void test_s3_each_field_matches_and_each_mismatch_is_range(void)
{
    tfw_hsm_set_t set;
    uint32_t k;

    for (k = 0u; k < TFW_IMX95_S3_FIELDS; k++) {
        const struct tfw_imx95_s3_field *f = &s3.f[k];

        memset(&set, 0, sizeof set);
        expect(&set, f->key, &info_image[f->off], f->len);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_compare(info_image, &set, &s3));
        set.expected[0].value[f->len - 1u] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &s3));
        set.expected[0].value[f->len - 1u] ^= 0x01u;
        set.expected[0].value[0] ^= 0x80u;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &s3));
    }
}

static void test_s3_all_fields_together_match_and_one_bad_fails_the_set(void)
{
    tfw_hsm_set_t set;
    uint32_t k;

    memset(&set, 0, sizeof set);
    for (k = 0u; k < TFW_IMX95_S3_FIELDS; k++) {
        expect(&set, s3.f[k].key, &info_image[s3.f[k].off], s3.f[k].len);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_compare(info_image, &set, &s3));
    set.expected[4].value[3] ^= 0x10u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &s3));
}

static void test_s3_no_expected_values_match(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_compare(info_image, &set, &s3));
}

static void test_s3_a_wrong_length_or_unknown_key_is_range(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    expect(&set, 3u, &info_image[TFW_ELE_INFO_SHA_FW_OFF], TFW_ELE_INFO_SHA_FW_LEN - 1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &s3));
    memset(&set, 0, sizeof set);
    expect(&set, 9u, info_image, 2u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &s3));
}

/* A field the board cannot read, its place zero or past the buffer, or a key
 * left zero, fails closed: a policy that expects it is a mismatch. */
static void test_s3_a_field_with_no_place_is_range(void)
{
    struct tfw_imx95_s3 bad = s3;
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    expect(&set, 5u, &info_image[160], 4u);
    bad.f[4].len = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &bad));
    bad.f[4].off = 254u;
    bad.f[4].len = 4u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &bad));
    bad = s3;
    bad.f[4].key = 0u;
    set.expected[0].key = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_compare(info_image, &set, &bad));
}

static void test_s3_measure_asks_get_info_and_compares(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    expect(&set, 3u, &info_image[TFW_ELE_INFO_SHA_FW_OFF], TFW_ELE_INFO_SHA_FW_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_imx95_hsm_measure(&ele, info, 0x2000u, &set, &s3));
    TEST_ASSERT_EQUAL_UINT32(1u, ele_n);
    TEST_ASSERT_EQUAL_HEX32(TFW_ELE_GET_INFO, ele_cmd[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(info_image, info, TFW_ELE_GET_INFO_LEN);
    set.expected[0].value[31] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_imx95_hsm_measure(&ele, info, 0x2000u, &set, &s3));
}

static void test_s3_a_refused_or_silent_get_info_is_not_a_match(void)
{
    static const uint8_t zero[TFW_ELE_GET_INFO_LEN];
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    expect(&set, 1u, &info_image[TFW_ELE_INFO_LMDA_OFF], TFW_ELE_INFO_LMDA_LEN);
    ele_fail_at = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_imx95_hsm_measure(&ele, info, 0x2000u, &set, &s3));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, info, TFW_ELE_GET_INFO_LEN);   /* nothing stale */
    m->ele.enclave = NULL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_imx95_hsm_measure(&ele, info, 0x2000u, &set, &s3));
}

static void test_s3_no_mu_is_unsupported_and_sends_nothing(void)
{
    tfw_hsm_set_t set;

    memset(&set, 0, sizeof set);
    ele.mu = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_imx95_hsm_measure(&ele, info, 0x2000u, &set, &s3));
    TEST_ASSERT_EQUAL_UINT32(0u, m->ele.requests);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a2_switches_the_a55_domain_off);
    RUN_TEST(test_a1_holds_the_phys_reset);
    RUN_TEST(test_a1_switches_the_phys_domain_off);
    RUN_TEST(test_a1_resets_then_powers_off_on_the_next_token);
    RUN_TEST(test_a1_tries_the_power_domain_when_the_reset_fails);
    RUN_TEST(test_a1_reports_a_power_domain_failure);
    RUN_TEST(test_each_status_maps_as_scmi_c_maps_it);
    RUN_TEST(test_a_channel_left_busy_is_a_timeout);
    RUN_TEST(test_a_channel_error_is_unrecognised);
    RUN_TEST(test_an_id_left_zero_is_unsupported_and_sends_nothing);
    RUN_TEST(test_a_channel_left_zero_is_unsupported_and_sends_nothing);
    RUN_TEST(test_every_other_action_is_unsupported_and_sends_nothing);
    RUN_TEST(test_a5_asks_a_forceful_shutdown);
    RUN_TEST(test_a5_each_refusal_is_not_done);
    RUN_TEST(test_a5_no_answer_is_a_timeout);
    RUN_TEST(test_a5_a_channel_error_is_unrecognised);
    RUN_TEST(test_a5_unpermitted_or_no_channel_is_unsupported_and_sends_nothing);
    RUN_TEST(test_the_boards_port_is_unsupported_and_sends_nothing);
    RUN_TEST(test_the_boards_measure_is_unsupported_and_sends_nothing);
    RUN_TEST(test_a4_acknowledged_deletes_every_key_strictly_and_closes);
    RUN_TEST(test_a4_a_refused_delete_is_not_done_and_the_rest_are_tried);
    RUN_TEST(test_a4_a_refused_open_deletes_nothing_and_closes_what_opened);
    RUN_TEST(test_a4_no_answer_to_a_delete_is_a_timeout_not_done);
    RUN_TEST(test_a4_no_enclave_at_all_is_a_timeout);
    RUN_TEST(test_a4_no_mu_command_or_key_is_unsupported_and_sends_nothing);
    RUN_TEST(test_a4_a_bad_layout_is_unsupported_and_deletes_nothing);
    RUN_TEST(test_s3_each_field_matches_and_each_mismatch_is_range);
    RUN_TEST(test_s3_all_fields_together_match_and_one_bad_fails_the_set);
    RUN_TEST(test_s3_no_expected_values_match);
    RUN_TEST(test_s3_a_wrong_length_or_unknown_key_is_range);
    RUN_TEST(test_s3_a_field_with_no_place_is_range);
    RUN_TEST(test_s3_measure_asks_get_info_and_compares);
    RUN_TEST(test_s3_a_refused_or_silent_get_info_is_not_a_match);
    RUN_TEST(test_s3_no_mu_is_unsupported_and_sends_nothing);
    return UNITY_END();
}
