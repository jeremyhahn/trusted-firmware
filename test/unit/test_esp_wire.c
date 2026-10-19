/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 wire (issue #48): src/hal/esp32/hal_uart.c over a mock lwIP
 * (test/mock/lwip_mock.c), under the real transport and dispatch, with the OTA
 * session reaching src/hal/esp32/hal_ota.c and the mock esp_ota behind it --
 * the path an OTA_DATA takes on the device from the socket to esp_ota_write.
 * hal_mock.c's byte link is weak; this one takes its place.
 *
 * What it holds: a refusal on this wire is a status reply and never a closed
 * socket; a second host cannot take a session that is live, only one gone
 * quiet; and every path that does close a socket says why at WARN.
 */
#include <errno.h>
#include <string.h>
#include "unity.h"
#include "transport.h"
#include "frame.h"
#include "sys.h"
#include "hal_mock.h"
#include "ota_stage.h"
#include "esp_ota_mock.h"
#include "lwip_mock.h"
#include "hal_esp32.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define CHUNK   60u          /* TFW_MAX_PAYLOAD less the offset */
#define IMG_LEN 1530u        /* 25 whole chunks and a short one of 30 */
#define NO_REPLY (-1)

static uint8_t         img[MOCK_ESP_SLOT_LEN];
static tfw_transport_t t;
static uint8_t         seq;
static unsigned        eth_starts;

/* The Ethernet link under the socket is hal_eth.c's, and not this suite's. */
tfw_status_t tfw_esp_eth_start(void)
{
    eth_starts++;
    return TFW_OK;
}

void setUp(void)
{
    unsigned i, n;

    /* hal_uart.c keeps its host from one test to the next, as the device does:
     * hang up whatever the last test left and let the wire let it go. */
    for (i = 0u; i < mock_lwip.nconn; i++) {
        mock_lwip.conn[i].in_pos     = mock_lwip.conn[i].in_len;
        mock_lwip.conn[i].hung_up    = 1u;
        mock_lwip.conn[i].recv_errno = 0;
        mock_lwip.conn[i].send_errno = 0;
        mock_lwip.conn[i].pending    = 0u;
    }
    for (n = 0u; n < 4u; n++) {
        uint8_t b;
        (void)tfw_hal_uart_rx(&b);
    }
    tfw_stage_init();        /* a session a failed test left open is given back */
    mock_lwip_reset();
    tfw_mock_reset();
    tfw_sys_reset_state();
    mock_esp_reset();
    tfw_stage_init();
    tfw_transport_init(&t);
    eth_starts = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.stale);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Polls the device until a whole reply to `cmd` is on c, and returns its status
 * -- or NO_REPLY if none came in a bound far past what one frame needs. */
static int reply_on(mock_conn_t *c, uint8_t cmd)
{
    tfw_framer_t f;
    unsigned     i, j;

    for (i = 0u; i < 64u; i++) {
        tfw_transport_poll(&t);
        tfw_framer_init(&f);
        for (j = 0u; j < c->out_len; j++) {
            if (tfw_framer_push(&f, c->out[j]) == TFW_FRAME_READY) {
                c->out_len = 0u;
                TEST_ASSERT_EQUAL_HEX8(cmd | TFW_OP_RESPONSE_BIT, tfw_frame_op(&f));
                TEST_ASSERT_EQUAL_UINT8(seq, tfw_frame_seq(&f));
                return (int)tfw_frame_status(&f);
            }
        }
    }
    return NO_REPLY;
}

/* One request from the host on c; the status that came back. */
static int ask(mock_conn_t *c, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len;

    seq++;
    len = tfw_frame_encode_request(wire, sizeof wire, seq, cmd, pay, n);
    TEST_ASSERT_NOT_EQUAL(0u, len);
    mock_lwip_feed(c, wire, len);
    return reply_on(c, cmd);
}

static int begin(mock_conn_t *c, uint32_t total)
{
    uint8_t in[4];

    put32(in, total);
    return ask(c, TFW_CMD_OTA_BEGIN, in, 4u);
}

static int data(mock_conn_t *c, uint32_t off, uint8_t n)
{
    uint8_t in[4u + CHUNK];

    put32(in, off);
    memcpy(&in[4], &img[off], n);
    return ask(c, TFW_CMD_OTA_DATA, in, (uint8_t)(4u + n));
}

static void stream(mock_conn_t *c, uint32_t from, uint32_t to)
{
    uint32_t off;
    uint8_t  n;

    for (off = from; off < to; off += n) {
        n = (uint8_t)(to - off < CHUNK ? to - off : CHUNK);
        TEST_ASSERT_EQUAL_INT(TFW_OK, data(c, off, n));
    }
}

/* A host that connected and was taken, with nothing sent yet. */
static mock_conn_t *host(void)
{
    mock_conn_t *c = mock_lwip_connect();

    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(c->open);
    return c;
}

/* ------------------------------------------------------------- the session */

static void test_an_image_streams_to_end_with_its_short_last_chunk(void)
{
    mock_conn_t *c = host();

    mock_esp_image(img, IMG_LEN, 3u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(c, IMG_LEN));
    stream(c, 0u, IMG_LEN - 30u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, data(c, IMG_LEN - 30u, 30u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_OTA_END, NULL, 0u));
    TEST_ASSERT_EQUAL_MEMORY(img, mock_esp.flash[1], IMG_LEN);
    TEST_ASSERT_TRUE(c->open);
    TEST_ASSERT_TRUE(c->nonblock);
    TEST_ASSERT_TRUE(c->nodelay);
    TEST_ASSERT_EQUAL_UINT(0u, mock_lwip.closes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_lwip.warns);
}

static void test_a_bad_chunk_gets_a_status_and_the_session_goes_on(void)
{
    static const uint8_t three[3] = { 0, 0, 0 };
    mock_conn_t *c = host();

    mock_esp_image(img, IMG_LEN, 3u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, data(c, 0u, CHUNK));       /* no session */
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(c, IMG_LEN));
    stream(c, 0u, 1500u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(c, 1440u, CHUNK));   /* again */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(c, 1560u, CHUNK));   /* ahead */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(c, 1500u, CHUNK));   /* past the end */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, ask(c, TFW_CMD_OTA_DATA, three, 3u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, data(c, 1500u, 30u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_OTA_END, NULL, 0u));
    TEST_ASSERT_TRUE(c->open);
    TEST_ASSERT_EQUAL_UINT(0u, mock_lwip.closes);
}

