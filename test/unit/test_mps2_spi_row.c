/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SPI=1 row of the MPS2 boards (row cortex-m, #161): mps2-an505's own
 * profile and board table, compiled with TFW_CM_SPI as profile.mk's SPI=1
 * compiles them, and SPI_CONFIG and SPI_XFER from src/caps/bus.c through
 * src/hal/cortex-m/mps2/hal_bus.c to a PL022 modelled in place of pl022.c.
 *
 * The model is a shield device behind the PL022: while MISC bit 8, the
 * select, is low it logs each byte the master sends and answers from its own
 * reply; with the select high it answers 0xFF and logs nothing, as an
 * unselected device leaves MISO pulled up. test/unit/test_mps2_bus.c checks
 * the master's register writes; this test checks the row: the feature bit
 * CAPS reports, the select the board table names, and a transfer from the
 * command handler to the bus.
 */

#include <string.h>
#include "unity.h"
#include "bus.h"
#include "reader.h"
#include "writer.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "board_profile.h"
#include "pl022.h"
#include "sbcon.h"

static struct {
    uint32_t       cr0;
    uint32_t       cr1;
    uint32_t       cpsr;
    uint32_t       misc;
    uint8_t        rx[8];        /* the receive FIFO */
    uint8_t        rxn;
    const uint8_t *reply;        /* what the selected device shifts out */
    uint8_t        replylen;
    uint8_t        seen[16];     /* what it shifted in */
    uint8_t        seenlen;
    unsigned       selects;      /* falling edges of the select */
} ssp;

/* ---- the PL022 and MISC, in place of pl022.c */

uint32_t tfw_mps2_ssp_read(uint32_t off)
{
    uint32_t v;

    switch (off) {
    case TFW_SSP_SR:
        return TFW_SSP_SR_TFE | TFW_SSP_SR_TNF | (ssp.rxn ? TFW_SSP_SR_RNE : 0u);
    case TFW_SSP_DR:
        if (ssp.rxn == 0u) {
            return 0u;
        }
        v = ssp.rx[0];
        ssp.rxn--;
        memmove(ssp.rx, &ssp.rx[1], ssp.rxn);
        return v;
    case TFW_SSP_CR0:
        return ssp.cr0;
    case TFW_SSP_CR1:
        return ssp.cr1;
    default:
        return ssp.cpsr;
    }
}

void tfw_mps2_ssp_write(uint32_t off, uint32_t v)
{
    uint8_t out = 0xFFu;

    switch (off) {
    case TFW_SSP_DR:
        /* A stopped port shifts nothing. */
        if ((ssp.cr1 & TFW_SSP_CR1_SSE) == 0u || ssp.rxn == sizeof ssp.rx) {
            return;
        }
        if ((ssp.misc & TFW_MPS2_MISC_SHIELD0_NCS) == 0u) {
            if (ssp.seenlen < sizeof ssp.seen) {
                ssp.seen[ssp.seenlen] = (uint8_t)v;
            }
            out = (ssp.seenlen < ssp.replylen) ? ssp.reply[ssp.seenlen] : 0x00u;
            ssp.seenlen++;
        }
        ssp.rx[ssp.rxn++] = out;
        return;
    case TFW_SSP_CR0:
        ssp.cr0 = v;
        return;
    case TFW_SSP_CR1:
        ssp.cr1 = v;
        return;
    default:
        ssp.cpsr = v;
        return;
    }
}

uint32_t tfw_mps2_misc_read(void)
{
    return ssp.misc;
}

void tfw_mps2_misc_write(uint32_t v)
{
    if ((ssp.misc & TFW_MPS2_MISC_SHIELD0_NCS) != 0u
        && (v & TFW_MPS2_MISC_SHIELD0_NCS) == 0u) {
        ssp.selects++;
    }
    ssp.misc = v;
}

/* ---- the rest of what hal_bus.c and bus.c reach: an idle SBCon with both
 * lines released, no delay, and the soft tier. */

uint32_t tfw_mps2_sb_lines(void)
{
    return TFW_SB_SCL | TFW_SB_SDA;
}

void tfw_mps2_sb_release(uint32_t mask)
{
    (void)mask;
}

void tfw_mps2_sb_drive_low(uint32_t mask)
{
    (void)mask;
}

void tfw_hal_delay_us(uint16_t us)
{
    (void)us;
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return TFW_CRYPTO_SOFTWARE;
}

/* ---- the commands, as dispatch hands them a request */

static uint8_t      s_reply[TFW_MAX_PAYLOAD];
static tfw_writer_t s_w;

static tfw_status_t run(tfw_status_t (*cmd)(tfw_reader_t *, tfw_writer_t *),
                        const uint8_t *req, uint8_t len)
{
    tfw_reader_t r;

    tfw_reader_init(&r, req, len);
    tfw_writer_init(&s_w, s_reply, sizeof s_reply);
    return cmd(&r, &s_w);
}

void setUp(void)
{
    memset(&ssp, 0, sizeof ssp);
    /* MISC resets with the select high: no device selected. */
    ssp.misc = TFW_MPS2_MISC_SHIELD0_NCS;
}

void tearDown(void) {}

static void test_the_row_reports_spi_and_no_other_bus(void)
{
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_SPI);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_I2C);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_ONEWIRE);
    /* CAPS bits 7, 8 and 9 (I2C, SPI, 1-Wire): SPI alone. */
    TEST_ASSERT_EQUAL_HEX32(0x00000100uL, TFW_FEATURE_BITMAP & 0x00000380uL);
}

