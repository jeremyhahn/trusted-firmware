/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 byte link, src/hal/esp32/hal_uart.c.
 *
 * test_esp_wire drives the unit through the whole stack over
 * test/mock/lwip_mock.c. This builds it alone, over a socket layer of its own
 * in this file, so each call it makes can be scripted and counted: a socket
 * or a listen that fails, a host on fd 0, a send that takes three bytes at a
 * time, a clock that wraps. It holds:
 *
 *   - init: the Ethernet status passed up, each socket call's failure mapped
 *     to TFW_ERR_INTERNAL with the listener closed and never accepted on, and
 *     the listener bound to INADDR_ANY:TFW_ESP_WIRE_PORT, reusable, backlog 1,
 *     non-blocking with its other flags kept;
 *   - the one-host rule at TFW_ESP_WIRE_IDLE_MS and one under it, across a
 *     wrap of the clock, with a host heard at its accept, its recv and its
 *     send, and a refused or replaced host closed with a warning;
 *   - a byte stream in and out unchanged, through partial sends, a full
 *     TX_BUF of exactly 512 bytes, and the pending reply sent before a new
 *     connection is weighed;
 *   - each socket error: EAGAIN keeps the host, anything else drops it with
 *     its errno, and a dropped host's unread and unsent bytes go with it.
 *
 * Deterministic: a literal clock, literal counts.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "tfw/hal.h"
#include "hal_esp32.h"

#define NFD       16
#define LSN_FD    7
#define OTHER_FL  0x400      /* a flag the unit must keep when it sets O_NONBLOCK */
#define TX_BUF    512u       /* hal_uart.c's */
#define RX_BUF    256u       /* hal_uart.c's */

typedef struct {
    int      fl;
    int      nodelay;
    int      reuse;
    unsigned closes;
    uint8_t  in[1024];
    uint16_t in_len, in_pos;
    int      hung;           /* recv reads 0 once `in` is drained */
    int      recv_err;       /* nonzero: recv fails with it once `in` is drained */
    uint8_t  out[2048];
    uint16_t out_len;
    unsigned send_cap;       /* bytes one send takes; 0 = all */
    int      send_block;     /* send fails with EAGAIN */
    int      send_err;       /* nonzero: send fails with it */
    unsigned sends, recvs;
    size_t   recv_len;
    int      recv_flags, send_flags;
} fd_t;

static struct {
    fd_t     fd[NFD];
    int      socket_fd;      /* what socket returns */
    int      lsn;            /* the listener it last made */
    int      bind_err, listen_err;
    int      bound_family;
    uint16_t bound_port;     /* network order, as the unit set it */
    uint32_t bound_addr;
    int      backlog;
    int      queue[8];
    unsigned nqueue, accepts, accepts_off_listener, fcntl_bad;
    unsigned warns;
    char     last_warn[160];
} k;

static tfw_ms_t     now;
static tfw_status_t eth_st;
static unsigned     eth_starts;

tfw_ms_t tfw_hal_millis(void)
{
    return now;
}

tfw_status_t tfw_esp_eth_start(void)
{
    eth_starts++;
    return eth_st;
}

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    int     n;

    TEST_ASSERT_EQUAL_CHAR('W', level);
    n = snprintf(k.last_warn, sizeof k.last_warn, "%s: ", tag);
    va_start(ap, fmt);
    (void)vsnprintf(&k.last_warn[n], sizeof k.last_warn - (size_t)n, fmt, ap);
    va_end(ap);
    k.warns++;
}

static fd_t *fd_of(int s)
{
    TEST_ASSERT_TRUE(s >= 0 && s < NFD);
    return &k.fd[s];
}

int lwip_socket(int domain, int type, int protocol)
{
    TEST_ASSERT_EQUAL_INT(AF_INET, domain);
    TEST_ASSERT_EQUAL_INT(SOCK_STREAM, type);
    TEST_ASSERT_EQUAL_INT(IPPROTO_TCP, protocol);
    if (k.socket_fd < 0) {
        errno = ENFILE;
    } else {
        k.lsn = k.socket_fd;
    }
    return k.socket_fd;
}

int lwip_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)name;

    TEST_ASSERT_EQUAL_INT(k.lsn, s);
    TEST_ASSERT_EQUAL_UINT(sizeof *in, namelen);
    k.bound_family = in->sin_family;
    k.bound_port   = in->sin_port;
    k.bound_addr   = in->sin_addr.s_addr;
    if (k.bind_err != 0) {
        errno = k.bind_err;
        return -1;
    }
    return 0;
}

