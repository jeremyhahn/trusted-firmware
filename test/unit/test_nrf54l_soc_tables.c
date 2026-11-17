/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's data (src/hal/cortex-m/nrf54l/soc_tables.c), on x86.
 *
 * Every expected value is a literal copied from the vendor's header (the nrfx
 * MDK's nrf54l15_types.h, nrf54l15_application_peripherals.h and
 * nrf54l_erratas.h), not computed: a wrong baud register on a board nobody is
 * watching is a console that prints noise, and a test that derived its
 * expectations from a formula would agree with the same mistake.
 */

#include "unity.h"
#include "tfw/status.h"
#include "soc_tables.h"

void setUp(void) {}
void tearDown(void) {}

static void test_every_listed_rate_has_the_vendors_register_value(void)
{
    static const struct { uint32_t baud, reg; } want[] = {
        {    1200u, 0x0004F000u }, {    2400u, 0x0009D000u },
        {    4800u, 0x0013B000u }, {    9600u, 0x00275000u },
        {   14400u, 0x003AF000u }, {   19200u, 0x004EA000u },
        {   28800u, 0x0075C000u }, {   31250u, 0x00800000u },
        {   38400u, 0x009D0000u }, {   56000u, 0x00E50000u },
        {   57600u, 0x00EB0000u }, {   76800u, 0x013A9000u },
        {  115200u, 0x01D60000u }, {  230400u, 0x03B00000u },
        {  250000u, 0x04000000u }, {  460800u, 0x07400000u },
        {  921600u, 0x0F000000u }, { 1000000u, 0x10000000u },
    };

    for (unsigned i = 0u; i < sizeof want / sizeof want[0]; i++) {
        uint32_t reg = 0u;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_nrf54l_uarte_baud(want[i].baud, &reg));
        TEST_ASSERT_EQUAL_HEX32(want[i].reg, reg);
    }
}

static void test_an_unlisted_rate_is_out_of_range_and_writes_nothing(void)
{
    static const uint32_t bad[] = { 0u, 1u, 300u, 115201u, 2000000u, 0xFFFFFFFFu };

    for (unsigned i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        uint32_t reg = 0xA5A5A5A5u;

        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_nrf54l_uarte_baud(bad[i], &reg));
        TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);
    }
}

static void test_the_pins_are_p0_0_6_p1_0_16_and_p2_0_10(void)
{
    static const uint8_t count[3] = { 7u, 17u, 11u };

    for (uint8_t port = 0u; port < 3u; port++) {
        for (uint8_t bit = 0u; bit < 32u; bit++) {
            TEST_ASSERT_EQUAL_UINT8(bit < count[port] ? 1u : 0u,
                                    tfw_nrf54l_pin_exists(port, bit) ? 1u : 0u);
        }
    }
    TEST_ASSERT_FALSE(tfw_nrf54l_pin_exists(3u, 0u));
    TEST_ASSERT_FALSE(tfw_nrf54l_pin_exists(255u, 0u));
}

static void test_psel_puts_the_port_in_bits_5_to_7_and_connects(void)
{
    /* The DK's console: P1.4 and P1.5. */
    TEST_ASSERT_EQUAL_HEX32(0x00000024u, tfw_nrf54l_psel(1u, 4u));
    TEST_ASSERT_EQUAL_HEX32(0x00000025u, tfw_nrf54l_psel(1u, 5u));
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, tfw_nrf54l_psel(0u, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x0000004Au, tfw_nrf54l_psel(2u, 10u));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_nrf54l_psel(2u, 10u) & 0x80000000u);
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, TFW_NRF54L_PSEL_DISCONNECTED & 0x80000000u);
}

static void test_only_the_first_revision_takes_the_errata_workarounds(void)
{
    TEST_ASSERT_TRUE(tfw_nrf54l_is_rev1(0x1Cu, 0x01u));
    TEST_ASSERT_FALSE(tfw_nrf54l_is_rev1(0x1Cu, 0x02u));
    TEST_ASSERT_FALSE(tfw_nrf54l_is_rev1(0x1Cu, 0xFFFFFFFFu));
    TEST_ASSERT_FALSE(tfw_nrf54l_is_rev1(0x2Eu, 0x01u));   /* the nRF54L10 */
    TEST_ASSERT_FALSE(tfw_nrf54l_is_rev1(0x2Fu, 0x01u));   /* the nRF54L05 */
    TEST_ASSERT_FALSE(tfw_nrf54l_is_rev1(0xFFFFFFFFu, 0x01u));
}

