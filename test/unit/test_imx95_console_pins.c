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

#define MU  0x44610000u
#define SHM 0x44611000u

static struct tfw_imx95_model *m;
static struct tfw_scmi_chan chan;

static uint32_t rings;
static uint32_t seen[2][12];     /* each request's words 3 to 14 */
static uint32_t reply_status[2]; /* the SCMI status each request gets */
static uint32_t reply_crc_xor;   /* spoils the reply's CRC */

static uint32_t words_crc(const uint32_t *w, uint32_t n)
{
    uint8_t  b[32];
    uint32_t i;

    for (i = 0u; i < n; i++) {
        b[4u * i]      = (uint8_t)w[i];
        b[4u * i + 1u] = (uint8_t)(w[i] >> 8);
        b[4u * i + 2u] = (uint8_t)(w[i] >> 16);
        b[4u * i + 3u] = (uint8_t)(w[i] >> 24);
    }
    return tfw_scmi_crc32(0u, b, 4u * n);
}

static void platform(struct tfw_imx95_model *mm)
{
    uint32_t i, k = rings < 2u ? rings : 1u;

    for (i = 0u; i < 12u; i++) {
        seen[k][i] = mm->scmi.shm[3u + i];
    }
    rings++;
    mm->scmi.shm[5] = 8u;                         /* header + status */
    mm->scmi.shm[7] = reply_status[k];
    mm->scmi.shm[3] = words_crc(&mm->scmi.shm[6], 2u) ^ reply_crc_xor;
    mm->scmi.shm[1] = 0x1u;                       /* free */
}

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
        TEST_ASSERT_EQUAL_HEX32(words_crc(&seen[k][3], 6u), seen[k][0]);
    }
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
}

static void test_a_reply_with_a_wrong_crc_is_refused(void)
{
    reply_crc_xor = 1u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_CRC, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(1u, rings);
}

/* No channel: nothing rung. */
static void test_no_channel_sends_nothing(void)
{
    chan.mu = 0u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_CHANNEL, tfw_imx95_console_pins_set(&chan, &pins));
    TEST_ASSERT_EQUAL_UINT32(0u, rings);
}

/* The standard check value: CRC-32 of "123456789" is 0xCBF43926. */
static void test_the_crc_is_ieee_crc32(void)
{
    static const uint8_t s[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, tfw_scmi_crc32(0u, s, 9u));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, tfw_scmi_crc32(tfw_scmi_crc32(0u, s, 4u), &s[4], 5u));
}

/* 12 bytes and 8 per config: the maximum fits the payload, one more would
 * not and is refused before a byte is written, the message left as it was. */
static void test_the_configs_maximum_fills_the_payload_and_one_more_is_refused(void)
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tx_then_rx_are_muxed);
    RUN_TEST(test_a_pad_configuration_is_sent_after_the_mux);
    RUN_TEST(test_a_refused_tx_stops_before_rx);
    RUN_TEST(test_a_reply_with_a_wrong_crc_is_refused);
    RUN_TEST(test_no_channel_sends_nothing);
    RUN_TEST(test_the_crc_is_ieee_crc32);
    RUN_TEST(test_the_configs_maximum_fills_the_payload_and_one_more_is_refused);
    return UNITY_END();
}
