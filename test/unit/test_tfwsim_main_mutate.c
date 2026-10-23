/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for tfwsim's shared loop, tools/tfwsim/main.c (#163).
 *
 * The unit is included, with its main() renamed, so its statics (run, pump,
 * preset_mock, hex_nibble) are reachable and the tier seam (tier.h) is the
 * stub below rather than a real tier: what a tier accepts is that tier's test,
 * what main.c does with the answer is this one's. Everything else -- the
 * transport, the commands, the mock HAL, the Ethernet socket -- is what
 * `make tfwsim` links.
 *
 * run() is driven in-process over pipes and a SOCK_SEQPACKET pair (one read
 * per packet, so time steps are countable); main()'s stdio and --pty paths in a
 * forked child, whose exit status is main()'s return. No fixed port, no sleep.
 */
/* run()'s poll is seen through wait_poll, so a test can tell whether the
 * socket joined the wait set; <poll.h> first, under main.c's own feature macro,
 * so only the call in main.c is renamed. */
#define _XOPEN_SOURCE 600
#include <poll.h>
static int wait_poll(struct pollfd *fds, nfds_t n, int timeout);
#define poll wait_poll
#define main tfwsim_main
#include "../../tools/tfwsim/main.c"
#undef main
#undef poll

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include "unity.h"
#include "frame.h"
#include "tfw/eth.h"

/* ---- the tier seam, stubbed ------------------------------------------- */

static int  tier_calls;
static int  tier_seeds;
static int  tier_argc[8];
static char tier_arg0[8][16];

/* Waits in which the controller's socket was in the set, beside the input. */
static int socket_waits;

static int wait_poll(struct pollfd *fds, nfds_t n, int timeout)
{
    if (n == 2u && fds[1].fd >= 0 && fds[1].fd == tfwsim_eth_fd() && fds[1].events == POLLIN) {
        socket_waits++;
    }
    return poll(fds, n, timeout);
}

int tfwsim_tier_option(int argc, char **argv)
{
    if (tier_calls < 8) {
        tier_argc[tier_calls] = argc;
        snprintf(tier_arg0[tier_calls], sizeof tier_arg0[0], "%s", argv[0]);
    }
    tier_calls++;
    if (strcmp(argv[0], "--two") == 0) { return 2; }
    if (strcmp(argv[0], "--bad") == 0) { return -1; }
    return 0;
}

const char *tfwsim_tier_usage(void) { return " [--two V]"; }

void tfwsim_tier_seed(void) { tier_seeds++; }

/* ---- helpers ----------------------------------------------------------- */

typedef struct {
    uint8_t op, seq, len;
    int     status;
    uint8_t pay[64];
} got_t;

static uint8_t outbuf[16384];
static size_t  outlen;
static got_t   got[128];
static size_t  ngot;

void setUp(void)
{
    tier_calls = 0;
    tier_seeds = 0;
    memset(tier_argc, 0, sizeof tier_argc);
    memset(tier_arg0, 0, sizeof tier_arg0);
    outlen = 0u;
    ngot   = 0u;
}

void tearDown(void) {}

static size_t frame(uint8_t *wire, size_t cap, uint8_t seq, uint8_t cmd,
                    const uint8_t *pay, uint8_t n)
{
    uint16_t len = tfw_frame_encode_request(wire, (uint16_t)cap, seq, cmd, pay, n);
    TEST_ASSERT_NOT_EQUAL(0u, len);
    return len;
}

static void decode(const uint8_t *b, size_t n)
{
    tfw_framer_t f;
    size_t       i;

    tfw_framer_init(&f);
    for (i = 0; i < n; i++) {
        if (tfw_framer_push(&f, b[i]) == TFW_FRAME_READY && ngot < sizeof got / sizeof got[0]) {
            got_t *g = &got[ngot++];
            g->op     = tfw_frame_op(&f);
            g->seq    = tfw_frame_seq(&f);
            g->status = (int)tfw_frame_status(&f);
            g->len    = tfw_frame_payload_len(&f);
            memcpy(g->pay, tfw_frame_payload(&f), g->len < 64u ? g->len : 64u);
        }
    }
}

