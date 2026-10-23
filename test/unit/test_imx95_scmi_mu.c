/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SCMI shared memory channel over an MU doorbell
 * (src/hal/cortex-m/imx95/scmi_mu.c), against the register model's MU and
 * shared memory and a System Manager written here. The area's layout is
 * written out from Arm DEN0056E, section 5.1.2, Table 25, as word indexes:
 * 1 channel_status (bit 0 free, bit 1 error), 4 channel_flags, 5 length,
 * 6 message header, 7 on the payload. The MU's GCR is +0x114 (Linux
 * imx-mailbox.c's; unconfirmed against IMX95RM). The wait's
 * bound is pinned as 100000 reads. No value is taken from the code under test.
 *
 * The bases are the test's: no document gives the M7 agent's MU or area
 * (docs/multi-arch/imx95-design.md question 21).
 */
#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "scmi_mu.h"

#define MU   0x60000000u
#define SHM  0x60100000u

static struct tfw_imx95_model *m;
static struct tfw_scmi_chan chan;
static struct tfw_scmi_msg cmd, rsp;

/* What the System Manager below answers. */
static uint32_t reply_header_xor;   /* flips bits of the command's header */
static uint32_t reply_length;       /* the length word; 0 = 4 + the words */
static uint32_t reply_words[8];
static uint32_t reply_n;
static uint32_t reply_status;       /* channel_status when done */
static uint32_t seen[8];            /* the request's words 4 to 11 */

static void platform(struct tfw_imx95_model *mm)
{
    uint32_t i;

    for (i = 0u; i < 8u; i++) {
        seen[i] = mm->scmi.shm[4u + i];
    }
    mm->scmi.shm[6] ^= reply_header_xor;
    mm->scmi.shm[5] = reply_length != 0u ? reply_length : 4u + 4u * reply_n;
    for (i = 0u; i < reply_n; i++) {
        mm->scmi.shm[7u + i] = reply_words[i];
    }
    mm->scmi.shm[1] = reply_status;
}

void setUp(void)
{
    tfw_imx95_model_reset();
    m = tfw_imx95_model_get();
    m->scmi.mu_base   = MU;
    m->scmi.shm_base  = SHM;
    m->scmi.shm_words = 32u;      /* 128 bytes */
    m->scmi.shm[1]    = 0x1u;     /* free */
    m->scmi.shm[4]    = 0x1u;     /* a completion interrupt the agent must clear */
    m->scmi.platform  = platform;
    memset(&chan, 0, sizeof chan);
    chan.mu       = MU;
    chan.shm      = SHM;
    chan.shm_len  = 128u;
    chan.doorbell = 2u;
    reply_header_xor = 0u;
    reply_length     = 0u;
    reply_n          = 1u;
    reply_words[0]   = 0u;        /* SUCCESS */
    reply_status     = 0x1u;      /* free, no error */
    memset(seen, 0, sizeof seen);
    memset(&rsp, 0, sizeof rsp);
}

void tearDown(void) {}

static void test_a_command_is_laid_out_and_rung(void)
{
    tfw_scmi_clock_config_set(&cmd, 5u, 0x00030000u, 42u, 1u);
    TEST_ASSERT_EQUAL_INT32(0, tfw_scmi_xfer(&chan, &cmd, &rsp));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[0]);   /* flags: polled */
    TEST_ASSERT_EQUAL_HEX32(0x00000010u, seen[1]);   /* length: header + 12 */
    TEST_ASSERT_EQUAL_HEX32(0x00145007u, seen[2]);   /* header */
    TEST_ASSERT_EQUAL_HEX32(0x0000002Au, seen[3]);   /* clock_id */
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, seen[4]);   /* attributes: enable */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, seen[5]);   /* extended_config_val */
    TEST_ASSERT_EQUAL_UINT32(1u, m->scmi.rings);
    TEST_ASSERT_EQUAL_HEX32(0x4u, m->scmi.rung);     /* GCR bit 2 */
    TEST_ASSERT_EQUAL_HEX32(0x0u, m->scmi.gcr);      /* taken */
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
}

static void test_the_agent_marks_the_channel_busy_before_ringing(void)
{
    m->scmi.platform = NULL;
    tfw_scmi_clock_rate_get(&cmd, 1u, 42u);
    (void)tfw_scmi_xfer(&chan, &cmd, &rsp);
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0u, m->scmi.shm[1]);
    TEST_ASSERT_EQUAL_HEX32(0x4u, m->scmi.gcr);
}