/* tfw-boot's 32 KiB region ends where the floor is; the RRAM ends at
 * 0x17D000 (Zephyr's cpuapp_rram, 1524 KiB). */
#define FLOOR 0x00008000uL

static void test_the_rram_is_1524_kib(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0017D000u, TFW_NRF54L_RRAM_SIZE);
    TEST_ASSERT_EQUAL_UINT32(1524u * 1024u, TFW_NRF54L_RRAM_SIZE);
}

static void test_a_span_in_the_rram_above_the_floor_is_writable(void)
{
    TEST_ASSERT_TRUE(tfw_nrf54l_rram_span(FLOOR, FLOOR, 1u));           /* the application's first byte */
    TEST_ASSERT_TRUE(tfw_nrf54l_rram_span(FLOOR, 0x0017C000u, 0x1000u)); /* the whole platform block */
    TEST_ASSERT_TRUE(tfw_nrf54l_rram_span(FLOOR, 0x0017CFFFu, 1u));     /* the RRAM's last byte */
    TEST_ASSERT_TRUE(tfw_nrf54l_rram_span(FLOOR, FLOOR, 0x0017D000u - FLOOR));
    TEST_ASSERT_TRUE(tfw_nrf54l_rram_span(FLOOR, 0x0017D000u, 0u));     /* empty, at the end */
}

static void test_a_span_into_tfw_boots_region_is_refused(void)
{
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, FLOOR - 1u, 1u));
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0u, 4u));
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0u, 0u));
}

static void test_a_span_past_the_rram_or_wrapping_is_refused(void)
{
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0x0017CFFFu, 2u));
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0x0017D000u, 1u));
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0x0017D001u, 0u));
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0x20000000u, 4u));    /* RAM is not RRAM */
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, FLOOR, 0xFFFFFFFFu)); /* addr + len wraps */
    TEST_ASSERT_FALSE(tfw_nrf54l_rram_span(FLOOR, 0xFFFFFFFCu, 8u));
}

static void test_aligned_whole_words_go_as_words_the_rest_as_bytes(void)
{
    TEST_ASSERT_EQUAL_UINT32(4u, tfw_nrf54l_rram_unit(0x0017C000u, 4u));
    TEST_ASSERT_EQUAL_UINT32(4u, tfw_nrf54l_rram_unit(0x0017C004u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nrf54l_rram_unit(0x0017C000u, 3u));  /* a tail */
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nrf54l_rram_unit(0x0017C001u, 8u));  /* unaligned */
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nrf54l_rram_unit(0x0017C002u, 8u));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nrf54l_rram_unit(0x0017C003u, 8u));
}

/* The walk boot_rram.c makes: every byte stored once, in order, and every
 * word store aligned. A 7-byte write at ...01 is 3 bytes, a word, nothing
 * left; the platform block's 1-byte confirm is one byte. */
static void test_the_walk_covers_each_byte_once_with_aligned_words(void)
{
    static const struct { uint32_t addr, len, words, bytes; } cases[] = {
        { 0x0017C001u, 7u,      1u,    3u },
        { 0x0017C000u, 1u,      0u,    1u },
        { 0x0017C000u, 0x1000u, 0x400u, 0u },
        { 0x0017C002u, 9u,      1u,    5u },
        { 0x00008000u, 0u,      0u,    0u },
    };

    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        uint32_t addr = cases[c].addr, left = cases[c].len;
        uint32_t words = 0u, bytes = 0u;

        while (left != 0u) {
            uint32_t unit = tfw_nrf54l_rram_unit(addr, left);

            TEST_ASSERT_TRUE(unit == 1u || unit == 4u);
            TEST_ASSERT_TRUE(unit <= left);
            if (unit == 4u) {
                TEST_ASSERT_EQUAL_UINT32(0u, addr & 3u);
                words++;
            } else {
                bytes++;
            }
            addr += unit;
            left -= unit;
        }
        TEST_ASSERT_EQUAL_UINT32(cases[c].addr + cases[c].len, addr);
        TEST_ASSERT_EQUAL_UINT32(cases[c].words, words);
        TEST_ASSERT_EQUAL_UINT32(cases[c].bytes, bytes);
    }
}