static void slurp(int fd)
{
    ssize_t r;

    while ((r = read(fd, outbuf + outlen, sizeof outbuf - outlen)) > 0) {
        outlen += (size_t)r;
    }
}

/* run() over a pipe holding `in`, closed behind it. */
static int run_pipe(const uint8_t *in, size_t n, int eth)
{
    int in_p[2], out_p[2], rc;

    TEST_ASSERT_EQUAL_INT(0, pipe(in_p));
    TEST_ASSERT_EQUAL_INT(0, pipe(out_p));
    if (n) {
        TEST_ASSERT_EQUAL_INT((int)n, (int)write(in_p[1], in, n));
    }
    close(in_p[1]);
    rc = run(in_p[0], out_p[1], eth);
    close(in_p[0]);
    close(out_p[1]);
    slurp(out_p[0]);
    close(out_p[0]);
    decode(outbuf, outlen);
    return rc;
}

/* run() over a seqpacket pair: each of the k packets is one read(). */
static int run_packets(const uint8_t *const *pk, const size_t *len, size_t k, int eth)
{
    int sp[2], out_p[2], rc;
    size_t i;

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sp));
    TEST_ASSERT_EQUAL_INT(0, pipe(out_p));
    for (i = 0; i < k; i++) {
        TEST_ASSERT_EQUAL_INT((int)len[i], (int)write(sp[1], pk[i], len[i]));
    }
    shutdown(sp[1], SHUT_WR);
    rc = run(sp[0], out_p[1], eth);
    close(sp[0]);
    close(sp[1]);
    close(out_p[1]);
    slurp(out_p[0]);
    close(out_p[0]);
    decode(outbuf, outlen);
    return rc;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* main() in-process, for the paths that return before run(); stderr kept. */
static char errbuf[1024];

static int main_args(int argc, char **argv)
{
    int     p[2], saved, rc;
    ssize_t r;

    TEST_ASSERT_EQUAL_INT(0, pipe(p));
    fflush(stderr);
    saved = dup(2);
    dup2(p[1], 2);
    rc = tfwsim_main(argc, argv);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(p[1]);
    r = read(p[0], errbuf, sizeof errbuf - 1u);
    errbuf[r > 0 ? r : 0] = '\0';
    close(p[0]);
    return rc;
}

static int child_status(pid_t pid)
{
    int st = 0;

    TEST_ASSERT_EQUAL_INT((int)pid, (int)waitpid(pid, &st, 0));
    TEST_ASSERT_TRUE(WIFEXITED(st));
    return WEXITSTATUS(st);
}

/* ---- hex_nibble -------------------------------------------------------- */

static void test_hex_nibble_takes_each_digit_range_at_its_bounds(void)
{
    TEST_ASSERT_EQUAL_INT(0,  hex_nibble('0'));
    TEST_ASSERT_EQUAL_INT(9,  hex_nibble('9'));
    TEST_ASSERT_EQUAL_INT(10, hex_nibble('a'));
    TEST_ASSERT_EQUAL_INT(15, hex_nibble('f'));
    TEST_ASSERT_EQUAL_INT(10, hex_nibble('A'));
    TEST_ASSERT_EQUAL_INT(15, hex_nibble('F'));
}