static void test_a_tampered_image_is_refused_at_end_with_crypto(void)
{
    mock_conn_t *c = host();

    mock_esp_image(img, IMG_LEN, 3u);
    img[700] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(c, IMG_LEN));
    stream(c, 0u, IMG_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, ask(c, TFW_CMD_OTA_END, NULL, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(c->open);
}

static void test_a_corrupted_frame_gets_a_crc_status_not_a_close(void)
{
    mock_conn_t *c = host();
    uint8_t      wire[TFW_MAX_FRAME];
    uint16_t     len;

    seq++;
    len = tfw_frame_encode_request(wire, sizeof wire, seq, TFW_CMD_PING, NULL, 0u);
    wire[len - 1u] ^= 0xFFu;
    mock_lwip_feed(c, wire, len);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, reply_on(c, TFW_CMD_PING));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(c->open);
}

/* ---------------------------------------------------------- a second host */

static void test_a_second_host_cannot_take_a_live_session(void)
{
    mock_conn_t *a = host(), *b;

    mock_esp_image(img, IMG_LEN, 3u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(a, IMG_LEN));
    stream(a, 0u, 600u);
    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS - 1u);
    b = mock_lwip_connect();
    stream(a, 600u, IMG_LEN);
    TEST_ASSERT_TRUE(b->closed);                                    /* refused */
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.warns);
    TEST_ASSERT_NOT_NULL(strstr(mock_lwip.last_warn, "refused a second host"));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(a, TFW_CMD_OTA_END, NULL, 0u));
    TEST_ASSERT_TRUE(a->open);
    TEST_ASSERT_EQUAL_UINT16(0u, b->out_len);
}

static void test_a_reply_after_a_long_command_keeps_the_host(void)
{
    mock_conn_t *a = host(), *b;
    uint8_t      wire[TFW_MAX_FRAME];
    uint16_t     len;

    /* The request is heard, the command runs past the window, and a second
     * host is waiting when it ends: the reply goes out first, and keeps it. */
    seq++;
    len = tfw_frame_encode_request(wire, sizeof wire, seq, TFW_CMD_PING, NULL, 0u);
    mock_lwip_feed(a, wire, len);
    while (a->in_pos < a->in_len) {
        tfw_transport_poll(&t);
    }
    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS + 500u);
    b = mock_lwip_connect();
    TEST_ASSERT_EQUAL_INT(TFW_OK, reply_on(a, TFW_CMD_PING));
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(a, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(a->open);
    TEST_ASSERT_TRUE(b->closed);
}

static void test_a_host_gone_quiet_is_replaced(void)
{
    mock_conn_t *a = host(), *b;

    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS);
    b = host();
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_TRUE(b->open);
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.warns);
    TEST_ASSERT_NOT_NULL(strstr(mock_lwip.last_warn, "replaces one quiet for 2000 ms"));
}

