/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/net/w5100.c on its own: the W5100's SPI frame and address map, byte for
 * byte.
 *
 * test_w5100 drives the chip file through w5x00.c against the software W5100,
 * which maps addresses with the same reading of the datasheet the chip file
 * was written from, so a map both get wrong in the same way passes. Here the
 * suite stands in for w5x00.c's bus -- w5x_bus_init and w5x_xfer -- and every
 * frame the chip file sends is compared with a literal from the W5100
 * datasheet's SPI operation: opcode 0xF0 to write or 0x0F to read, the 16-bit
 * address high byte first, one data byte, in one four-byte transaction. The
 * datasheet's memory map: common registers at 0x0000, socket 0's at 0x0400, its 2 KB TX buffer at
 * 0x4000 and RX buffer at 0x6000, the pointer wrapped into the buffer by the
 * chip file. During the header the W5100 shifts out 0x00, 0x01, 0x02, and
 * after a reset RTR reads 0x07D0 and RCR 0x08: that is how the chip file knows
 * it is talking to one, and each of those checks is pinned on its own.
 */

#include <string.h>
#include "unity.h"
#include "net/w5x00.h"

#define MAX_FRAMES 256u

/* Every frame the chip file sent, as it was before the "chip" answered. */
static uint8_t            sent[MAX_FRAMES][4];
static uint8_t            sent_len[MAX_FRAMES];
static unsigned           frames;
static const tfw_pinref_t *bus_cs;      /* what w5x_bus_init was handed */
static unsigned           bus_inits;
static unsigned           cs_mismatch;

/* How the "chip" answers. */
static tfw_status_t bus_status;
static uint8_t      echo[3];             /* shifted out during the header */
static unsigned     mr_busy;             /* MR reads that still show RST */
static uint8_t      mr_low;              /* MR's other bits, on every read */
static uint8_t      rtr_hi;              /* 0x0017 */
static uint8_t      rtr_lo;              /* 0x0018 */
static uint8_t      rcr;                 /* 0x0019 */
static uint8_t      data;                /* any other register read */

tfw_status_t w5x_bus_init(tfw_pinref_t *cs)
{
    bus_inits++;
    bus_cs = cs;
    return bus_status;
}

void w5x_xfer(const tfw_pinref_t *cs, uint8_t *buf, uint8_t len)
{
    uint16_t addr;
    uint8_t  op;

    if (cs != bus_cs) {
        cs_mismatch++;
    }
    TEST_ASSERT_TRUE(frames < MAX_FRAMES);
    sent_len[frames] = len;
    memset(sent[frames], 0, sizeof sent[frames]);
    memcpy(sent[frames], buf, len < 4u ? len : 4u);
    frames++;
    if (len != 4u) {
        return;
    }
    op   = buf[0];
    addr = (uint16_t)((uint16_t)buf[1] << 8 | buf[2]);
    buf[0] = echo[0];
    buf[1] = echo[1];
    buf[2] = echo[2];
    if (op != 0x0Fu) {
        return;                          /* a write, or no opcode at all */
    }
    switch (addr) {
    case 0x0000u:
        buf[3] = (uint8_t)((mr_busy ? 0x80u : 0x00u) | mr_low);
        if (mr_busy) {
            mr_busy--;
        }
        break;
    case 0x0017u: buf[3] = rtr_hi; break;
    case 0x0018u: buf[3] = rtr_lo; break;
    case 0x0019u: buf[3] = rcr;    break;
    default:      buf[3] = data;   break;
    }
}

void setUp(void)
{
    frames      = 0u;
    bus_cs      = NULL;
    bus_inits   = 0u;
    cs_mismatch = 0u;
    bus_status  = TFW_OK;
    echo[0]     = 0x00u;
    echo[1]     = 0x01u;
    echo[2]     = 0x02u;
    mr_busy     = 0u;
    mr_low      = 0x00u;
    rtr_hi      = 0x07u;
    rtr_lo      = 0xD0u;
    rcr         = 0x08u;
    data        = 0x00u;
}

void tearDown(void) {}

static void expect_frame(unsigned i, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    const uint8_t want[4] = { b0, b1, b2, b3 };

    TEST_ASSERT_TRUE(i < frames);
    TEST_ASSERT_EQUAL_UINT8(4u, sent_len[i]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, sent[i], 4);
}

/* The frames of a successful w5x_chip_init, in order. */
static void expect_init_tail(unsigned first)
{
    expect_frame(first,      0x0Fu, 0x00u, 0x18u, 0x00u);   /* RTR low */
    expect_frame(first + 1u, 0x0Fu, 0x00u, 0x19u, 0x00u);   /* RCR */
    expect_frame(first + 2u, 0x0Fu, 0x00u, 0x17u, 0x00u);   /* RTR high */
}

