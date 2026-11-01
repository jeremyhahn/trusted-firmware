/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's six commands (boot/boot_session.c), the command set the
 * boot answers while an update session is open.
 *
 * Arity is checked per command rather than by a table -- with six commands a
 * table would be larger than the checks -- which means each check is a separate
 * line that can be separately wrong. So this suite drives every command id,
 * every payload length at each boundary, and the bytes the handler hands to
 * the OTA engine and back, so a handler that answers but answers wrong fails
 * here. PING has no arity: it echoes a payload of any length.
 *
 * Mutants this suite kills (each tried against the unit and seen to fail it):
 *   S1  *rlen not cleared on entry
 *   S2  *action not reset to CONTINUE on entry
 *   S3  PING: one byte fewer echoed
 *   S4  PING: rlen 0 after an echo
 *   S5  OTA_BEGIN: `plen != 4` -> `plen < 4`
 *   S6  OTA_BEGIN: the length's top byte not read
 *   S7  OTA_DATA: `plen < 5` -> `plen < 6`
 *   S8  OTA_DATA: the data taken from offset 3
 *   S9  OTA_DATA: one byte fewer passed
 *   S10 OTA_DATA: the offset's high half not read
 *   S11 OTA_END: COMMIT asked for on a refused end
 *   S12 OTA_END: the payload-length check dropped
 *   S13 OTA_STATUS: rlen 4
 *   S14 OTA_STATUS: the received count's second byte misplaced
 *   S15 RESET: `plen != 1` -> `plen > 1`
 *   S16 default: an unknown command answered TFW_OK
 */
#include <string.h>
#include "unity.h"
#include "boot_session.h"

#define PAGE  128u
#define IMAGE 1024u

static tfw_ota_t                 ota;
static uint8_t                   resp[256];
static uint8_t                   rlen;
static tfw_boot_session_action_t action;

static uint8_t  flashed[IMAGE];
static uint32_t flashed_top;

static tfw_status_t sink(uint32_t addr, const uint8_t *d, uint16_t n)
{
    TEST_ASSERT_TRUE(addr + n <= IMAGE);
    memcpy(&flashed[addr], d, n);
    if (addr + n > flashed_top) {
        flashed_top = addr + n;
    }
    return TFW_OK;
}

/* Every call starts from a response and an action the handler must replace. */
static tfw_status_t call(uint8_t cmd, const uint8_t *p, uint8_t plen)
{
    memset(resp, 0xEEu, sizeof resp);
    rlen   = 0xEEu;
    action = (tfw_boot_session_action_t)0x7E;
    return tfw_boot_session_handle(&ota, cmd, p, plen, resp, &rlen, &action);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static tfw_status_t begin(uint32_t len)
{
    uint8_t p[4];

    le32(p, len);
    return call(TFW_CMD_OTA_BEGIN, p, 4u);
}

static tfw_status_t data(uint32_t off, const uint8_t *d, uint8_t n)
{
    uint8_t p[255];

    le32(p, off);
    memcpy(&p[4], d, n);
    return call(TFW_CMD_OTA_DATA, p, (uint8_t)(n + 4u));
}

static void assert_quiet(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, rlen);
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_CONTINUE, action);
}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&ota, sink, PAGE));
    memset(flashed, 0, sizeof flashed);
    flashed_top = 0u;
}

void tearDown(void) {}

static int handled(unsigned c)
{
    return c == TFW_CMD_PING || c == TFW_CMD_OTA_BEGIN || c == TFW_CMD_OTA_DATA
        || c == TFW_CMD_OTA_END || c == TFW_CMD_OTA_STATUS || c == TFW_CMD_RESET;
}

/* S1, S2, S16 */
static void test_every_other_command_id_is_unknown_and_quiet(void)
{
    uint8_t p[4] = { 1u, 2u, 3u, 4u };

    for (unsigned c = 0u; c < 256u; c++) {
        if (handled(c)) {
            continue;
        }
        for (uint8_t n = 0u; n <= 4u; n++) {
            TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, call((uint8_t)c, p, n));
            assert_quiet();
        }
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_IDLE, tfw_ota_state(&ota));
}

/* S3, S4 */
static void test_ping_echoes_every_length(void)
{
    uint8_t p[255];

    for (unsigned i = 0u; i < sizeof p; i++) {
        p[i] = (uint8_t)(i * 7u + 1u);
    }
    for (unsigned n = 0u; n <= sizeof p; n++) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_PING, p, (uint8_t)n));
        TEST_ASSERT_EQUAL_UINT8(n, rlen);
        if (n > 0u) {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(p, resp, n);
        }
        TEST_ASSERT_EQUAL_HEX8(0xEEu, resp[n < sizeof resp ? n : sizeof resp - 1u]);
        TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_CONTINUE, action);
    }
}

/* S1, S2, S5: every length but four is refused, and leaves no session. */
static void test_ota_begin_takes_exactly_four_bytes(void)
{
    uint8_t p[8];

    le32(p, IMAGE);
    le32(&p[4], 0u);
    for (uint8_t n = 0u; n <= 8u; n++) {
        TEST_ASSERT_EQUAL_INT(n == 4u ? TFW_OK : TFW_ERR_BAD_ARGC,
                              call(TFW_CMD_OTA_BEGIN, p, n));
        assert_quiet();
        TEST_ASSERT_EQUAL_UINT8(n == 4u ? TFW_OTA_RECEIVING : TFW_OTA_IDLE,
                                tfw_ota_state(&ota));
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&ota, sink, PAGE));
    }
}