static void test_a_host_just_taken_keeps_the_wire_for_the_window(void)
{
    mock_conn_t *a = mock_lwip_connect(), *b, *c;
    uint8_t      byte;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&byte));
    TEST_ASSERT_TRUE(a->open);
    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS - 1u);
    b = mock_lwip_connect();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&byte));
    TEST_ASSERT_TRUE(b->closed);
    TEST_ASSERT_TRUE(a->open);
    tfw_mock_advance(1u);
    c = host();
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_TRUE(c->open);
    TEST_ASSERT_EQUAL_UINT(2u, mock_lwip.warns);
}

/* ------------------------------------------------- every close says why */

static void test_a_host_that_hangs_up_is_let_go_with_a_warning(void)
{
    mock_conn_t *a = host(), *b;

    a->hung_up = 1u;
    TEST_ASSERT_EQUAL_INT(NO_REPLY, reply_on(a, TFW_CMD_PING));
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: the host closed the connection", mock_lwip.last_warn);
    b = host();
    TEST_ASSERT_TRUE(b->open);
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.warns);
}

static void test_a_broken_recv_drops_the_host_with_its_errno(void)
{
    mock_conn_t *a = host();

    a->recv_errno = ECONNRESET;
    TEST_ASSERT_EQUAL_INT(NO_REPLY, reply_on(a, TFW_CMD_PING));
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.warns);
    TEST_ASSERT_NOT_NULL(strstr(mock_lwip.last_warn, "recv failed"));
}

static void test_a_broken_send_drops_the_host_with_its_errno(void)
{
    mock_conn_t *a = host();

    a->send_errno = EPIPE;
    TEST_ASSERT_EQUAL_INT(NO_REPLY, ask(a, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.warns);
    TEST_ASSERT_NOT_NULL(strstr(mock_lwip.last_warn, "send failed"));
}

static void test_a_full_send_buffer_waits_rather_than_drops(void)
{
    mock_conn_t *a = host();

    a->out_len = MOCK_LWIP_BUF;                  /* the host is not reading */
    seq++;
    {
        uint8_t  wire[TFW_MAX_FRAME];
        uint16_t len = tfw_frame_encode_request(wire, sizeof wire, seq, TFW_CMD_PING, NULL, 0u);
        unsigned i;

        mock_lwip_feed(a, wire, len);
        for (i = 0u; i < 16u; i++) {
            tfw_transport_poll(&t);
        }
    }
    TEST_ASSERT_TRUE(a->open);
    a->out_len = 0u;                             /* it reads again */
    TEST_ASSERT_EQUAL_INT(TFW_OK, reply_on(a, TFW_CMD_PING));
    TEST_ASSERT_EQUAL_UINT(0u, mock_lwip.warns);
}

static void test_the_listener_is_on_the_wire_port(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, eth_starts);
    TEST_ASSERT_TRUE(mock_lwip.listening);
    TEST_ASSERT_TRUE(mock_lwip.listen_nonblock);
    TEST_ASSERT_EQUAL_UINT16(TFW_ESP_WIRE_PORT, mock_lwip.port);
}

static void test_a_wire_that_cannot_listen_says_why(void)
{
    mock_lwip_reset();
    mock_lwip.bind_errno = EADDRINUSE;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_uart_init(115200u));
    TEST_ASSERT_FALSE(mock_lwip.listening);
    TEST_ASSERT_EQUAL_UINT(1u, mock_lwip.closes);
    TEST_ASSERT_NOT_NULL(strstr(mock_lwip.last_warn, "would not bind or listen"));
    mock_lwip.bind_errno = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));      /* for the next test */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_image_streams_to_end_with_its_short_last_chunk);
    RUN_TEST(test_a_bad_chunk_gets_a_status_and_the_session_goes_on);
    RUN_TEST(test_a_tampered_image_is_refused_at_end_with_crypto);
    RUN_TEST(test_a_corrupted_frame_gets_a_crc_status_not_a_close);
    RUN_TEST(test_a_second_host_cannot_take_a_live_session);
    RUN_TEST(test_a_reply_after_a_long_command_keeps_the_host);
    RUN_TEST(test_a_host_gone_quiet_is_replaced);
    RUN_TEST(test_a_host_just_taken_keeps_the_wire_for_the_window);
    RUN_TEST(test_a_host_that_hangs_up_is_let_go_with_a_warning);
    RUN_TEST(test_a_broken_recv_drops_the_host_with_its_errno);
    RUN_TEST(test_a_broken_send_drops_the_host_with_its_errno);
    RUN_TEST(test_a_full_send_buffer_waits_rather_than_drops);
    RUN_TEST(test_the_listener_is_on_the_wire_port);
    RUN_TEST(test_a_wire_that_cannot_listen_says_why);
    return UNITY_END();
}
