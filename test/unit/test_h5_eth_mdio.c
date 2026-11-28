/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 Ethernet PHY probe's decode (#579): PHYID1/PHYID2 into OUI,
 * model and revision, BMSR's latched-low link bit, the 0-31 scan and the boot
 * line, over a fake MDIO bus of 32 clause 22 register blocks.
 */
#include <string.h>

#include "unity.h"
#include "hal_eth_mdio.h"

static uint16_t bus[32][32];     /* [addr][reg]; 0xFFFF: nothing answers */
static int      link_latched_low[32];
static int      link_now[32];
static int      reads, fail_at_addr = -1;
static int      fail_reg = -1;     /* with fail_at_addr: fail only this register (-1: all) */
static int      fail_read_n = -1;  /* fail only the Nth read, counted from 1 (-1: none) */

static int fake_read(uint8_t addr, uint8_t reg, uint16_t *val)
{
    reads++;
    if (reads == fail_read_n) {
        return -1;
    }
    if ((int)addr == fail_at_addr && (fail_reg < 0 || (int)reg == fail_reg)) {
        return -1;
    }
    if (reg == TFW_PHY_REG_BMSR && bus[addr][reg] != 0xFFFFu) {
        uint16_t v = (uint16_t)(bus[addr][reg] & ~TFW_PHY_BMSR_LINK);
        if (link_now[addr] && !link_latched_low[addr]) {
            v |= TFW_PHY_BMSR_LINK;
        }
        link_latched_low[addr] = 0;     /* the read clears the latch */
        *val = v;
        return 0;
    }
    *val = bus[addr][reg];
    return 0;
}

static void put_phy(uint8_t a, uint16_t id1, uint16_t id2, int up, int latched)
{
    bus[a][TFW_PHY_REG_PHYID1] = id1;
    bus[a][TFW_PHY_REG_PHYID2] = id2;
    bus[a][TFW_PHY_REG_BMSR] = 0x7809u;
    link_now[a] = up;
    link_latched_low[a] = latched;
}

void setUp(void)
{
    memset(bus, 0xFF, sizeof bus);
    memset(link_latched_low, 0, sizeof link_latched_low);
    memset(link_now, 0, sizeof link_now);
    reads = 0;
    fail_at_addr = -1;
    fail_reg = -1;
    fail_read_n = -1;
}

void tearDown(void) { }

static void test_lan8742a_id_decodes(void)
{
    /* LAN8742A: PHYID1 0x0007, PHYID2 0xC131 (model 0x13, revision 1). */
    TEST_ASSERT_EQUAL_HEX32(0x1F0u, tfw_phy_oui(0x0007u, 0xC131u));
    TEST_ASSERT_EQUAL_HEX8(0x13u, tfw_phy_model(0xC131u));
    TEST_ASSERT_EQUAL_HEX8(0x1u, tfw_phy_revision(0xC131u));
    /* every OUI bit reaches its place: PHYID1 high, PHYID2[15:10] low */
    TEST_ASSERT_EQUAL_HEX32(0x3FFFC0u, tfw_phy_oui(0xFFFFu, 0x0000u));
    TEST_ASSERT_EQUAL_HEX32(0x3Fu, tfw_phy_oui(0x0000u, 0xFC00u));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, tfw_phy_model(0x03F0u));
    TEST_ASSERT_EQUAL_HEX8(0x0u, tfw_phy_model(0xFC0Fu));
    TEST_ASSERT_EQUAL_HEX8(0xFu, tfw_phy_revision(0xFFFFu));
}

static void test_idle_bus_ids_are_absent(void)
{
    TEST_ASSERT_FALSE(tfw_phy_id_present(0xFFFFu, 0xFFFFu));
    TEST_ASSERT_FALSE(tfw_phy_id_present(0x0000u, 0x0000u));
    TEST_ASSERT_TRUE(tfw_phy_id_present(0x0007u, 0xC131u));
    TEST_ASSERT_TRUE(tfw_phy_id_present(0xFFFFu, 0x0000u));
}

static void test_bmsr_link_bit(void)
{
    TEST_ASSERT_TRUE(tfw_phy_link_up(0x0004u));
    TEST_ASSERT_TRUE(tfw_phy_link_up(0x782Du));
    TEST_ASSERT_FALSE(tfw_phy_link_up(0x7809u));
    TEST_ASSERT_FALSE(tfw_phy_link_up(0xFFFBu));
}

static void test_scan_finds_the_first_phy_and_reads_bmsr_twice(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    put_phy(5u, 0x0007u, 0xC131u, 1, 1);    /* link up, latched low since reset */
    put_phy(9u, 0x2000u, 0x5C90u, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(5u, p.addr);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, p.id1);
    TEST_ASSERT_EQUAL_HEX16(0xC131u, p.id2);
    TEST_ASSERT_EQUAL_UINT8(1u, p.link_up);   /* the second read, not the latch */
    TEST_ASSERT_EQUAL_INT(5 * 2 + 4, reads);
}