static void test_hex_nibble_refuses_the_neighbours_of_each_range(void)
{
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('/'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble(':'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('`'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('g'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('@'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('G'));
    TEST_ASSERT_EQUAL_INT(-1, hex_nibble('\0'));
}

/* ---- preset_mock ------------------------------------------------------- */

static void clear_env(void)
{
    unsetenv("TFWSIM_PULSE_IN_US");
    unsetenv("TFWSIM_CAPTURE_US");
    unsetenv("TFWSIM_BUS_RESPONSE");
    unsetenv("TFWSIM_OW_PRESENCE");
}

static void test_preset_mock_with_nothing_set_leaves_the_reset_mock(void)
{
    uint32_t us = 0u;
    uint16_t cap[4];
    uint8_t  n = 0u, rd[1];

    clear_env();
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(NULL, 1u, 1000000u, &us));
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(NULL, 1u, cap, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 1u));
}

static void test_preset_mock_reads_the_pulse_width(void)
{
    uint32_t us = 0u;

    clear_env();
    setenv("TFWSIM_PULSE_IN_US", "1234", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(NULL, 1u, 1000000u, &us));
    TEST_ASSERT_EQUAL_UINT32(1234u, us);
    clear_env();
}

static void test_preset_mock_reads_a_capture_list_up_to_its_end(void)
{
    uint16_t cap[40];
    uint8_t  n = 0u;

    clear_env();
    setenv("TFWSIM_CAPTURE_US", "10,20,300", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(NULL, 1u, cap, 40u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    TEST_ASSERT_EQUAL_UINT16(10u, cap[0]);
    TEST_ASSERT_EQUAL_UINT16(20u, cap[1]);
    TEST_ASSERT_EQUAL_UINT16(300u, cap[2]);
    clear_env();
}

static void test_preset_mock_stops_a_capture_list_at_a_bad_separator(void)
{
    uint16_t cap[40];
    uint8_t  n = 0u;

    clear_env();
    setenv("TFWSIM_CAPTURE_US", "7 8,9", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(NULL, 1u, cap, 40u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(7u, cap[0]);
    clear_env();
}

static void test_preset_mock_caps_a_capture_list_at_32(void)
{
    char     list[200];
    size_t   o = 0u;
    uint16_t cap[40];
    uint8_t  n = 0u;
    int      i;

    for (i = 1; i <= 34; i++) {
        o += (size_t)snprintf(list + o, sizeof list - o, i == 1 ? "%d" : ",%d", i);
    }
    clear_env();
    setenv("TFWSIM_CAPTURE_US", list, 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(NULL, 1u, cap, 40u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(32u, n);
    TEST_ASSERT_EQUAL_UINT16(1u, cap[0]);
    TEST_ASSERT_EQUAL_UINT16(32u, cap[31]);
    clear_env();
}

static void test_preset_mock_decodes_a_bus_response_in_either_case(void)
{
    uint8_t rd[3] = { 0 };
    const uint8_t want[3] = { 0x0Fu, 0xA9u, 0xBCu };

    clear_env();
    setenv("TFWSIM_BUS_RESPONSE", "0fA9bC", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rd, 3u);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 4u));
    clear_env();
}

static void test_preset_mock_drops_a_lone_or_bad_trailing_nibble(void)
{
    uint8_t rd[2] = { 0 };

    clear_env();
    setenv("TFWSIM_BUS_RESPONSE", "12x4", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x12u, rd[0]);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 2u));

    setenv("TFWSIM_BUS_RESPONSE", "345", 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x34u, rd[0]);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 2u));
    clear_env();
}

static void test_preset_mock_caps_a_bus_response_at_32_bytes(void)
{
    char    hex[70];
    uint8_t rd[33];
    int     i;

    for (i = 0; i < 33; i++) {
        snprintf(hex + 2 * i, 3, "%02x", (unsigned)(i + 1));
    }
    clear_env();
    setenv("TFWSIM_BUS_RESPONSE", hex, 1);
    tfw_mock_reset();
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 32u));
    TEST_ASSERT_EQUAL_HEX8(0x01u, rd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, rd[31]);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_hal_i2c_xfer(0x50u, NULL, 0u, rd, 33u));
    clear_env();
}

static void test_preset_mock_sets_one_wire_presence_only_for_1(void)
{
    uint8_t present = 9u;

    clear_env();
    setenv("TFWSIM_OW_PRESENCE", "1", 1);
    tfw_mock_reset();
    (void)tfw_mock_ow_presence_set(0u);
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_reset(NULL, &present));
    TEST_ASSERT_EQUAL_UINT8(1u, present);

    setenv("TFWSIM_OW_PRESENCE", "10", 1);
    (void)tfw_mock_ow_presence_set(1u);
    preset_mock();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_reset(NULL, &present));
    TEST_ASSERT_EQUAL_UINT8(0u, present);
    clear_env();
}

/* ---- run() and pump() --------------------------------------------------- */

static void test_run_with_no_input_announces_boot_and_returns_0(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_pipe(NULL, 0u, 0));
    TEST_ASSERT_EQUAL_UINT(1u, ngot);
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, got[0].op);
    TEST_ASSERT_EQUAL_HEX8(TFW_EVENT_BOOT, got[0].pay[0]);
    TEST_ASSERT_EQUAL_INT(1, tier_seeds);
}

