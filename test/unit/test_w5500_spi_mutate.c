/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/net/w5500.c on its own: the W5500's SPI frame, byte for byte.
 *
 * test_w5500 drives the chip file through w5x00.c against the software W5500,
 * which decodes the frames with the same reading of the datasheet the chip
 * file was written from, so a frame both get wrong in the same way passes. Here
 * the suite stands in for w5x00.c's bus -- w5x_bus_init and w5x_xfer -- and
 * every frame the chip file sends is compared with a literal from the W5500
 * datasheet's SPI frame (offset, control and data phases): the 16-bit
 * offset high byte first, then the control byte
 *
 *   bits 7..3  BSB  block select: 00000 common, 00001 socket 0 registers,
 *                   00010 socket 0 TX buffer, 00011 socket 0 RX buffer
 *   bit  2     RWB  1 write, 0 read
 *   bits 1..0  OM   00 variable-length data mode, the length set by CS
 *
 * then one data byte, all in one four-byte transaction. A read returns what
 * the chip shifts out in the data phase; the header phase is filled with a
 * pattern the chip file must not read. Chip identification is pinned too:
 * the MR reset write, the poll on MR's RST bit, VERSIONR (0x0039) reading 0x04.
 */

#include <string.h>
#include "unity.h"
#include "net/w5x00.h"

#define MAX_FRAMES 256u

/* Every frame the chip file sent, as it was before the "chip" answered. */
static uint8_t           sent[MAX_FRAMES][4];
static uint8_t           sent_len[MAX_FRAMES];
static unsigned          frames;
static const tfw_pinref_t *bus_cs;      /* what w5x_bus_init was handed */
static unsigned          bus_inits;
static unsigned          cs_mismatch;

/* How the "chip" answers. */
static tfw_status_t bus_status;
static unsigned     mr_busy;             /* MR reads that still show RST */
static uint8_t      mr_low;              /* MR's other bits, on every read */
static uint8_t      version;             /* VERSIONR */
static uint8_t      data;                /* any other register read */

tfw_status_t w5x_bus_init(tfw_pinref_t *cs)
{
    bus_inits++;
    bus_cs = cs;
    return bus_status;
}

void w5x_xfer(const tfw_pinref_t *cs, uint8_t *buf, uint8_t len)
{
    uint16_t off;

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
    off = (uint16_t)((uint16_t)buf[0] << 8 | buf[1]);
    /* The header phase: the W5500 shifts out junk the driver must ignore. */
    buf[0] = 0xA5u;
    buf[1] = 0x5Au;
    buf[2] = 0xC3u;
    if (sent[frames - 1u][2] & 0x04u) {
        return;                          /* a write: MOSI only */
    }
    if (sent[frames - 1u][2] == 0x00u && off == 0x0000u) {
        buf[3] = (uint8_t)((mr_busy ? 0x80u : 0x00u) | mr_low);
        if (mr_busy) {
            mr_busy--;
        }
    } else if (sent[frames - 1u][2] == 0x00u && off == 0x0039u) {
        buf[3] = version;
    } else {
        buf[3] = data;
    }
}

void setUp(void)
{
    frames      = 0u;
    bus_cs      = NULL;
    bus_inits   = 0u;
    cs_mismatch = 0u;
    bus_status  = TFW_OK;
    mr_busy     = 0u;
    mr_low      = 0x00u;
    version     = 0x04u;
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

/* ------------------------------------------------------------- the frames */

static void test_a_common_register_write_is_offset_bsb_00000_rwb_1_then_data(void)
{
    w5x_wr(W5X_COMMON, 0x0000u, 0x80u);
    TEST_ASSERT_EQUAL_UINT(1u, frames);
    expect_frame(0u, 0x00u, 0x00u, 0x04u, 0x80u);
}

static void test_a_common_register_read_is_offset_bsb_00000_rwb_0(void)
{
    data = 0x3Cu;
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, w5x_rd(W5X_COMMON, 0x000Fu));
    TEST_ASSERT_EQUAL_UINT(1u, frames);
    expect_frame(0u, 0x00u, 0x0Fu, 0x00u, 0x00u);
}

static void test_socket0_registers_are_bsb_00001(void)
{
    data = 0x17u;
    w5x_wr(W5X_SOCK0, 0x0001u, 0x20u);
    TEST_ASSERT_EQUAL_HEX8(0x17u, w5x_rd(W5X_SOCK0, 0x0003u));
    TEST_ASSERT_EQUAL_UINT(2u, frames);
    expect_frame(0u, 0x00u, 0x01u, 0x0Cu, 0x20u);
    expect_frame(1u, 0x00u, 0x03u, 0x08u, 0x00u);
}

/* The buffer spaces take the raw 16-bit pointer: the W5500 wraps it into the
 * buffer itself, so the chip file sends it as it came, high byte first. */
static void test_socket0_tx_buffer_is_bsb_00010_with_the_pointer_unwrapped(void)
{
    data = 0x99u;
    w5x_wr(W5X_TX0, 0xABCDu, 0x5Au);
    TEST_ASSERT_EQUAL_HEX8(0x99u, w5x_rd(W5X_TX0, 0x1234u));
    TEST_ASSERT_EQUAL_UINT(2u, frames);
    expect_frame(0u, 0xABu, 0xCDu, 0x14u, 0x5Au);
    expect_frame(1u, 0x12u, 0x34u, 0x10u, 0x00u);
}