static void test_the_select_is_pin_2_misc_bit_8(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(2u, &r));
    TEST_ASSERT_EQUAL_UINT8(TFW_MPS2_PORT_MISC, r.port);
    TEST_ASSERT_EQUAL_UINT8(TFW_MPS2_SPI_CS_BIT, r.bit);
    TEST_ASSERT_EQUAL_HEX8(0x00u, tfw_board_pin_flags(2u) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE));
}

/* SPI_CONFIG mode 0, SPR 0, MSB first, then SPI_XFER on pin 2: a JEDEC ID read,
 * the select low across all four frames and released after them. */
static void test_spi_xfer_on_pin_2_reaches_the_device(void)
{
    static const uint8_t cfg[]   = { 0x00u, 0x00u, 0x00u };
    static const uint8_t xfer[]  = { 0x02u, 0x9Fu, 0x00u, 0x00u, 0x00u };
    static const uint8_t jedec[] = { 0xFFu, 0xEFu, 0x40u, 0x18u };
    static const uint8_t sent[]  = { 0x9Fu, 0x00u, 0x00u, 0x00u };

    ssp.reply    = jedec;
    ssp.replylen = sizeof jedec;

    TEST_ASSERT_EQUAL(TFW_OK, run(tfw_spi_cmd_config, cfg, sizeof cfg));
    TEST_ASSERT_EQUAL_HEX32(TFW_SSP_CR1_SSE, ssp.cr1);
    TEST_ASSERT_EQUAL(TFW_OK, run(tfw_spi_cmd_xfer, xfer, sizeof xfer));

    TEST_ASSERT_EQUAL_UINT(1u, ssp.selects);
    TEST_ASSERT_EQUAL_UINT8(sizeof sent, ssp.seenlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sent, ssp.seen, sizeof sent);
    TEST_ASSERT_EQUAL_UINT8(sizeof jedec, tfw_writer_len(&s_w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(jedec, s_reply, sizeof jedec);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MISC_SHIELD0_NCS, ssp.misc & TFW_MPS2_MISC_SHIELD0_NCS);
}

/* An LED as the select: in the table, but not a route to the shield, so the
 * master refuses it before a frame is clocked. */
static void test_an_led_as_the_select_is_refused(void)
{
    static const uint8_t cfg[]  = { 0x00u, 0x00u, 0x00u };
    static const uint8_t xfer[] = { 0x00u, 0x9Fu };

    TEST_ASSERT_EQUAL(TFW_OK, run(tfw_spi_cmd_config, cfg, sizeof cfg));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(tfw_spi_cmd_xfer, xfer, sizeof xfer));
    TEST_ASSERT_EQUAL_UINT(0u, ssp.selects);
    TEST_ASSERT_EQUAL_UINT8(0u, ssp.seenlen);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_row_reports_spi_and_no_other_bus);
    RUN_TEST(test_the_select_is_pin_2_misc_bit_8);
    RUN_TEST(test_spi_xfer_on_pin_2_reaches_the_device);
    RUN_TEST(test_an_led_as_the_select_is_refused);
    return UNITY_END();
}