static void test_scan_link_down(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    put_phy(0u, 0x0007u, 0xC131u, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(0u, p.addr);
    TEST_ASSERT_EQUAL_UINT8(0u, p.link_up);
}

static void test_scan_skips_a_failed_read_and_reaches_31(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    fail_at_addr = 3;
    put_phy(3u, 0x0007u, 0xC131u, 1, 0);
    put_phy(31u, 0x0007u, 0xC131u, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(31u, p.addr);
}

static void test_scan_of_an_empty_bus_finds_nothing(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    TEST_ASSERT_EQUAL_INT(-1, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_INT(64, reads);
}

static void test_boot_line(void)
{
    char buf[48];
    tfw_phy_probe_t p = { 0u, 0x0007u, 0xC131u, 1u };

    TEST_ASSERT_EQUAL_size_t(38u, tfw_phy_format(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("eth: phy addr=0 id=0007:C131 link=up\r\n", buf);
    p.addr = 31u;
    p.id2 = 0xC13Fu;
    p.link_up = 0u;
    tfw_phy_format(&p, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("eth: phy addr=31 id=0007:C13F link=down\r\n", buf);
    tfw_phy_format(NULL, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("eth: phy none\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_phy_format(&p, buf, 10u));
}

static void test_scan_with_a_failed_bmsr_read_keeps_the_phy_and_reports_link_down(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    put_phy(4u, 0x0007u, 0xC131u, 1, 0);
    fail_at_addr = 4;
    fail_reg = TFW_PHY_REG_BMSR;
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(4u, p.addr);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, p.id1);
    TEST_ASSERT_EQUAL_HEX16(0xC131u, p.id2);
    TEST_ASSERT_EQUAL_UINT8(0u, p.link_up);
    /* 0-3: two ID reads each; 4: two ID reads, one failed BMSR read, no second */
    TEST_ASSERT_EQUAL_INT(11, reads);
}

static void test_boot_line_fits_exactly_or_not_at_all(void)
{
    char buf[48];
    tfw_phy_probe_t p = { 0u, 0x0007u, 0xC131u, 1u };

    memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_size_t(38u, tfw_phy_format(&p, buf, 39u));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[38]);
    memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_phy_format(&p, buf, 38u));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_size_t(15u, tfw_phy_format(NULL, buf, 16u));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[15]);
    memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_phy_format(NULL, buf, 15u));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
}

static void test_scan_reports_link_down_when_only_the_second_bmsr_read_fails(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    put_phy(3u, 0x0007u, 0xC131u, 1, 0);    /* link up, not latched: the first BMSR read says up */
    put_phy(8u, 0x0007u, 0xC131u, 1, 0);
    /* 0-2: two ID reads each; 3: ID, ID, BMSR, then the failed second BMSR read */
    fail_read_n = 3 * 2 + 4;
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(3u, p.addr);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, p.id1);
    TEST_ASSERT_EQUAL_HEX16(0xC131u, p.id2);
    TEST_ASSERT_EQUAL_UINT8(0u, p.link_up);
    TEST_ASSERT_EQUAL_INT(10, reads);
}

static void test_scan_skips_addresses_that_read_all_zeros(void)
{
    tfw_phy_probe_t p = { 0xFFu, 0u, 0u, 0xFFu };

    memset(bus, 0x00, sizeof bus);          /* MDIO held low: every register reads 0x0000 */
    TEST_ASSERT_EQUAL_INT(-1, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_INT(64, reads);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, p.addr);
    reads = 0;
    put_phy(6u, 0x0007u, 0xC131u, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, tfw_phy_scan(fake_read, &p));
    TEST_ASSERT_EQUAL_UINT8(6u, p.addr);
    TEST_ASSERT_EQUAL_UINT8(1u, p.link_up);
    TEST_ASSERT_EQUAL_INT(6 * 2 + 4, reads);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lan8742a_id_decodes);
    RUN_TEST(test_idle_bus_ids_are_absent);
    RUN_TEST(test_bmsr_link_bit);
    RUN_TEST(test_scan_finds_the_first_phy_and_reads_bmsr_twice);
    RUN_TEST(test_scan_link_down);
    RUN_TEST(test_scan_skips_a_failed_read_and_reaches_31);
    RUN_TEST(test_scan_of_an_empty_bus_finds_nothing);
    RUN_TEST(test_boot_line);
    RUN_TEST(test_scan_with_a_failed_bmsr_read_keeps_the_phy_and_reports_link_down);
    RUN_TEST(test_boot_line_fits_exactly_or_not_at_all);
    RUN_TEST(test_scan_reports_link_down_when_only_the_second_bmsr_read_fails);
    RUN_TEST(test_scan_skips_addresses_that_read_all_zeros);
    return UNITY_END();
}
