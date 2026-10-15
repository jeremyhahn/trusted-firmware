/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The W5x00 driver -- src/net/w5x00.c over one chip file -- against the
 * software W5100 / W5500 in test/model/w5x00_model.c, which decodes each part's real
 * SPI framing. Built twice: W5X_TEST_CHIP is 1 for test_w5100 (w5100.c linked)
 * and 2 for test_w5500 (w5500.c linked). The same cases run against both, and
 * each binary also puts the OTHER part on the bus to prove its identity check
 * refuses it without writing to it.
 */

#include <string.h>
#include "unity.h"
#include "tfw/eth.h"
#include "tfw/board.h"
#include "w5x00_model.h"

#define CHIP   ((uint8_t)W5X_TEST_CHIP)
#define OTHER  ((uint8_t)(3u - W5X_TEST_CHIP))

/* Socket register offsets, from the datasheets, for inspecting the model. */
#define S_MR     0x00u
#define S_SR     0x03u
#define S_PORT   0x04u
#define S_DIPR   0x0Cu
#define S_DPORT  0x10u
#define S_RX_RD  0x28u

static const tfw_netcfg_t cfg = {
    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u },
    { 192u, 0u, 2u, 10u },
    { 255u, 255u, 255u, 0u },
    { 192u, 0u, 2u, 1u },
    { 192u, 0u, 2u, 20u },
    7020u
};

void setUp(void)    { w5x_model_reset(CHIP); }
void tearDown(void) {}

static void connect(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    w5x_model_peer_accept();
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

static void send_bytes(const uint8_t *b, uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(b[i]));
    }
}

static void test_start_programs_the_network_and_dials_the_endpoint(void)
{
    uint8_t i;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8(cfg.gateway[i], w5x_model_common((uint16_t)(0x01u + i)));
        TEST_ASSERT_EQUAL_UINT8(cfg.mask[i],    w5x_model_common((uint16_t)(0x05u + i)));
        TEST_ASSERT_EQUAL_UINT8(cfg.ip[i],      w5x_model_common((uint16_t)(0x0Fu + i)));
        TEST_ASSERT_EQUAL_UINT8(cfg.peer[i],    w5x_model_sock((uint16_t)(S_DIPR + i)));
    }
    for (i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL_UINT8(cfg.mac[i], w5x_model_common((uint16_t)(0x09u + i)));
    }
    TEST_ASSERT_EQUAL_UINT8(0x01u, w5x_model_sock(S_MR));          /* TCP */
    TEST_ASSERT_EQUAL_UINT16(50000u, w5x_model_sock16(S_PORT));
    TEST_ASSERT_EQUAL_UINT16(7020u, w5x_model_sock16(S_DPORT));
    TEST_ASSERT_EQUAL_UINT8(0x15u, w5x_model_sock(S_SR));          /* SYNSENT */
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, tfw_eth_state());
}

static void test_start_refuses_the_other_chip_and_never_writes_to_it(void)
{
    w5x_model_reset(OTHER);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 50000u));
    TEST_ASSERT_EQUAL_UINT32(0u, w5x_model_writes());
}

static void test_start_refuses_an_empty_socket_either_way_miso_floats(void)
{
    w5x_model_reset(W5X_MODEL_NONE_LOW);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 50000u));
    w5x_model_reset(W5X_MODEL_NONE_HIGH);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 50000u));
}

/* The W5100 has no version register. After a reset its RTR reads 0x07D0 and
 * its RCR 0x08, the pattern the probe read off the reference shield, and a
 * part where either byte of RTR or RCR differs is refused. The W5500 is known
 * by VERSIONR alone, so the same parts start. */
static void test_a_w5100_is_known_by_its_retry_registers_a_w5500_by_its_version(void)
{
    static const struct { uint16_t rtr; uint8_t rcr; } off[] = {
        { 0x06D0u, 0x08u }, { 0x07D1u, 0x08u }, { 0x07D0u, 0x09u },
    };
    const tfw_status_t want = CHIP == W5X_MODEL_W5100 ? TFW_ERR_UNSUPPORTED : TFW_OK;
    uint8_t i;

    for (i = 0u; i < sizeof off / sizeof off[0]; i++) {
        w5x_model_reset(CHIP);
        w5x_model_retry_reset(off[i].rtr, off[i].rcr);
        TEST_ASSERT_EQUAL(want, tfw_eth_start(&cfg, 50000u));
    }
}

static void test_the_sd_card_is_parked_and_the_chip_select_is_d10(void)
{
    tfw_pinref_t sd, cs;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(4u, &sd));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(10u, &cs));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    TEST_ASSERT_EQUAL_UINT8(1u, w5x_model_pin_output(sd.port, sd.bit));
    TEST_ASSERT_EQUAL_UINT8(1u, w5x_model_pin_high(sd.port, sd.bit));
    TEST_ASSERT_EQUAL_UINT16(cs.port, w5x_model_last_cs_port());
    TEST_ASSERT_EQUAL_UINT8(cs.bit, w5x_model_last_cs_bit());
}

/* A serial host's SPI_CONFIG can leave the bus in another mode between two
 * driver calls; the driver must not depend on how it found it. */
static void test_every_transaction_reapplies_mode_0_msb_first(void)
{
    static const uint8_t b[2] = { 0xA5u, 0x5Au };
    uint8_t got;

    connect();
    w5x_model_peer_send(b, 2u);
    (void)tfw_hal_spi_init(1u, 3u, 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&got));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, got);
}

