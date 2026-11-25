/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The console's pad request (src/hal/cortex-m/imx95/console_pins.c) over the
 * SCMI channel (scmi_mu.c), against the register model's MU and shared
 * memory: two PINCTRL_SETTINGS_CONFIGURE, TX then RX, CRC-32 both ways as the
 * board's channel checks it.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "console_pins.h"
#include "scmi.h"
#include "tfw/config.h"

#define MU  0x44610000u
#define SHM 0x44611000u

static struct tfw_imx95_model *m;
static struct tfw_scmi_chan chan;

static uint32_t rings;
static uint32_t seen[2][12];     /* each request's words 3 to 14 */
static uint32_t reply_status[2]; /* the SCMI status each request gets */
static uint32_t reply_crc_xor;   /* spoils the reply's CRC */
static uint32_t req_flip;        /* spoils a request bit after its CRC is written */
static uint32_t crc_rejects;     /* requests the platform refused for their CRC */
static uint32_t sent_crc[2];     /* the CRC word of each reply */

/* The System Manager's own CRC-32, transcribed from imx-sm rel_imx_sm_2026q3
 * components/crc/sm_crc.c CRC_Crc32: crc = 0, a nibble table with the
 * complement built in, no final XOR in code. sm/rpc/smt/rpc_smt.c RPC_SMT_Tx
 * writes impCrc (shm+0x0C) as CRC_Crc32(&buf->header, length) and RPC_SMT_Rx
 * refuses a message whose impCrc is not CRC_Crc32 of the `length` bytes from
 * the header: the covered bytes are the header word and the payload, as they
 * lie little-endian in the area. The SM's M7 channel (configs/mx95evk.cfg
 * SCMI_AGENT0, db=0) is check=crc32. Deliberately independent of the
 * production tfw_scmi_crc32 (scmi_mu.c), which only the cross-check test calls. */
static uint32_t sm_crc32(const uint8_t *addr, uint32_t size)
{
    static const uint32_t t[16] = {
        0x4DBDF21Cu, 0x500AE278u, 0x76D3D2D4u, 0x6B64C2B0u, 0x3B61B38Cu, 0x26D6A3E8u,
        0x000F9344u, 0x1DB88320u, 0xA005713Cu, 0xBDB26158u, 0x9B6B51F4u, 0x86DC4190u,
        0xD6D930ACu, 0xCB6E20C8u, 0xEDB71064u, 0xF0000000u,
    };
    uint32_t crc = 0u;
    uint32_t i;

    for (i = 0u; i < size; i++) {
        crc = (crc >> 4) ^ t[(crc ^ ((uint32_t)addr[i] >> 0)) & 0xFu];
        crc = (crc >> 4) ^ t[(crc ^ ((uint32_t)addr[i] >> 4)) & 0xFu];
    }
    return crc;
}

/* The first n bytes of words w, little-endian, as they lie in the area. */
static uint32_t words_crc_n(const uint32_t *w, uint32_t n)
{
    uint8_t  b[64];
    uint32_t i;

    if (n > sizeof b) {
        return 0u;
    }
    for (i = 0u; i < n; i++) {
        b[i] = (uint8_t)(w[i / 4u] >> (8u * (i % 4u)));
    }
    return sm_crc32(b, n);
}

static uint32_t words_crc(const uint32_t *w, uint32_t n)
{
    return words_crc_n(w, 4u * n);
}

/* imx-sm's side of the channel: RPC_SMT_Rx's check of the request's CRC over
 * the `length` bytes the agent wrote (a mismatch is refused, here with
 * COMMS_ERROR, the stand-in for the SM's CRC-error reply), then RPC_SMT_Tx's
 * reply with its own CRC. */
static void platform(struct tfw_imx95_model *mm)
{
    uint32_t i, k = rings < 2u ? rings : 1u;
    uint32_t status;

    mm->scmi.shm[7] ^= req_flip;
    for (i = 0u; i < 12u; i++) {
        seen[k][i] = mm->scmi.shm[3u + i];
    }
    rings++;
    status = reply_status[k];
    if (chan.crc != 0u && mm->scmi.shm[3] != words_crc_n(&mm->scmi.shm[6], mm->scmi.shm[5])) {
        crc_rejects++;
        status = (uint32_t)TFW_SCMI_COMMS_ERROR;
    }
    mm->scmi.shm[5] = 8u;                         /* header + status */
    mm->scmi.shm[7] = status;
    mm->scmi.shm[3] = words_crc(&mm->scmi.shm[6], 2u) ^ reply_crc_xor;
    sent_crc[k]     = mm->scmi.shm[3];
    mm->scmi.shm[1] = 0x1u;                       /* free */
}