/* S6: a length that differs from a good one only in a high byte is too long. */
static void test_ota_begin_reads_all_four_length_bytes(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, begin(0x01000000uL | IMAGE));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, begin(0x00010000uL | IMAGE));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_IDLE, tfw_ota_state(&ota));
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
}

/* S7: one data byte after the offset is a chunk; none is not. */
static void test_ota_data_takes_one_byte_after_the_offset(void)
{
    const uint8_t one = 0x42u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_DATA, 0, 0u));
    assert_quiet();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, data(0u, &one, 0u));
    assert_quiet();
    TEST_ASSERT_EQUAL_INT(TFW_OK, data(0u, &one, 1u));
    assert_quiet();
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_ota_received(&ota));
}

/* S8, S9: the bytes flashed are the bytes sent, in whole pages and at the end. */
static void test_ota_data_flashes_exactly_the_bytes_sent(void)
{
    uint8_t img[IMAGE];
    uint8_t chunk[] = { 128u, 100u, 1u, 27u, 251u };
    uint32_t off = 0u;
    unsigned k = 0u;

    for (unsigned i = 0u; i < IMAGE; i++) {
        img[i] = (uint8_t)(i * 13u + (i >> 8) + 5u);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    while (off < IMAGE) {
        uint8_t n = chunk[k++ % sizeof chunk];

        if (n > IMAGE - off) {
            n = (uint8_t)(IMAGE - off);
        }
        TEST_ASSERT_EQUAL_INT(TFW_OK, data(off, &img[off], n));
        assert_quiet();
        off += n;
        TEST_ASSERT_EQUAL_UINT32(off, tfw_ota_received(&ota));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_END, 0, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_COMMIT, action);
    TEST_ASSERT_EQUAL_UINT8(0u, rlen);
    TEST_ASSERT_EQUAL_UINT32(IMAGE, flashed_top);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, flashed, IMAGE);
}

/* S10: an offset that differs from the expected one only in a high byte is
 * the wrong offset. */
static void test_ota_data_reads_all_four_offset_bytes(void)
{
    const uint8_t b = 0x11u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(0x01000000uL, &b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(0x00010000uL, &b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, data(0x00000100uL, &b, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_ota_received(&ota));
}

/* S11, S12: an end that is refused, for a payload or a short image, asks for
 * nothing. */
static void test_a_refused_end_asks_for_no_commit(void)
{
    const uint8_t junk[2] = { 0u, 0u };
    uint8_t       page[PAGE];

    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    memset(page, 0x33u, sizeof page);
    for (uint32_t off = 0u; off < IMAGE; off += PAGE) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, data(off, page, PAGE));
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_END, junk, 1u));
    assert_quiet();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_END, junk, 2u));
    assert_quiet();
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_RECEIVING, tfw_ota_state(&ota));

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&ota, sink, PAGE));
    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    TEST_ASSERT_EQUAL_INT(TFW_OK, data(0u, page, PAGE));
    TEST_ASSERT_TRUE(call(TFW_CMD_OTA_END, 0, 0u) != TFW_OK);
    assert_quiet();

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&ota, sink, PAGE));
    TEST_ASSERT_TRUE(call(TFW_CMD_OTA_END, 0, 0u) != TFW_OK);
    assert_quiet();
}

/* S13, S14: the status is five bytes, the state then the count little-endian.
 * An interrupted session must be resumable or abandonable deliberately, and a
 * host cannot decide which without knowing where it got to. */
static void test_ota_status_is_state_then_count(void)
{
    static const uint8_t idle[5] = { TFW_OTA_IDLE, 0u, 0u, 0u, 0u };
    const uint8_t junk[1] = { 0u };
    uint8_t       page[PAGE];
    uint8_t       want[5];

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_STATUS, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(5u, rlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(idle, resp, 5);

    TEST_ASSERT_EQUAL_INT(TFW_OK, begin(IMAGE));
    memset(page, 0x44u, sizeof page);
    for (uint32_t off = 0u; off < 3u * PAGE; off += PAGE) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, data(off, page, PAGE));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, data(3u * PAGE, page, 7u));

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_STATUS, 0, 0u));
    want[0] = TFW_OTA_RECEIVING;
    le32(&want[1], 3u * PAGE + 7u);
    TEST_ASSERT_EQUAL_UINT8(5u, rlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, resp, 5);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, resp[5]);
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_CONTINUE, action);

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_STATUS, junk, 1u));
    assert_quiet();
}

/* S15: RESET takes exactly its one mode byte. */
static void test_reset_takes_exactly_one_byte(void)
{
    const uint8_t mode[4] = { 0u, 1u, 2u, 3u };

    for (uint8_t n = 0u; n <= 4u; n++) {
        if (n == 1u) {
            TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_RESET, mode, n));
            TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_RESET, action);
            TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        } else {
            TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_RESET, mode, n));
            assert_quiet();
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_other_command_id_is_unknown_and_quiet);
    RUN_TEST(test_ping_echoes_every_length);
    RUN_TEST(test_ota_begin_takes_exactly_four_bytes);
    RUN_TEST(test_ota_begin_reads_all_four_length_bytes);
    RUN_TEST(test_ota_data_takes_one_byte_after_the_offset);
    RUN_TEST(test_ota_data_flashes_exactly_the_bytes_sent);
    RUN_TEST(test_ota_data_reads_all_four_offset_bytes);
    RUN_TEST(test_a_refused_end_asks_for_no_commit);
    RUN_TEST(test_ota_status_is_state_then_count);
    RUN_TEST(test_reset_takes_exactly_one_byte);
    return UNITY_END();
}