static void test_run_answers_a_ping_after_the_boot_event(void)
{
    const uint8_t msg[4] = { 'p', 'o', 'n', 'g' };
    uint8_t wire[64];
    size_t  n = frame(wire, sizeof wire, 0x21u, TFW_CMD_PING, msg, 4u);

    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 0));
    TEST_ASSERT_EQUAL_UINT(2u, ngot);
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, got[0].op);
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, got[1].op);
    TEST_ASSERT_EQUAL_HEX8(0x21u, got[1].seq);
    TEST_ASSERT_EQUAL_INT(TFW_OK, got[1].status);
    TEST_ASSERT_EQUAL_UINT8(4u, got[1].len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, got[1].pay, 4u);
    /* A RESET latched by an earlier run must not survive into this one. */
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_run_answers_every_frame_of_one_full_read(void)
{
    uint8_t in[512];
    size_t  n = 0u, one;
    uint8_t seq = 0u;

    for (;;) {
        uint8_t wire[32];
        one = frame(wire, sizeof wire, seq, TFW_CMD_PING, &seq, 1u);
        if (n + one > sizeof in) { break; }
        memcpy(in + n, wire, one);
        n += one;
        seq++;
    }
    TEST_ASSERT_EQUAL_INT(0, run_pipe(in, n, 0));
    TEST_ASSERT_EQUAL_UINT((size_t)seq + 1u, ngot);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(seq - 1u), got[seq].seq);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(seq - 1u), got[seq].pay[0]);
}

static void test_run_advances_time_one_ms_per_read_from_a_fresh_clock(void)
{
    uint8_t a[32], b[32];
    const uint8_t *pk[2] = { a, b };
    size_t  len[2];

    tfw_mock_set_millis(5000u);       /* a stale clock run() must reset */
    len[0] = frame(a, sizeof a, 1u, TFW_CMD_SYS_INFO, NULL, 0u);
    len[1] = frame(b, sizeof b, 2u, TFW_CMD_SYS_INFO, NULL, 0u);
    TEST_ASSERT_EQUAL_INT(0, run_packets(pk, len, 2u, 0));
    TEST_ASSERT_EQUAL_UINT(3u, ngot);
    TEST_ASSERT_EQUAL_UINT32(1u, le32(got[1].pay));
    TEST_ASSERT_EQUAL_UINT32(2u, le32(got[2].pay));
}

static void test_run_bridges_the_framer_errors_into_sys_info(void)
{
    uint8_t bad[32], info[32];
    const uint8_t *pk[2] = { bad, info };
    size_t  len[2];

    len[0] = frame(bad, sizeof bad, 1u, TFW_CMD_PING, (const uint8_t *)"x", 1u);
    bad[len[0] - 1u] ^= 0x5Au;        /* the CRC no longer matches */
    len[1] = frame(info, sizeof info, 2u, TFW_CMD_SYS_INFO, NULL, 0u);
    TEST_ASSERT_EQUAL_INT(0, run_packets(pk, len, 2u, 0));
    TEST_ASSERT_TRUE(ngot >= 2u);
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_SYS_INFO | TFW_OP_RESPONSE_BIT, got[ngot - 1u].op);
    /* millis u32, ram u16, cause u8, then err_crc u16 and err_format u16 */
    TEST_ASSERT_EQUAL_UINT8(1u, got[ngot - 1u].pay[7]);
    TEST_ASSERT_EQUAL_UINT8(0u, got[ngot - 1u].pay[8]);
}

static void test_run_carries_out_a_latched_reset(void)
{
    const uint8_t mode = 0u;
    uint8_t wire[32];
    size_t  n = frame(wire, sizeof wire, 3u, TFW_CMD_RESET, &mode, 1u);

    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 0));
    TEST_ASSERT_EQUAL_INT(TFW_OK, got[1].status);
    TEST_ASSERT_TRUE(tfw_mock_reset_count() >= 1u);
}

