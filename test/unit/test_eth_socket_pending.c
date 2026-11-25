/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfwsim's Ethernet stand-in (tools/tfwsim/eth_socket.c, #55) while its
 * connect is still in flight. A loopback listener with a backlog of 0 and one
 * client already queued answers no further SYN, so the stand-in's
 * non-blocking connect stays pending: tfw_eth_state() must read BUSY until
 * the socket is writable, never UP on a poll that saw nothing.
 */
/* First: it defines _DEFAULT_SOURCE before any system header. */
#include "../../tools/tfwsim/eth_socket.c"
#include <arpa/inet.h>
#include "unity.h"

static int          lsn    = -1;
static int          filler = -1;
static tfw_netcfg_t cfg;

void setUp(void) {}

void tearDown(void)
{
    tfw_eth_close();
    if (filler >= 0) { (void)close(filler); filler = -1; }
    if (lsn >= 0)    { (void)close(lsn);    lsn    = -1; }
}

/* A loopback listener, backlog 0, its one accept-queue slot already taken. */
static void full_listener(void)
{
    struct sockaddr_in a;
    socklen_t          len = sizeof a;

    lsn = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE(lsn >= 0);
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_EQUAL_INT(0, bind(lsn, (const struct sockaddr *)&a, sizeof a));
    TEST_ASSERT_EQUAL_INT(0, getsockname(lsn, (struct sockaddr *)&a, &len));
    TEST_ASSERT_EQUAL_INT(0, listen(lsn, 0));
    filler = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE(filler >= 0);
    TEST_ASSERT_EQUAL_INT(0, connect(filler, (const struct sockaddr *)&a, sizeof a));
    memset(&cfg, 0, sizeof cfg);
    cfg.peer[0] = 127u; cfg.peer[3] = 1u;
    cfg.port    = ntohs(a.sin_port);
}

/* Mutant: eth_socket.c:112 `poll(&p, 1u, 0) > 0` as `>= 0`, which reads a
 * connect that has not completed as UP (SO_ERROR is still 0). */
static void test_a_connect_still_in_flight_reads_busy_not_up(void)
{
    uint8_t b;

    full_listener();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_eth_start(&cfg, 4000u));
    TEST_ASSERT_TRUE(tfwsim_eth_fd() >= 0);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
    TEST_ASSERT_TRUE(tfwsim_eth_fd() >= 0);
}

int main(void)
{
    UNITY_BEGIN();
    tfwsim_eth_enable();
    RUN_TEST(test_a_connect_still_in_flight_reads_busy_not_up);
    return UNITY_END();
}