int lwip_listen(int s, int backlog)
{
    TEST_ASSERT_EQUAL_INT(k.lsn, s);
    k.backlog = backlog;
    if (k.listen_err != 0) {
        errno = k.listen_err;
        return -1;
    }
    return 0;
}

int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    unsigned i;
    int      fd;

    (void)addr; (void)addrlen;
    if (s < 0 || s != k.lsn) {
        k.accepts_off_listener++;
        errno = EBADF;
        return -1;
    }
    if (k.nqueue == 0u) {
        errno = EAGAIN;
        return -1;
    }
    fd = k.queue[0];
    for (i = 1u; i < k.nqueue; i++) {
        k.queue[i - 1u] = k.queue[i];
    }
    k.nqueue--;
    k.accepts++;
    return fd;
}

int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    fd_t *f = fd_of(s);
    int   v = *(const int *)optval;

    TEST_ASSERT_EQUAL_UINT(sizeof(int), optlen);
    if (level == IPPROTO_TCP && optname == TCP_NODELAY) {
        f->nodelay = v;
    } else if (level == SOL_SOCKET && optname == SO_REUSEADDR) {
        f->reuse = v;
    } else {
        TEST_FAIL_MESSAGE("an option the unit does not set");
    }
    return 0;
}

ssize_t lwip_recv(int s, void *mem, size_t len, int flags)
{
    fd_t  *f = fd_of(s);
    size_t n;

    f->recvs++;
    f->recv_len   = len;
    f->recv_flags = flags;
    if (f->in_pos < f->in_len) {
        n = (size_t)(f->in_len - f->in_pos);
        n = n < len ? n : len;
        memcpy(mem, &f->in[f->in_pos], n);
        f->in_pos = (uint16_t)(f->in_pos + n);
        return (ssize_t)n;
    }
    if (f->hung) {
        return 0;
    }
    errno = f->recv_err != 0 ? f->recv_err : EAGAIN;
    return -1;
}

ssize_t lwip_send(int s, const void *data, size_t size, int flags)
{
    fd_t  *f = fd_of(s);
    size_t n = size;

    f->sends++;
    f->send_flags = flags;
    TEST_ASSERT_TRUE(size > 0u);
    if (f->send_err != 0) {
        errno = f->send_err;
        return -1;
    }
    if (f->send_block) {
        errno = EAGAIN;
        return -1;
    }
    if (f->send_cap != 0u && n > f->send_cap) {
        n = f->send_cap;
    }
    TEST_ASSERT_TRUE(f->out_len + n <= sizeof f->out);
    memcpy(&f->out[f->out_len], data, n);
    f->out_len = (uint16_t)(f->out_len + n);
    return (ssize_t)n;
}

int lwip_close(int s)
{
    fd_of(s)->closes++;
    return 0;
}

int lwip_fcntl(int s, int cmd, int val)
{
    fd_t *f;

    if (s < 0 || s >= NFD) {
        k.fcntl_bad++;
        errno = EBADF;
        return -1;
    }
    f = &k.fd[s];
    if (cmd == F_GETFL) {
        return f->fl;
    }
    TEST_ASSERT_EQUAL_INT(F_SETFL, cmd);
    f->fl = val;
    return 0;
}

uint16_t lwip_htons(uint16_t n)
{
    return (uint16_t)((n >> 8) | (n << 8));
}

uint32_t lwip_htonl(uint32_t n)
{
    return (n >> 24) | ((n >> 8) & 0xFF00u) | ((n << 8) & 0xFF0000u) | (n << 24);
}

/* ---- helpers ---------------------------------------------------------- */

static void fake_reset(void)
{
    unsigned i;

    memset(&k, 0, sizeof k);
    k.socket_fd = LSN_FD;
    k.lsn       = LSN_FD;
    for (i = 0u; i < NFD; i++) {
        k.fd[i].fl = OTHER_FL;
    }
}

static void arrive(int fd)
{
    k.queue[k.nqueue++] = fd;
}

static void feed(int fd, const uint8_t *b, uint16_t n)
{
    fd_t *f = &k.fd[fd];

    memcpy(&f->in[f->in_len], b, n);
    f->in_len = (uint16_t)(f->in_len + n);
}

/* One pass of the link with nothing to read: what the superloop does idle. */
static void idle_pass(void)
{
    uint8_t b;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

static void send_str(const char *s)
{
    while (*s != '\0') {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)*s++));
    }
}