/* nrfx soc/nrfx_soc_defines.h, SAADC_EXTERNAL_AIN_PSELS for the nRF54L15:
 * AIN2-AIN5 are P1.6, P1.7, P1.11 and P1.12. */
static void test_the_saadc_channels_are_ain2_to_ain5_on_p1(void)
{
    static const uint8_t want[4] = { 6u, 7u, 11u, 12u };
    uint8_t ch;

    TEST_ASSERT_EQUAL_UINT32(4u, TFW_NRF54L_SAADC_CHANNELS);
    for (ch = 0u; ch < 4u; ch++) {
        uint8_t port = 0xEEu, bit = 0xEEu;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_nrf54l_saadc_pin(ch, &port, &bit));
        TEST_ASSERT_EQUAL_UINT8(1u, port);
        TEST_ASSERT_EQUAL_UINT8(want[ch], bit);
        TEST_ASSERT_TRUE(tfw_nrf54l_pin_exists(port, bit));
    }
}

static void test_a_channel_past_the_table_is_refused_and_writes_nothing(void)
{
    uint8_t port = 0xEEu, bit = 0xEEu;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_nrf54l_saadc_pin(4u, &port, &bit));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_nrf54l_saadc_pin(0xFFu, &port, &bit));
    TEST_ASSERT_EQUAL_UINT8(0xEEu, port);
    TEST_ASSERT_EQUAL_UINT8(0xEEu, bit);
}

/* nrf54l15_types.h: SAADC_CH_PSELP_PIN_Pos 0, PORT_Pos 8, CONNECT_Pos 30,
 * CONNECT_AnalogInput 1. */
static void test_saadc_pselp_puts_the_port_in_bits_8_to_11_and_connects_analog(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x40000106u, tfw_nrf54l_saadc_pselp(1u, 6u));
    TEST_ASSERT_EQUAL_HEX32(0x40000107u, tfw_nrf54l_saadc_pselp(1u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0x4000010Bu, tfw_nrf54l_saadc_pselp(1u, 11u));
    TEST_ASSERT_EQUAL_HEX32(0x4000010Cu, tfw_nrf54l_saadc_pselp(1u, 12u));
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, tfw_nrf54l_saadc_pselp(0u, 0u));
}

static void test_a_sample_is_clamped_to_twelve_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_nrf54l_saadc_raw(-3));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_nrf54l_saadc_raw(0));
    TEST_ASSERT_EQUAL_UINT16(2048u, tfw_nrf54l_saadc_raw(2048));
    TEST_ASSERT_EQUAL_UINT16(4095u, tfw_nrf54l_saadc_raw(4095));
    TEST_ASSERT_EQUAL_UINT16(4095u, tfw_nrf54l_saadc_raw(4100));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_listed_rate_has_the_vendors_register_value);
    RUN_TEST(test_an_unlisted_rate_is_out_of_range_and_writes_nothing);
    RUN_TEST(test_the_pins_are_p0_0_6_p1_0_16_and_p2_0_10);
    RUN_TEST(test_psel_puts_the_port_in_bits_5_to_7_and_connects);
    RUN_TEST(test_only_the_first_revision_takes_the_errata_workarounds);
    RUN_TEST(test_the_rram_is_1524_kib);
    RUN_TEST(test_a_span_in_the_rram_above_the_floor_is_writable);
    RUN_TEST(test_a_span_into_tfw_boots_region_is_refused);
    RUN_TEST(test_a_span_past_the_rram_or_wrapping_is_refused);
    RUN_TEST(test_aligned_whole_words_go_as_words_the_rest_as_bytes);
    RUN_TEST(test_the_walk_covers_each_byte_once_with_aligned_words);
    RUN_TEST(test_the_saadc_channels_are_ain2_to_ain5_on_p1);
    RUN_TEST(test_a_channel_past_the_table_is_refused_and_writes_nothing);
    RUN_TEST(test_saadc_pselp_puts_the_port_in_bits_8_to_11_and_connects_analog);
    RUN_TEST(test_a_sample_is_clamped_to_twelve_bits);
    return UNITY_END();
}