static size_t timer_set(uint8_t *wire, size_t cap, uint8_t seq, uint32_t ms)
{
    const uint8_t pay[6] = { 13u, 1u, (uint8_t)ms, (uint8_t)(ms >> 8),
                             (uint8_t)(ms >> 16), (uint8_t)(ms >> 24) };
    return frame(wire, cap, seq, TFW_CMD_TIMER_SET, pay, 6u);
}

static void test_run_sweeps_a_timer_that_falls_due(void)
{
    const uint8_t pin = 13u;
    uint8_t a[32], b[32], c[32];
    const uint8_t *pk[3] = { a, b, c };
    size_t  len[3];

    len[0] = timer_set(a, sizeof a, 1u, 1u);
    len[1] = frame(b, sizeof b, 2u, TFW_CMD_TIMER_QUERY, &pin, 1u);
    len[2] = frame(c, sizeof c, 3u, TFW_CMD_TIMER_QUERY, &pin, 1u);
    TEST_ASSERT_EQUAL_INT(0, run_packets(pk, len, 3u, 0));
    TEST_ASSERT_EQUAL_UINT(4u, ngot);
    TEST_ASSERT_EQUAL_INT(TFW_OK, got[1].status);
    TEST_ASSERT_EQUAL_UINT8(1u, got[2].pay[0]);   /* due, not yet swept */
    TEST_ASSERT_EQUAL_UINT8(0u, got[3].pay[0]);   /* swept */
}

static void test_run_starts_with_no_timer_from_an_earlier_run(void)
{
    const uint8_t pin = 13u;
    uint8_t wire[32];
    size_t  n = timer_set(wire, sizeof wire, 1u, 100000u);

    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 0));
    TEST_ASSERT_EQUAL_INT(TFW_OK, got[1].status);
    ngot = 0u;
    outlen = 0u;
    n = frame(wire, sizeof wire, 2u, TFW_CMD_TIMER_QUERY, &pin, 1u);
    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, got[1].pay[0]);
}

static void test_run_starts_with_no_partial_frame_from_an_earlier_run(void)
{
    uint8_t wire[64];
    size_t  n = frame(wire, sizeof wire, 0x31u, TFW_CMD_PING, (const uint8_t *)"0123456789", 10u);

    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n / 2u, 0));
    ngot = 0u;
    outlen = 0u;
    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 0));
    TEST_ASSERT_EQUAL_UINT(2u, ngot);
    TEST_ASSERT_EQUAL_HEX8(0x31u, got[1].seq);
    TEST_ASSERT_EQUAL_INT(TFW_OK, got[1].status);
}

static void test_run_brings_the_bus_up(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_pipe(NULL, 0u, 0));
    TEST_ASSERT_NOT_EQUAL_UINT32(0xFFFFFFFFu, tfw_mock_last_i2c_hz());
}

static void test_run_applies_the_environment_after_the_reset(void)
{
    uint32_t us = 0u;

    clear_env();
    setenv("TFWSIM_PULSE_IN_US", "77", 1);
    TEST_ASSERT_EQUAL_INT(0, run_pipe(NULL, 0u, 0));
    clear_env();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(NULL, 1u, 1000000u, &us));
    TEST_ASSERT_EQUAL_UINT32(77u, us);
}

static void test_run_fails_1_on_an_unreadable_input(void)
{
    int p[2], q[2];

    TEST_ASSERT_EQUAL_INT(0, pipe(p));
    TEST_ASSERT_EQUAL_INT(0, pipe(q));
    close(p[0]);
    close(p[1]);                      /* p[0] is now a closed descriptor */
    TEST_ASSERT_EQUAL_INT(1, run(p[0], q[1], 0));
    close(q[0]);
    close(q[1]);
}

static void test_run_with_eth_still_ends_0_when_input_closes(void)
{
    uint8_t wire[32];
    size_t  n = frame(wire, sizeof wire, 9u, TFW_CMD_PING, NULL, 0u);

    TEST_ASSERT_EQUAL_INT(0, run_pipe(wire, n, 1));
    TEST_ASSERT_EQUAL_UINT(2u, ngot);
    TEST_ASSERT_EQUAL_HEX8(9u, got[1].seq);
}

/* With --eth an idle wait is a 1 ms tick: time moves with no input at all. A
 * thread holds the input open and closes it once the clock has moved. */