void setUp(void)
{
    unsigned i;

    /* hal_uart.c keeps its host from one test to the next, as the device
     * does: make every socket fail and poll until it lets the host go. */
    k.nqueue = 0u;
    for (i = 0u; i < NFD; i++) {
        k.fd[i].in_pos   = k.fd[i].in_len;
        k.fd[i].hung     = 1;
        k.fd[i].send_err = EPIPE;
    }
    for (i = 0u; i < RX_BUF + 2u; i++) {
        uint8_t b;
        (void)tfw_hal_uart_rx(&b);
    }
    fake_reset();
    now        = 1000u;
    eth_st     = TFW_OK;
    eth_starts = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    k.warns = 0u;
}

void tearDown(void) {}

/* ---- init ------------------------------------------------------------- */

static void test_the_listener_is_bound_reusable_and_nonblocking(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, eth_starts);
    TEST_ASSERT_EQUAL_INT(AF_INET, k.bound_family);
    TEST_ASSERT_EQUAL_HEX16(lwip_htons(TFW_ESP_WIRE_PORT), k.bound_port);
    TEST_ASSERT_EQUAL_HEX32(0u, k.bound_addr);
    TEST_ASSERT_EQUAL_INT(1, k.backlog);
    TEST_ASSERT_EQUAL_INT(1, k.fd[LSN_FD].reuse);
    TEST_ASSERT_EQUAL_HEX(OTHER_FL | O_NONBLOCK, k.fd[LSN_FD].fl);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[LSN_FD].closes);
}

static void test_an_ethernet_failure_is_passed_up_before_any_socket(void)
{
    fake_reset();
    eth_st      = TFW_ERR_UNSUPPORTED;
    k.socket_fd = 9;                /* a socket made anyway would show here */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL_INT(0, k.backlog);
    TEST_ASSERT_EQUAL_INT(0, k.fd[9].reuse);
}

static void test_no_socket_is_internal_and_nothing_is_accepted(void)
{
    fake_reset();
    k.socket_fd = -1;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_uart_init(0u));
    arrive(3);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.accepts_off_listener);
    TEST_ASSERT_EQUAL_UINT(0u, k.accepts);
    TEST_ASSERT_EQUAL_UINT(0u, k.fcntl_bad);
}

static void check_listen_failure(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[LSN_FD].closes);
    TEST_ASSERT_EQUAL_UINT(1u, k.warns);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: no wire: tcp/21574 would not bind or listen, errno 98",
                             k.last_warn);
    TEST_ASSERT_EQUAL_HEX(OTHER_FL, k.fd[LSN_FD].fl);   /* never made non-blocking */
    arrive(3);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.accepts);
    TEST_ASSERT_EQUAL_UINT(0u, k.accepts_off_listener);
}

static void test_a_bind_failure_closes_the_listener_and_says_why(void)
{
    fake_reset();
    k.bind_err = EADDRINUSE;
    check_listen_failure();
    TEST_ASSERT_EQUAL_INT(0, k.backlog);     /* no listen after a failed bind */
}

static void test_a_listen_failure_closes_the_listener_and_says_why(void)
{
    fake_reset();
    k.listen_err = EADDRINUSE;
    check_listen_failure();
}

static void test_a_listener_on_fd_0_accepts(void)
{
    fake_reset();
    k.socket_fd = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL_HEX(OTHER_FL | O_NONBLOCK, k.fd[0].fl);
    arrive(3);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.accepts);
    TEST_ASSERT_EQUAL_INT(1, k.fd[3].nodelay);
}

/* ---- one host --------------------------------------------------------- */

static void test_a_host_on_fd_0_is_taken_nonblocking_and_nodelay(void)
{
    uint8_t b = 0u;

    arrive(0);
    feed(0, (const uint8_t *)"A", 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_UINT8('A', b);
    TEST_ASSERT_EQUAL_HEX(OTHER_FL | O_NONBLOCK, k.fd[0].fl);
    TEST_ASSERT_EQUAL_INT(1, k.fd[0].nodelay);
    TEST_ASSERT_EQUAL_UINT(RX_BUF, k.fd[0].recv_len);
    TEST_ASSERT_EQUAL_INT(MSG_DONTWAIT, k.fd[0].recv_flags);
    send_str("ok");
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(2u, k.fd[0].out_len);
    TEST_ASSERT_EQUAL_MEMORY("ok", k.fd[0].out, 2u);
    TEST_ASSERT_EQUAL_INT(MSG_DONTWAIT, k.fd[0].send_flags);
    /* fd 0 is a host: a hang-up closes it. */
    k.fd[0].hung = 1;
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[0].closes);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: the host closed the connection", k.last_warn);
}