static void test_a_rate_comes_back(void)
{
    uint64_t hz = 0u;

    tfw_scmi_clock_rate_get(&cmd, 7u, 42u);
    reply_n        = 3u;
    reply_words[1] = 0x016E3600u;   /* 24 MHz */
    reply_words[2] = 0x00000000u;
    TEST_ASSERT_EQUAL_INT32(0, tfw_scmi_xfer(&chan, &cmd, &rsp));
    TEST_ASSERT_EQUAL_HEX32(0x001C5006u, rsp.header);
    TEST_ASSERT_EQUAL_UINT32(12u, rsp.len);
    TEST_ASSERT_EQUAL_INT32(0, tfw_scmi_clock_rate(&rsp, &hz));
    TEST_ASSERT_TRUE(hz == 24000000u);
}

static void test_a_status_reaches_the_caller(void)
{
    tfw_scmi_power_state_set(&cmd, 3u, 7u, TFW_SCMI_POWER_ON);
    reply_words[0] = 0xFFFFFFFDu;   /* DENIED, -3 */
    TEST_ASSERT_EQUAL_INT32(-3, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_wrong_token_is_refused(void)
{
    tfw_scmi_reset(&cmd, 3u, 1u, TFW_SCMI_RESET_AUTONOMOUS, TFW_SCMI_RESET_COLD);
    reply_header_xor = 0x00040000u;   /* token 3 becomes 2 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_wrong_protocol_is_refused(void)
{
    tfw_scmi_reset(&cmd, 3u, 1u, TFW_SCMI_RESET_AUTONOMOUS, TFW_SCMI_RESET_COLD);
    reply_header_xor = 0x00000400u;   /* protocol 0x16 becomes 0x17 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_channel_busy_before_the_send_times_out_unrung(void)
{
    m->scmi.shm[1] = 0x0u;   /* never freed */
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TIMEOUT, tfw_scmi_xfer(&chan, &cmd, &rsp));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(100000u, m->scmi.status_reads);
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
    TEST_ASSERT_EQUAL_HEX32(0x0u, m->scmi.shm[6]);   /* nothing written */
}

static void test_a_channel_left_busy_after_the_send_times_out(void)
{
    m->scmi.platform = NULL;   /* the System Manager never answers */
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TIMEOUT, tfw_scmi_xfer(&chan, &cmd, &rsp));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u + 100000u, m->scmi.status_reads);
    TEST_ASSERT_EQUAL_UINT32(1u, m->scmi.rings);
}

static void test_a_reply_longer_than_the_channel_is_refused(void)
{
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    reply_length = 4u + 100u + 4u;   /* the 128-byte area holds 100 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TOO_LONG, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_reply_longer_than_the_buffer_is_refused(void)
{
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    reply_length = 4u + 36u;         /* fits the area, not 32 bytes */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TOO_LONG, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_command_longer_than_the_channel_is_not_sent(void)
{
    chan.shm_len = 0x20u;            /* room for one payload word */
    tfw_scmi_power_state_set(&cmd, 3u, 7u, TFW_SCMI_POWER_ON);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_TOO_LONG, tfw_scmi_xfer(&chan, &cmd, &rsp));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->scmi.rings);
}

static void test_a_reply_without_a_header_is_short(void)
{
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    reply_length = 3u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_SHORT, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_a_channel_error_is_reported(void)
{
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    reply_status = 0x3u;             /* free, error */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_CHANNEL, tfw_scmi_xfer(&chan, &cmd, &rsp));
}

static void test_the_token_counts_and_wraps_at_ten_bits(void)
{
    chan.token = 1022u;
    TEST_ASSERT_EQUAL_UINT32(1022u, tfw_scmi_token(&chan));
    TEST_ASSERT_EQUAL_UINT32(1023u, tfw_scmi_token(&chan));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_scmi_token(&chan));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_command_is_laid_out_and_rung);
    RUN_TEST(test_the_agent_marks_the_channel_busy_before_ringing);
    RUN_TEST(test_a_rate_comes_back);
    RUN_TEST(test_a_status_reaches_the_caller);
    RUN_TEST(test_a_wrong_token_is_refused);
    RUN_TEST(test_a_wrong_protocol_is_refused);
    RUN_TEST(test_a_channel_busy_before_the_send_times_out_unrung);
    RUN_TEST(test_a_channel_left_busy_after_the_send_times_out);
    RUN_TEST(test_a_reply_longer_than_the_channel_is_refused);
    RUN_TEST(test_a_reply_longer_than_the_buffer_is_refused);
    RUN_TEST(test_a_command_longer_than_the_channel_is_not_sent);
    RUN_TEST(test_a_reply_without_a_header_is_short);
    RUN_TEST(test_a_channel_error_is_reported);
    RUN_TEST(test_the_token_counts_and_wraps_at_ten_bits);
    return UNITY_END();
}