static int idle_wr;

static void *close_after_ticks(void *arg)
{
    (void)arg;
    while (tfw_hal_millis() < 3u) { /* spin: the loop's own ticks move it */ }
    close(idle_wr);
    return NULL;
}

static void test_run_with_eth_ticks_while_input_is_idle(void)
{
    int       p[2], q[2];
    pthread_t th;

    TEST_ASSERT_EQUAL_INT(0, pipe(p));
    TEST_ASSERT_EQUAL_INT(0, pipe(q));
    idle_wr = p[1];
    tfw_mock_set_millis(0u);
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, close_after_ticks, NULL));
    TEST_ASSERT_EQUAL_INT(0, run(p[0], q[1], 1));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(th, NULL));
    TEST_ASSERT_TRUE(tfw_hal_millis() >= 3u);
    close(p[0]);
    close(q[0]);
    close(q[1]);
}

/* ---- main(): arguments ------------------------------------------------- */

static void test_main_refuses_an_unknown_option_with_the_usage(void)
{
    char *argv[] = { "tfwsim-x", "--nope", NULL };

    TEST_ASSERT_EQUAL_INT(2, main_args(2, argv));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "tfwsim: unknown option --nope\n"));
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "usage: tfwsim-x [--pty] [--eth] [--two V]\n"));
    TEST_ASSERT_EQUAL_INT(1, tier_calls);
    TEST_ASSERT_EQUAL_INT(1, tier_argc[0]);
}

static void test_main_exits_2_quietly_when_the_tier_has_reported(void)
{
    char *argv[] = { "tfwsim", "--bad", NULL };

    TEST_ASSERT_EQUAL_INT(2, main_args(2, argv));
    TEST_ASSERT_EQUAL_STRING("", errbuf);
}

static void test_main_skips_what_the_tier_consumed(void)
{
    char *argv[] = { "tfwsim", "--two", "v", "--nope", NULL };

    TEST_ASSERT_EQUAL_INT(2, main_args(4, argv));
    TEST_ASSERT_EQUAL_INT(2, tier_calls);
    TEST_ASSERT_EQUAL_STRING("--two", tier_arg0[0]);
    TEST_ASSERT_EQUAL_INT(3, tier_argc[0]);
    TEST_ASSERT_EQUAL_STRING("--nope", tier_arg0[1]);
    TEST_ASSERT_EQUAL_INT(1, tier_argc[1]);
    TEST_ASSERT_NOT_NULL(strstr(errbuf, "unknown option --nope"));
}

static void test_main_keeps_pty_and_eth_from_the_tier(void)
{
    char *argv[] = { "tfwsim", "--pty", "--eth", "--bad", NULL };
    pid_t pid;

    fflush(stdout);
    pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        tfw_netcfg_t cfg;
        int rc = main_args(4, argv);

        /* --eth put the controller on the bus: a start is no longer refused.
         * Port 0 on loopback is refused at once, which reads as DOWN. */
        memset(&cfg, 0, sizeof cfg);
        cfg.peer[0] = 127u; cfg.peer[3] = 1u;
        exit(rc * 16 + (tier_calls == 1 && strcmp(tier_arg0[0], "--bad") == 0) * 2 +
             (tfw_eth_start(&cfg, 0u) == TFW_OK));
    }
    TEST_ASSERT_EQUAL_INT(2 * 16 + 2 + 1, child_status(pid));
}

static void test_main_without_eth_leaves_the_controller_off(void)
{
    tfw_netcfg_t cfg;

    memset(&cfg, 0, sizeof cfg);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 0u));
}

/* ---- main(): the two transports ---------------------------------------- */