/* A request for the builder's own tests: two pins and a mux. The board's
 * request (board_profile.h, through tfw/config.h) has its own test,
 * test_the_board_routes_gpio_io36_37_at_alt2, through tfw_imx95_console_route. */
static const struct tfw_imx95_console_pins pins = { 18u, 19u, 1u, 0u };

void setUp(void)
{
    tfw_imx95_model_reset();
    m = tfw_imx95_model_get();
    m->scmi.mu_base   = MU;
    m->scmi.shm_base  = SHM;
    m->scmi.shm_words = 32u;
    m->scmi.shm[1]    = 0x1u;
    m->scmi.platform  = platform;
    memset(&chan, 0, sizeof chan);
    chan.mu      = MU;
    chan.shm     = SHM;
    chan.shm_len = 128u;
    chan.crc     = 1u;
    rings = 0u;
    memset(seen, 0, sizeof seen);
    reply_status[0] = reply_status[1] = 0u;
    reply_crc_xor = 0u;
    req_flip      = 0u;
    crc_rejects   = 0u;
    sent_crc[0] = sent_crc[1] = 0u;
}

void tearDown(void) {}

/* TX's pin, then RX's: no function, one config, the mux; each with its CRC. */
static void test_tx_then_rx_are_muxed(void)
{
    uint32_t k;

    TEST_ASSERT_EQUAL_INT32(0, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(2u, rings);
    for (k = 0u; k < 2u; k++) {
        TEST_ASSERT_EQUAL_HEX32(4u + 20u, seen[k][2]);            /* length */
        TEST_ASSERT_EQUAL_HEX32((k << 18) | (0x19u << 10) | 0x6u, seen[k][3]);
        TEST_ASSERT_EQUAL_HEX32(18u + k, seen[k][4]);             /* pin id */
        TEST_ASSERT_EQUAL_HEX32(0u, seen[k][5]);                  /* no function */
        TEST_ASSERT_EQUAL_HEX32(1u << 2, seen[k][6]);             /* a pin, 1 config */
        TEST_ASSERT_EQUAL_HEX32(192u, seen[k][7]);                /* MUX */
        TEST_ASSERT_EQUAL_HEX32(1u, seen[k][8]);                  /* the mux */
        TEST_ASSERT_EQUAL_HEX32(words_crc(&seen[k][3], 6u), seen[k][0]);
    }
    /* imx-sm's known answers (header word then payload, little-endian), see
     * test_tfw_scmi_crc32_matches_imx_sm_vectors. */
    TEST_ASSERT_EQUAL_HEX32(0xFF9BEACFu, seen[0][0]);
    TEST_ASSERT_EQUAL_HEX32(0xC65D4289u, seen[1][0]);
    TEST_ASSERT_EQUAL_HEX32(0xDCB4BE18u, sent_crc[0]);
    TEST_ASSERT_EQUAL_HEX32(0x4725FC0Eu, sent_crc[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, crc_rejects);
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

/* A pad configuration, when there is one, is the second config. */
static void test_a_pad_configuration_is_sent_after_the_mux(void)
{
    static const struct tfw_imx95_console_pins cfg = { 18u, 19u, 1u, 0x31Eu };

    TEST_ASSERT_EQUAL_INT32(0, tfw_imx95_console_pins_set(&chan, &cfg));
    TEST_ASSERT_EQUAL_HEX32(4u + 28u, seen[0][2]);
    TEST_ASSERT_EQUAL_HEX32(2u << 2, seen[0][6]);
    TEST_ASSERT_EQUAL_HEX32(193u, seen[0][9]);                    /* CONFIG */
}

/* TX refused: its status comes back and RX is not asked. */
static void test_a_refused_tx_stops_before_rx(void)
{
    reply_status[0] = 0xFFFFFFFDu;                                /* DENIED */
    TEST_ASSERT_EQUAL_INT32(-3, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(1u, rings);
    TEST_ASSERT_EQUAL_HEX32(0xA8065670u, sent_crc[0]);            /* imx-sm's */
}

static void test_a_reply_with_a_wrong_crc_is_refused(void)
{
    reply_crc_xor = 1u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_CRC, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(1u, rings);
}

/* TX granted, RX refused: both are asked and RX's status comes back. */
static void test_a_refused_rx_comes_back_after_tx(void)
{
    reply_status[1] = 0xFFFFFFFDu;                                /* DENIED */
    TEST_ASSERT_EQUAL_INT32(-3, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(2u, rings);
    TEST_ASSERT_EQUAL_HEX32(0xDCB4BE18u, sent_crc[0]);
    TEST_ASSERT_EQUAL_HEX32(0x33971466u, sent_crc[1]);            /* imx-sm's */
}

/* A request bit spoiled after the agent wrote its CRC: the SM refuses it
 * (RPC_SMT_Rx), the refusal comes back and RX is not asked. */
static void test_a_request_with_a_wrong_crc_is_refused_by_the_platform(void)
{
    req_flip = 1u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_COMMS_ERROR, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(1u, rings);
    TEST_ASSERT_EQUAL_UINT32(1u, crc_rejects);
}

/* A channel without CRC (every older initializer): no CRC word at shm+0x0C
 * on the request, and a reply whose CRC is wrong is not refused. Only the
 * first request is checked: the platform writes shm+0x0C on each reply. */
static void test_a_channel_without_crc_neither_writes_nor_checks_one(void)
{
    chan.crc      = 0u;
    reply_crc_xor = 1u;
    TEST_ASSERT_EQUAL_INT32(0, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(2u, rings);
    TEST_ASSERT_EQUAL_HEX32(0u, seen[0][0]);
}

/* No channel: nothing rung. */
static void test_no_channel_sends_nothing(void)
{
    chan.mu = 0u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_CHANNEL, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(0u, rings);
}

/* The standard check value: the transcribed imx-sm CRC_Crc32 of "123456789"
 * is 0xCBF43926, IEEE CRC-32's (zlib.crc32). */
static void test_the_crc_is_ieee_crc32(void)
{
    static const uint8_t s[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, sm_crc32(s, 9u));
}

/* imx-sm known answers: each message's bytes (header word then payload,
 * little-endian) and its CRC, computed with the transcribed CRC_Crc32 and,
 * independently, with zlib, e.g. the first:
 *   python3 -c 'import zlib,struct; print(hex(zlib.crc32(struct.pack("<6I",0x6406,18,0,4,192,1))))'
 * The production tfw_scmi_crc32 gives each, whole and chained as msg_crc
 * computes it (the 4 header bytes, then the payload). */
static void test_tfw_scmi_crc32_matches_imx_sm_vectors(void)
{
    static const struct {
        uint8_t  b[24];
        uint32_t n;
        uint32_t crc;
    } v[] = {
        /* request token 0, pin 18, mux 1 */
        { { 0x06, 0x64, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 },
          24u, 0xFF9BEACFu },
        /* request token 1, pin 19, mux 1 */
        { { 0x06, 0x64, 0x04, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 },
          24u, 0xC65D4289u },
        { { 0x06, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 8u, 0xDCB4BE18u }, /* reply 0, OK */
        { { 0x06, 0x64, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, 8u, 0x4725FC0Eu }, /* reply 1, OK */
        { { 0x06, 0x64, 0x00, 0x00, 0xFD, 0xFF, 0xFF, 0xFF }, 8u, 0xA8065670u }, /* reply 0, DENIED */
        { { 0x06, 0x64, 0x04, 0x00, 0xFD, 0xFF, 0xFF, 0xFF }, 8u, 0x33971466u }, /* reply 1, DENIED */
    };
    static const uint8_t s[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    uint32_t i;

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, tfw_scmi_crc32(0u, s, 9u));
    for (i = 0u; i < sizeof v / sizeof v[0]; i++) {
        TEST_ASSERT_EQUAL_HEX32(v[i].crc, sm_crc32(v[i].b, v[i].n));
        TEST_ASSERT_EQUAL_HEX32(v[i].crc, tfw_scmi_crc32(0u, v[i].b, v[i].n));
        TEST_ASSERT_EQUAL_HEX32(v[i].crc,
                                tfw_scmi_crc32(tfw_scmi_crc32(0u, v[i].b, 4u), &v[i].b[4], v[i].n - 4u));
    }
}

/* 12 bytes and 8 per config: the maximum fits the payload, one more would
 * not and is refused before a byte is written, the message left as it was. */
static void test_the_configs_maximum_fits_the_payload_and_one_more_is_refused(void)
{
    struct tfw_scmi_msg msg;
    uint32_t types[TFW_SCMI_PINCTRL_CONFIGS_MAX + 1u];
    uint32_t values[TFW_SCMI_PINCTRL_CONFIGS_MAX + 1u];
    uint32_t i;

    TEST_ASSERT_EQUAL_UINT32(2u, TFW_SCMI_PINCTRL_CONFIGS_MAX);
    for (i = 0u; i <= TFW_SCMI_PINCTRL_CONFIGS_MAX; i++) {
        types[i]  = 192u + i;
        values[i] = 0xA0u + i;
    }
    TEST_ASSERT_EQUAL_INT32(0, tfw_scmi_pinctrl_settings_configure(
                                   &msg, 1u, 18u, types, values, TFW_SCMI_PINCTRL_CONFIGS_MAX));
    TEST_ASSERT_EQUAL_UINT32(12u + 8u * TFW_SCMI_PINCTRL_CONFIGS_MAX, msg.len);
    TEST_ASSERT_TRUE(msg.len + 8u > TFW_SCMI_PAYLOAD_MAX);             /* one more overflows */
    TEST_ASSERT_EQUAL_HEX8(0xA1u, msg.payload[msg.len - 4u]);          /* last value */

    memset(&msg, 0x5A, sizeof msg);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TOO_LONG,
                            tfw_scmi_pinctrl_settings_configure(&msg, 1u, 18u, types, values,
                                                                TFW_SCMI_PINCTRL_CONFIGS_MAX + 1u));
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, msg.len);
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, msg.header);
}

/* The board's own request, as the console sends it (console_pins.c's channel
 * and pads from board_profile.h): LPUART7 on GPIO_IO36 and GPIO_IO37, the SM's
 * pin ids 40 then 41, one config each, the mux (type 192) at ALT2, and each
 * message's CRC-32 at shm+0x0C (owner ruling 1243, #474). */
static void test_the_board_routes_gpio_io36_37_at_alt2(void)
{
    uint32_t k;

    TEST_ASSERT_EQUAL_HEX32(MU, TFW_IMX95_SCMI_MU_BASE);
    TEST_ASSERT_EQUAL_HEX32(SHM, TFW_IMX95_SCMI_SHM_BASE);
    TEST_ASSERT_EQUAL_INT32(0, tfw_imx95_console_route());
    TEST_ASSERT_EQUAL_UINT32(2u, rings);
    for (k = 0u; k < 2u; k++) {
        TEST_ASSERT_EQUAL_HEX32(4u + 20u, seen[k][2]);            /* length */
        TEST_ASSERT_EQUAL_HEX32(0x19u, (seen[k][3] >> 10) & 0xFFu); /* PINCTRL */
        TEST_ASSERT_EQUAL_HEX32(0x6u, seen[k][3] & 0x3FFu);         /* message 6 */
        TEST_ASSERT_EQUAL_HEX32(40u + k, seen[k][4]);             /* GPIO_IO36, 37 */
        TEST_ASSERT_EQUAL_HEX32(0u, seen[k][5]);                  /* no function */
        TEST_ASSERT_EQUAL_HEX32(1u << 2, seen[k][6]);             /* a pin, 1 config */
        TEST_ASSERT_EQUAL_HEX32(192u, seen[k][7]);                /* MUX */
        TEST_ASSERT_EQUAL_HEX32(2u, seen[k][8]);                  /* ALT2, LPUART7 */
        TEST_ASSERT_EQUAL_HEX32(words_crc(&seen[k][3], 6u), seen[k][0]);  /* shm+0x0C */
    }
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tx_then_rx_are_muxed);
    RUN_TEST(test_a_pad_configuration_is_sent_after_the_mux);
    RUN_TEST(test_a_refused_tx_stops_before_rx);
    RUN_TEST(test_a_reply_with_a_wrong_crc_is_refused);
    RUN_TEST(test_a_refused_rx_comes_back_after_tx);
    RUN_TEST(test_a_channel_without_crc_neither_writes_nor_checks_one);
    RUN_TEST(test_no_channel_sends_nothing);
    RUN_TEST(test_a_request_with_a_wrong_crc_is_refused_by_the_platform);
    RUN_TEST(test_the_crc_is_ieee_crc32);
    RUN_TEST(test_tfw_scmi_crc32_matches_imx_sm_vectors);
    RUN_TEST(test_the_configs_maximum_fits_the_payload_and_one_more_is_refused);
    RUN_TEST(test_the_board_routes_gpio_io36_37_at_alt2);
    return UNITY_END();
}