/* ----------------------------------------------------- frames and the map */

static void test_a_common_register_write_is_f0_address_data(void)
{
    w5x_wr(W5X_COMMON, 0x0000u, 0x80u);
    w5x_wr(W5X_COMMON, 0x0019u, 0x5Au);
    TEST_ASSERT_EQUAL_UINT(2u, frames);
    expect_frame(0u, 0xF0u, 0x00u, 0x00u, 0x80u);
    expect_frame(1u, 0xF0u, 0x00u, 0x19u, 0x5Au);
}

/* A read returns the data phase only, never a header byte. */
static void test_a_common_register_read_is_0f_address(void)
{
    data = 0x3Cu;
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, w5x_rd(W5X_COMMON, 0x000Fu));
    TEST_ASSERT_EQUAL_UINT(1u, frames);
    expect_frame(0u, 0x0Fu, 0x00u, 0x0Fu, 0x00u);
}

static void test_socket0_registers_sit_at_0x0400(void)
{
    data = 0x17u;
    w5x_wr(W5X_SOCK0, 0x0001u, 0x20u);
    TEST_ASSERT_EQUAL_HEX8(0x17u, w5x_rd(W5X_SOCK0, 0x0028u));
    TEST_ASSERT_EQUAL_UINT(2u, frames);
    expect_frame(0u, 0xF0u, 0x04u, 0x01u, 0x20u);
    expect_frame(1u, 0x0Fu, 0x04u, 0x28u, 0x00u);
}

/* The TX buffer is 0x4000..0x47FF: the raw pointer is wrapped by its low 11
 * bits, so 0x0800 is the buffer's first byte again and 0x07FF its last. */
static void test_socket0_tx_buffer_wraps_into_0x4000_0x47ff(void)
{
    w5x_wr(W5X_TX0, 0x0000u, 0x01u);
    w5x_wr(W5X_TX0, 0x07FFu, 0x02u);
    w5x_wr(W5X_TX0, 0x0800u, 0x03u);
    w5x_wr(W5X_TX0, 0x1234u, 0x04u);
    w5x_wr(W5X_TX0, 0xFFFFu, 0x05u);
    TEST_ASSERT_EQUAL_UINT(5u, frames);
    expect_frame(0u, 0xF0u, 0x40u, 0x00u, 0x01u);
    expect_frame(1u, 0xF0u, 0x47u, 0xFFu, 0x02u);
    expect_frame(2u, 0xF0u, 0x40u, 0x00u, 0x03u);
    expect_frame(3u, 0xF0u, 0x42u, 0x34u, 0x04u);
    expect_frame(4u, 0xF0u, 0x47u, 0xFFu, 0x05u);
}

static void test_socket0_rx_buffer_wraps_into_0x6000_0x67ff(void)
{
    data = 0xC3u;
    TEST_ASSERT_EQUAL_HEX8(0xC3u, w5x_rd(W5X_RX0, 0x0000u));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, w5x_rd(W5X_RX0, 0x07FFu));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, w5x_rd(W5X_RX0, 0x0800u));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, w5x_rd(W5X_RX0, 0xABCDu));
    TEST_ASSERT_EQUAL_UINT(4u, frames);
    expect_frame(0u, 0x0Fu, 0x60u, 0x00u, 0x00u);
    expect_frame(1u, 0x0Fu, 0x67u, 0xFFu, 0x00u);
    expect_frame(2u, 0x0Fu, 0x60u, 0x00u, 0x00u);
    expect_frame(3u, 0x0Fu, 0x63u, 0xCDu, 0x00u);
}

/* A space w5x00.h does not name is taken as a flat address. */
static void test_an_unnamed_space_is_a_flat_address(void)
{
    w5x_wr(4u, 0x1234u, 0x99u);
    TEST_ASSERT_EQUAL_UINT(1u, frames);
    expect_frame(0u, 0xF0u, 0x12u, 0x34u, 0x99u);
}

static void test_data_bytes_pass_unchanged_both_ways(void)
{
    w5x_wr(W5X_SOCK0, 0x0024u, 0x00u);
    w5x_wr(W5X_SOCK0, 0x0025u, 0xFFu);
    data = 0x00u;
    TEST_ASSERT_EQUAL_HEX8(0x00u, w5x_rd(W5X_SOCK0, 0x0026u));
    data = 0xFFu;
    TEST_ASSERT_EQUAL_HEX8(0xFFu, w5x_rd(W5X_SOCK0, 0x0027u));
    expect_frame(0u, 0xF0u, 0x04u, 0x24u, 0x00u);
    expect_frame(1u, 0xF0u, 0x04u, 0x25u, 0xFFu);
    expect_frame(2u, 0x0Fu, 0x04u, 0x26u, 0x00u);
    expect_frame(3u, 0x0Fu, 0x04u, 0x27u, 0x00u);
}