static void test_main_serves_stdio_and_exits_0_at_eof(void)
{
    char   *argv[] = { "tfwsim", NULL };
    int     in_p[2], out_p[2];
    uint8_t wire[32];
    size_t  n = frame(wire, sizeof wire, 0x44u, TFW_CMD_PING, NULL, 0u);
    pid_t   pid;

    TEST_ASSERT_EQUAL_INT(0, pipe(in_p));
    TEST_ASSERT_EQUAL_INT(0, pipe(out_p));
    fflush(stdout);
    pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        dup2(in_p[0], STDIN_FILENO);
        dup2(out_p[1], STDOUT_FILENO);
        close(in_p[0]); close(in_p[1]); close(out_p[0]); close(out_p[1]);
        exit(tfwsim_main(1, argv));
    }
    close(in_p[0]);
    close(out_p[1]);
    TEST_ASSERT_EQUAL_INT((int)n, (int)write(in_p[1], wire, n));
    close(in_p[1]);
    slurp(out_p[0]);
    close(out_p[0]);
    TEST_ASSERT_EQUAL_INT(0, child_status(pid));
    decode(outbuf, outlen);
    TEST_ASSERT_EQUAL_UINT(2u, ngot);
    TEST_ASSERT_EQUAL_HEX8(0x44u, got[1].seq);
}

static void test_main_serves_a_pty_named_on_stderr(void)
{
    char   *argv[] = { "tfwsim", "--pty", NULL };
    int     ep[2], fd;
    char    name[128];
    size_t  nl = 0u;
    uint8_t wire[32], b;
    size_t  n = frame(wire, sizeof wire, 0x55u, TFW_CMD_PING, NULL, 0u);
    struct termios tio;
    tfw_framer_t   f;
    int     answered = 0;
    pid_t   pid;

    TEST_ASSERT_EQUAL_INT(0, pipe(ep));
    fflush(stdout);
    pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        dup2(ep[1], 2);
        close(ep[0]); close(ep[1]);
        exit(tfwsim_main(2, argv));
    }
    close(ep[1]);
    while (nl < sizeof name - 1u && read(ep[0], &name[nl], 1u) == 1 && name[nl] != '\n') {
        nl++;
    }
    name[nl] = '\0';
    close(ep[0]);
    TEST_ASSERT_EQUAL_STRING_LEN("/dev/pts/", name, 9u);

    fd = open(name, O_RDWR | O_NOCTTY);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, tcgetattr(fd, &tio));
    tio.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= ~(tcflag_t)OPOST;
    tio.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    TEST_ASSERT_EQUAL_INT(0, tcsetattr(fd, TCSANOW, &tio));
    TEST_ASSERT_EQUAL_INT((int)n, (int)write(fd, wire, n));

    tfw_framer_init(&f);
    while (!answered && read(fd, &b, 1u) == 1) {
        if (tfw_framer_push(&f, b) == TFW_FRAME_READY &&
            tfw_frame_op(&f) == (TFW_CMD_PING | TFW_OP_RESPONSE_BIT)) {
            answered = tfw_frame_seq(&f) == 0x55u;
        }
    }
    close(fd);
    TEST_ASSERT_TRUE(answered);
    /* The peer hung up: the pty reads as an error, and tfwsim exits 1. */
    TEST_ASSERT_EQUAL_INT(1, child_status(pid));
}

/* --eth reaches run(): the loop ticks on its 1 ms wait while stdin is idle, and
 * input that never closes by itself is closed by that ticking. Without it the
 * wait is on stdin alone and the alarm ends the child unexited. */
static void test_main_with_eth_ticks_while_stdin_is_idle(void)
{
    char *argv[] = { "tfwsim", "--eth", NULL };
    pid_t pid;

    fflush(stdout);
    pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        int       p[2], null = open("/dev/null", O_WRONLY);
        pthread_t th;

        if (pipe(p) != 0 || null < 0) { exit(100); }
        dup2(p[0], STDIN_FILENO);
        dup2(null, STDOUT_FILENO);
        close(p[0]);
        close(null);
        idle_wr = p[1];
        tfw_mock_set_millis(0u);
        alarm(30u);
        if (pthread_create(&th, NULL, close_after_ticks, NULL) != 0) { exit(101); }
        exit(tfwsim_main(2, argv));
    }
    TEST_ASSERT_EQUAL_INT(0, child_status(pid));
}

/* The controller's socket is waited on beside the input, so traffic on it
 * wakes the loop. The peer is a listener on an ephemeral loopback port. */