static void test_a_host_on_fd_0_keeps_the_wire_for_the_window(void)
{
    arrive(0);
    idle_pass();
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[0].closes);
    TEST_ASSERT_EQUAL_INT(0, k.fd[4].nodelay);
}

static void test_no_connection_touches_nothing(void)
{
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.fcntl_bad);
    TEST_ASSERT_EQUAL_UINT(0u, k.warns);
    /* Bytes with no host go nowhere, and are not kept for the next one. */
    send_str("lost");
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    arrive(3);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].sends);
}

static void take(int fd)
{
    arrive(fd);
    idle_pass();
    TEST_ASSERT_EQUAL_HEX(OTHER_FL | O_NONBLOCK, k.fd[fd].fl);
}

static void test_a_second_host_inside_the_window_is_refused(void)
{
    take(3);
    now += TFW_ESP_WIRE_IDLE_MS - 1u;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_HEX(OTHER_FL, k.fd[4].fl);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: refused a second host: the current one moved a byte 1999 ms ago",
                             k.last_warn);
    /* The first host still has the wire. */
    feed(3, (const uint8_t *)"x", 1u);
    {
        uint8_t b;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    }
}

static void test_a_host_quiet_for_the_window_is_replaced(void)
{
    take(3);
    feed(3, (const uint8_t *)"abc", 3u);
    {
        uint8_t b;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));   /* "bc" left unread */
    }
    k.fd[3].send_block = 1;
    send_str("old");                                        /* and "old" unsent */
    now += TFW_ESP_WIRE_IDLE_MS;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_INT(1, k.fd[4].nodelay);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: a new host replaces one quiet for 2000 ms", k.last_warn);
    /* The old host's bytes went with it, both ways. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[4].sends);
    feed(4, (const uint8_t *)"N", 1u);
    {
        uint8_t b = 0u;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
        TEST_ASSERT_EQUAL_UINT8('N', b);
    }
}

static void test_the_window_holds_across_a_clock_wrap(void)
{
    now = 0xFFFFFC00u;
    take(3);
    now = 0xFFFFFC00u + TFW_ESP_WIRE_IDLE_MS - 1u;          /* wrapped */
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    now++;
    arrive(5);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[5].closes);
}

static void test_a_host_just_taken_is_heard(void)
{
    now = 50000u;           /* long after setUp's clock */
    take(3);
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
}

static void test_a_byte_received_is_heard(void)
{
    uint8_t b;

    take(3);
    now += 1500u;
    feed(3, (const uint8_t *)"r", 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    now += 1500u;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
}

static void test_a_byte_sent_is_heard(void)
{
    take(3);
    now += 1500u;
    send_str("s");
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    now += 1500u;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
}

static void test_a_blocked_send_is_not_heard(void)
{
    take(3);
    k.fd[3].send_block = 1;
    send_str("s");
    now += 1500u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    now += 500u;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(1u, k.warns);         /* only the replacement's */
}

static void test_a_pending_reply_is_sent_before_a_new_host_is_weighed(void)
{
    take(3);
    k.fd[3].send_block = 1;
    send_str("late");
    now += TFW_ESP_WIRE_IDLE_MS + 500u;     /* a long command */
    k.fd[3].send_block = 0;
    arrive(4);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(4u, k.fd[3].out_len);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[4].closes);
}

/* ---- the byte stream ------------------------------------------------- */

static void test_bytes_come_in_order_one_recv_per_buffer(void)
{
    uint8_t  in[RX_BUF + 44u];
    uint8_t  b;
    unsigned i;

    for (i = 0u; i < sizeof in; i++) {
        in[i] = (uint8_t)(i * 7u + 1u);
    }
    take(3);
    feed(3, in, (uint16_t)sizeof in);
    for (i = 0u; i < sizeof in; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
        TEST_ASSERT_EQUAL_UINT8(in[i], b);
    }
    TEST_ASSERT_EQUAL_UINT(3u, k.fd[3].recvs);   /* take's empty one, then one per buffer */
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.warns);
}

static void test_a_partial_send_keeps_the_rest_in_order(void)
{
    take(3);
    k.fd[3].send_cap = 3u;
    send_str("0123456789");
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(3u, k.fd[3].out_len);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(10u, k.fd[3].out_len);
    TEST_ASSERT_EQUAL_MEMORY("0123456789", k.fd[3].out, 10u);
    TEST_ASSERT_EQUAL_UINT(4u, k.fd[3].sends);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(4u, k.fd[3].sends);   /* nothing to send, no send */
}