static void test_socket0_rx_buffer_is_bsb_00011_with_the_pointer_unwrapped(void)
{
    data = 0xC3u;
    TEST_ASSERT_EQUAL_HEX8(0xC3u, w5x_rd(W5X_RX0, 0xFFFFu));
    w5x_wr(W5X_RX0, 0x0800u, 0x01u);
    TEST_ASSERT_EQUAL_UINT(2u, frames);
    expect_frame(0u, 0xFFu, 0xFFu, 0x18u, 0x00u);
    expect_frame(1u, 0x08u, 0x00u, 0x1Cu, 0x01u);
}

/* A write sends its data byte whatever it is, 0x00 and 0xFF included, and a
 * read returns the data phase only, never a header byte. */
static void test_data_bytes_pass_unchanged_both_ways(void)
{
    w5x_wr(W5X_SOCK0, 0x0024u, 0x00u);
    w5x_wr(W5X_SOCK0, 0x0025u, 0xFFu);
    data = 0x00u;
    TEST_ASSERT_EQUAL_HEX8(0x00u, w5x_rd(W5X_SOCK0, 0x0026u));
    data = 0xFFu;
    TEST_ASSERT_EQUAL_HEX8(0xFFu, w5x_rd(W5X_SOCK0, 0x0027u));
    expect_frame(0u, 0x00u, 0x24u, 0x0Cu, 0x00u);
    expect_frame(1u, 0x00u, 0x25u, 0x0Cu, 0xFFu);
    expect_frame(2u, 0x00u, 0x26u, 0x08u, 0x00u);
    expect_frame(3u, 0x00u, 0x27u, 0x08u, 0x00u);
}

/* ---------------------------------------------------- w5x_chip_init */

static void test_init_resets_polls_mr_once_and_reads_versionr(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u, bus_inits);
    TEST_ASSERT_NOT_NULL(bus_cs);
    TEST_ASSERT_EQUAL_UINT(0u, cs_mismatch);
    TEST_ASSERT_EQUAL_UINT(3u, frames);
    expect_frame(0u, 0x00u, 0x00u, 0x04u, 0x80u);   /* MR = RST */
    expect_frame(1u, 0x00u, 0x00u, 0x00u, 0x00u);   /* read MR */
    expect_frame(2u, 0x00u, 0x39u, 0x00u, 0x00u);   /* read VERSIONR */
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

/* RST self-clears; the chip file polls until it does. Only the RST bit is
 * looked at: the other MR bits may read anything. */
static void test_init_polls_until_rst_clears(void)
{
    unsigned i;

    mr_busy = 3u;
    mr_low  = 0x7Fu;
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 4u + 1u, frames);
    expect_frame(0u, 0x00u, 0x00u, 0x04u, 0x80u);
    for (i = 1u; i <= 4u; i++) {
        expect_frame(i, 0x00u, 0x00u, 0x00u, 0x00u);
    }
    expect_frame(5u, 0x00u, 0x39u, 0x00u, 0x00u);
}

/* The limit is 200 polls (w5x00.h). Clearing on the 200th is in time. */
static void test_init_accepts_rst_clearing_on_the_last_poll(void)
{
    mr_busy = 199u;
    TEST_ASSERT_EQUAL(TFW_OK, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 200u + 1u, frames);
    expect_frame(200u, 0x00u, 0x00u, 0x00u, 0x00u);
    expect_frame(201u, 0x00u, 0x39u, 0x00u, 0x00u);
}

/* A reset that never ends fails closed after exactly 200 polls, and VERSIONR
 * -- which reads right here -- is not asked. */
static void test_init_gives_up_on_a_reset_that_never_ends(void)
{
    mr_busy = 1000u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
    TEST_ASSERT_EQUAL_UINT(1u + 200u, frames);
    expect_frame(200u, 0x00u, 0x00u, 0x00u, 0x00u);
}

/* VERSIONR is 0x04 on a W5500 and nothing else is one. */
static void test_init_refuses_any_other_version(void)
{
    static const uint8_t wrong[] = { 0x00u, 0x03u, 0x05u, 0x14u, 0x84u, 0xFFu };
    unsigned i;

    for (i = 0u; i < sizeof wrong; i++) {
        setUp();
        version = wrong[i];
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, w5x_chip_init());
        TEST_ASSERT_EQUAL_UINT(3u, frames);
        expect_frame(2u, 0x00u, 0x39u, 0x00u, 0x00u);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_common_register_write_is_offset_bsb_00000_rwb_1_then_data);
    RUN_TEST(test_a_common_register_read_is_offset_bsb_00000_rwb_0);
    RUN_TEST(test_socket0_registers_are_bsb_00001);
    RUN_TEST(test_socket0_tx_buffer_is_bsb_00010_with_the_pointer_unwrapped);
    RUN_TEST(test_socket0_rx_buffer_is_bsb_00011_with_the_pointer_unwrapped);
    RUN_TEST(test_data_bytes_pass_unchanged_both_ways);
    RUN_TEST(test_init_resets_polls_mr_once_and_reads_versionr);
    RUN_TEST(test_init_refuses_a_bus_it_cannot_have_without_a_frame);
    RUN_TEST(test_init_polls_until_rst_clears);
    RUN_TEST(test_init_accepts_rst_clearing_on_the_last_poll);
    RUN_TEST(test_init_gives_up_on_a_reset_that_never_ends);
    RUN_TEST(test_init_refuses_any_other_version);
    return UNITY_END();
}