static void test_run_with_eth_waits_on_the_socket_too(void)
{
    struct sockaddr_in a;
    socklen_t          al = sizeof a;
    tfw_netcfg_t       cfg;
    int                ls, p[2], q[2];
    pthread_t          th;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE(ls >= 0);
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_EQUAL_INT(0, bind(ls, (struct sockaddr *)&a, sizeof a));
    TEST_ASSERT_EQUAL_INT(0, listen(ls, 1));
    TEST_ASSERT_EQUAL_INT(0, getsockname(ls, (struct sockaddr *)&a, &al));
    memset(&cfg, 0, sizeof cfg);
    cfg.peer[0] = 127u; cfg.peer[3] = 1u;
    cfg.port    = ntohs(a.sin_port);
    tfwsim_eth_enable();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 0u));
    TEST_ASSERT_TRUE(tfwsim_eth_fd() >= 0);

    TEST_ASSERT_EQUAL_INT(0, pipe(p));
    TEST_ASSERT_EQUAL_INT(0, pipe(q));
    idle_wr      = p[1];
    socket_waits = 0;
    tfw_mock_set_millis(0u);
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&th, NULL, close_after_ticks, NULL));
    TEST_ASSERT_EQUAL_INT(0, run(p[0], q[1], 1));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(th, NULL));
    TEST_ASSERT_TRUE(socket_waits >= 1);
    tfw_eth_close();
    close(ls);
    close(p[0]);
    close(q[0]);
    close(q[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hex_nibble_takes_each_digit_range_at_its_bounds);
    RUN_TEST(test_hex_nibble_refuses_the_neighbours_of_each_range);
    RUN_TEST(test_preset_mock_with_nothing_set_leaves_the_reset_mock);
    RUN_TEST(test_preset_mock_reads_the_pulse_width);
    RUN_TEST(test_preset_mock_reads_a_capture_list_up_to_its_end);
    RUN_TEST(test_preset_mock_stops_a_capture_list_at_a_bad_separator);
    RUN_TEST(test_preset_mock_caps_a_capture_list_at_32);
    RUN_TEST(test_preset_mock_decodes_a_bus_response_in_either_case);
    RUN_TEST(test_preset_mock_drops_a_lone_or_bad_trailing_nibble);
    RUN_TEST(test_preset_mock_caps_a_bus_response_at_32_bytes);
    RUN_TEST(test_preset_mock_sets_one_wire_presence_only_for_1);
    RUN_TEST(test_run_carries_out_a_latched_reset);
    RUN_TEST(test_run_with_no_input_announces_boot_and_returns_0);
    RUN_TEST(test_run_answers_a_ping_after_the_boot_event);
    RUN_TEST(test_run_answers_every_frame_of_one_full_read);
    RUN_TEST(test_run_advances_time_one_ms_per_read_from_a_fresh_clock);
    RUN_TEST(test_run_bridges_the_framer_errors_into_sys_info);
    RUN_TEST(test_run_sweeps_a_timer_that_falls_due);
    RUN_TEST(test_run_starts_with_no_timer_from_an_earlier_run);
    RUN_TEST(test_run_starts_with_no_partial_frame_from_an_earlier_run);
    RUN_TEST(test_run_brings_the_bus_up);
    RUN_TEST(test_run_applies_the_environment_after_the_reset);
    RUN_TEST(test_run_fails_1_on_an_unreadable_input);
    RUN_TEST(test_run_with_eth_still_ends_0_when_input_closes);
    RUN_TEST(test_main_without_eth_leaves_the_controller_off);
    RUN_TEST(test_main_refuses_an_unknown_option_with_the_usage);
    RUN_TEST(test_main_exits_2_quietly_when_the_tier_has_reported);
    RUN_TEST(test_main_skips_what_the_tier_consumed);
    RUN_TEST(test_main_keeps_pty_and_eth_from_the_tier);
    RUN_TEST(test_main_serves_stdio_and_exits_0_at_eof);
    RUN_TEST(test_main_serves_a_pty_named_on_stderr);
    RUN_TEST(test_run_with_eth_ticks_while_input_is_idle);
    RUN_TEST(test_main_with_eth_ticks_while_stdin_is_idle);
    /* Last: it enables the controller in this process, for good. */
    RUN_TEST(test_run_with_eth_waits_on_the_socket_too);
    return UNITY_END();
}