static void test_the_send_buffer_holds_exactly_tx_buf(void)
{
    unsigned i;

    take(3);
    k.fd[3].send_block = 1;
    for (i = 0u; i < TX_BUF; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)i));
    }
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].sends);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xAAu));
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].sends);
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
    /* Room made by a send takes the byte. */
    k.fd[3].send_block = 0;
    k.fd[3].send_cap   = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xAAu));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xBBu));
    k.fd[3].send_cap = 0u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(TX_BUF + 2u, k.fd[3].out_len);
    for (i = 0u; i < TX_BUF; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, k.fd[3].out[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(0xAAu, k.fd[3].out[TX_BUF]);
    TEST_ASSERT_EQUAL_UINT8(0xBBu, k.fd[3].out[TX_BUF + 1u]);
}

static void test_a_full_buffer_whose_send_fails_drops_the_host(void)
{
    unsigned i;

    take(3);
    k.fd[3].send_block = 1;
    for (i = 0u; i < TX_BUF; i++) {
        (void)tfw_hal_uart_tx_try(0u);
    }
    k.fd[3].send_err = ECONNRESET;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(1u));
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: dropped the host: send failed, errno 104", k.last_warn);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].out_len);
}

/* ---- socket errors ---------------------------------------------------- */

static void test_eagain_on_recv_keeps_the_host_quietly(void)
{
    take(3);
    idle_pass();
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.warns);
    feed(3, (const uint8_t *)"k", 1u);
    {
        uint8_t b;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    }
}

static void test_a_broken_recv_drops_the_host_with_its_errno(void)
{
    take(3);
    k.fd[3].recv_err = ECONNRESET;
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: dropped the host: recv failed, errno 104", k.last_warn);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);   /* let go once */
}

static void test_a_broken_send_drops_the_host_and_its_bytes(void)
{
    take(3);
    feed(3, (const uint8_t *)"zz", 2u);
    {
        uint8_t b;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));   /* one "z" unread */
    }
    k.fd[3].send_err = EPIPE;
    send_str("x");
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT(1u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_STRING("tfw-wire: dropped the host: send failed, errno 32", k.last_warn);
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(1u, k.warns);
}

static void test_eagain_on_send_keeps_the_host_and_the_bytes(void)
{
    take(3);
    k.fd[3].send_block = 1;
    send_str("hold");
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    idle_pass();
    TEST_ASSERT_EQUAL_UINT(0u, k.fd[3].closes);
    TEST_ASSERT_EQUAL_UINT(0u, k.warns);
    k.fd[3].send_block = 0;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_MEMORY("hold", k.fd[3].out, 4u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_listener_is_bound_reusable_and_nonblocking);
    RUN_TEST(test_an_ethernet_failure_is_passed_up_before_any_socket);
    RUN_TEST(test_no_socket_is_internal_and_nothing_is_accepted);
    RUN_TEST(test_a_bind_failure_closes_the_listener_and_says_why);
    RUN_TEST(test_a_listen_failure_closes_the_listener_and_says_why);
    RUN_TEST(test_a_listener_on_fd_0_accepts);
    RUN_TEST(test_a_host_on_fd_0_is_taken_nonblocking_and_nodelay);
    RUN_TEST(test_a_host_on_fd_0_keeps_the_wire_for_the_window);
    RUN_TEST(test_no_connection_touches_nothing);
    RUN_TEST(test_a_second_host_inside_the_window_is_refused);
    RUN_TEST(test_a_host_quiet_for_the_window_is_replaced);
    RUN_TEST(test_the_window_holds_across_a_clock_wrap);
    RUN_TEST(test_a_host_just_taken_is_heard);
    RUN_TEST(test_a_byte_received_is_heard);
    RUN_TEST(test_a_byte_sent_is_heard);
    RUN_TEST(test_a_blocked_send_is_not_heard);
    RUN_TEST(test_a_pending_reply_is_sent_before_a_new_host_is_weighed);
    RUN_TEST(test_bytes_come_in_order_one_recv_per_buffer);
    RUN_TEST(test_a_partial_send_keeps_the_rest_in_order);
    RUN_TEST(test_the_send_buffer_holds_exactly_tx_buf);
    RUN_TEST(test_a_full_buffer_whose_send_fails_drops_the_host);
    RUN_TEST(test_eagain_on_recv_keeps_the_host_quietly);
    RUN_TEST(test_a_broken_recv_drops_the_host_with_its_errno);
    RUN_TEST(test_a_broken_send_drops_the_host_and_its_bytes);
    RUN_TEST(test_eagain_on_send_keeps_the_host_and_the_bytes);
    return UNITY_END();
}