/* ---------------------------------------------------- w5x_chip_init */

static void test_init_resets_polls_and_reads_the_reset_pattern(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u, bus_inits);
    TEST_ASSERT_NOT_NULL(bus_cs);
    TEST_ASSERT_EQUAL_UINT(0u, cs_mismatch);
    TEST_ASSERT_EQUAL_UINT(5u, frames);
    expect_frame(0u, 0xF0u, 0x00u, 0x00u, 0x80u);   /* MR = RST */
    expect_frame(1u, 0x0Fu, 0x00u, 0x00u, 0x00u);   /* read MR */
    expect_init_tail(2u);
}

static void test_init_refuses_a_bus_it_cannot_have_without_a_frame(void)
{
    bus_status = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(0u, frames);

    setUp();
    bus_status = TFW_ERR_BAD_PIN;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(0u, frames);
}

/* RST self-clears; only the RST bit is looked at. */
static void test_init_polls_until_rst_clears(void)
{
    unsigned i;

    mr_busy = 3u;
    mr_low  = 0x7Fu;
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 4u + 3u, frames);
    for (i = 1u; i <= 4u; i++) {
        expect_frame(i, 0x0Fu, 0x00u, 0x00u, 0x00u);
    }
    expect_init_tail(5u);
}

/* The limit is 200 polls (w5x00.h). Clearing on the 200th is in time. */
static void test_init_accepts_rst_clearing_on_the_last_poll(void)
{
    mr_busy = 199u;
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 200u + 3u, frames);
    expect_init_tail(201u);
}

/* A reset that never ends fails closed after exactly 200 polls, though the
 * reset pattern reads right. The RTR low read is made either way; RCR and RTR
 * high are not. */
static void test_init_gives_up_on_a_reset_that_never_ends(void)
{
    mr_busy = 1000u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 200u + 1u, frames);
    expect_frame(200u, 0x0Fu, 0x00u, 0x00u, 0x00u);
    expect_frame(201u, 0x0Fu, 0x00u, 0x18u, 0x00u);
}

/* Each header echo byte is checked on its own: a W5500 or an empty socket
 * shifts out something else, and then 0x0017 -- a write in W5500 framing --
 * must not be sent. */
static void test_init_refuses_a_wrong_header_echo_byte_by_byte(void)
{
    unsigned pos;

    for (pos = 0u; pos < 3u; pos++) {
        setUp();
        echo[pos] ^= 0x10u;
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
        TEST_ASSERT_EQUAL_UINT(3u, frames);
    }
    setUp();
    echo[0] = 0xFFu;
    echo[1] = 0xFFu;
    echo[2] = 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(3u, frames);
}

static void test_init_refuses_a_wrong_rtr_low(void)
{
    rtr_lo = 0xD1u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(3u, frames);
}

static void test_init_refuses_a_wrong_rcr_before_reading_rtr_high(void)
{
    rcr = 0x07u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(4u, frames);
    expect_frame(3u, 0x0Fu, 0x00u, 0x19u, 0x00u);
}

static void test_init_refuses_a_wrong_rtr_high(void)
{
    rtr_hi = 0x08u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(5u, frames);
    expect_init_tail(2u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_common_register_write_is_f0_address_data);
    RUN_TEST(test_a_common_register_read_is_0f_address);
    RUN_TEST(test_socket0_registers_sit_at_0x0400);
    RUN_TEST(test_socket0_tx_buffer_wraps_into_0x4000_0x47ff);
    RUN_TEST(test_socket0_rx_buffer_wraps_into_0x6000_0x67ff);
    RUN_TEST(test_an_unnamed_space_is_a_flat_address);
    RUN_TEST(test_data_bytes_pass_unchanged_both_ways);
    RUN_TEST(test_init_resets_polls_and_reads_the_reset_pattern);
    RUN_TEST(test_init_refuses_a_bus_it_cannot_have_without_a_frame);
    RUN_TEST(test_init_polls_until_rst_clears);
    RUN_TEST(test_init_accepts_rst_clearing_on_the_last_poll);
    RUN_TEST(test_init_gives_up_on_a_reset_that_never_ends);
    RUN_TEST(test_init_refuses_a_wrong_header_echo_byte_by_byte);
    RUN_TEST(test_init_refuses_a_wrong_rtr_low);
    RUN_TEST(test_init_refuses_a_wrong_rcr_before_reading_rtr_high);
    RUN_TEST(test_init_refuses_a_wrong_rtr_high);
    return UNITY_END();
}