static void test_state_follows_the_connection_and_a_hang_up_closes_it(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, tfw_eth_state());
    w5x_model_peer_accept();
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
    w5x_model_peer_close();
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));          /* CLOSED */
}

static void test_a_refused_or_timed_out_connect_reads_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    w5x_model_set_sr(0x00u);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
}

static void test_receive_reads_in_order_across_batches_and_returns_the_space(void)
{
    static const uint8_t a[3] = { 1u, 2u, 3u };
    static const uint8_t b[2] = { 4u, 5u };
    uint8_t got[5], i;

    connect();
    w5x_model_peer_send(a, 3u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&got[0]));
    w5x_model_peer_send(b, 2u);                  /* arrives mid-batch */
    for (i = 1u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&got[i]));
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((const uint8_t[]){ 1u, 2u, 3u, 4u, 5u }), got, 5);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&got[0]));
    TEST_ASSERT_EQUAL_UINT16(5u, w5x_model_sock16(S_RX_RD));   /* handed back */
}

/* 0xFFFD crosses both the 16-bit pointer's wrap and the 2 KB buffer's. */
static void test_receive_wraps_the_pointer_and_the_buffer(void)
{
    static const uint8_t b[6] = { 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u };
    uint8_t got[6], i;

    w5x_model_seed_pointers(0xFFFDu);
    connect();
    w5x_model_peer_send(b, 6u);
    for (i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&got[i]));
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, got, 6);
    TEST_ASSERT_EQUAL_UINT16(0x0003u, w5x_model_sock16(S_RX_RD));
}

static void test_send_puts_each_reply_on_the_wire(void)
{
    static const uint8_t a[5] = { 'h', 'e', 'l', 'l', 'o' };
    static const uint8_t b[2] = { 'o', 'k' };
    uint8_t wire[8];

    connect();
    send_bytes(a, 5u);
    TEST_ASSERT_EQUAL_UINT16(0u, w5x_model_peer_recv(wire, sizeof wire));  /* not yet */
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT16(5u, w5x_model_peer_recv(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, wire, 5);
    send_bytes(b, 2u);
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT16(2u, w5x_model_peer_recv(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, wire, 2);
    TEST_ASSERT_EQUAL_UINT8(2u, w5x_model_sends());
}

static void test_send_wraps_the_pointer_and_the_buffer(void)
{
    static const uint8_t b[6] = { 0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u };
    uint8_t wire[8];

    w5x_model_seed_pointers(0xFFFDu);
    connect();
    send_bytes(b, 6u);
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT16(6u, w5x_model_peer_recv(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, wire, 6);
}

static void test_send_with_nothing_queued_issues_no_send(void)
{
    connect();
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_sends());
}

static void test_a_full_transmit_buffer_refuses_the_byte(void)
{
    connect();
    w5x_model_tx_cap(0u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_tx(0x55u));
}

/* A reply longer than the free space: what fits is sent, and the rest follows
 * once the chip has room again. Nothing is lost or reordered. */
static void test_a_buffer_that_fills_mid_reply_sends_what_is_there(void)
{
    static const uint8_t b[5] = { 1u, 2u, 3u, 4u, 5u };
    uint8_t wire[8];

    connect();
    w5x_model_tx_cap(3u);
    send_bytes(b, 5u);
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT16(5u, w5x_model_peer_recv(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, wire, 5);
    TEST_ASSERT_EQUAL_UINT8(2u, w5x_model_sends());
}

static void test_a_command_the_chip_never_takes_is_given_up(void)
{
    w5x_model_cr_stuck(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_eth_start(&cfg, 50000u));
}

static void test_a_size_that_never_settles_reads_as_nothing(void)
{
    static const uint8_t b[4] = { 1u, 2u, 3u, 4u };
    uint8_t got;

    connect();
    w5x_model_peer_send(b, 4u);
    w5x_model_size_unstable(1u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&got));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_tx(0x55u));
}

static void test_close_closes_the_socket(void)
{
    connect();
    tfw_eth_close();
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_programs_the_network_and_dials_the_endpoint);
    RUN_TEST(test_start_refuses_the_other_chip_and_never_writes_to_it);
    RUN_TEST(test_start_refuses_an_empty_socket_either_way_miso_floats);
    RUN_TEST(test_a_w5100_is_known_by_its_retry_registers_a_w5500_by_its_version);
    RUN_TEST(test_the_sd_card_is_parked_and_the_chip_select_is_d10);
    RUN_TEST(test_every_transaction_reapplies_mode_0_msb_first);
    RUN_TEST(test_state_follows_the_connection_and_a_hang_up_closes_it);
    RUN_TEST(test_a_refused_or_timed_out_connect_reads_down);
    RUN_TEST(test_receive_reads_in_order_across_batches_and_returns_the_space);
    RUN_TEST(test_receive_wraps_the_pointer_and_the_buffer);
    RUN_TEST(test_send_puts_each_reply_on_the_wire);
    RUN_TEST(test_send_wraps_the_pointer_and_the_buffer);
    RUN_TEST(test_send_with_nothing_queued_issues_no_send);
    RUN_TEST(test_a_full_transmit_buffer_refuses_the_byte);
    RUN_TEST(test_a_buffer_that_fills_mid_reply_sends_what_is_there);
    RUN_TEST(test_a_command_the_chip_never_takes_is_given_up);
    RUN_TEST(test_a_size_that_never_settles_reads_as_nothing);
    RUN_TEST(test_close_closes_the_socket);
    return UNITY_END();
}
