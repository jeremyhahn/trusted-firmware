/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OPTIGA Trust M command layer over the real IFX I2C transport (row
 * optiga-trust-m): the Infineon device certificate, data object 0xE0E0
 * (Solution Reference Manual Rev 3.70, Table 68), read as the bus carries it.
 *
 * tfw_hal_i2c_xfer is faked as in test_optiga_i2c.c, with room for a frame of
 * the full 64-byte DATA register. The frames are literals, worked out apart
 * from the code under test with the reference CRC of test_optiga_i2c.c: frame
 * number, acknowledge number, packet chaining (IFX I2C Protocol v2.03) around
 * the APDUs of Table 42.
 *
 * The 70-byte certificate is read as its metadata, then 64 bytes from offset 0
 * and 6 from offset 64; the 68-byte response to the first piece crosses two
 * frames, FIRST and LAST.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "optiga_cmd.h"
#include "optiga_i2c.h"

#define RESYNC 0xC0, 0x00, 0x00, 0x0A, 0x9A
#define ACK0   0x80, 0x00, 0x00, 0x0C, 0xEC
#define ACK1   0x81, 0x00, 0x00, 0x56, 0x30
#define ACK2   0x82, 0x00, 0x00, 0xB9, 0x54
#define ACK3   0x83, 0x00, 0x00, 0xE3, 0x88

/* GetDataObject metadata of 0xE0E0 (81 01 00 02 E0 E0) as host frame 0, and
 * the answer: LcsO op, max 1728, used 70. */
#define HQ_MD 0x03, 0x00, 0x07, 0x00, 0x81, 0x01, 0x00, 0x02, 0xE0, 0xE0, 0x59, 0x85
#define EA_MD 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x20, 0x0B, 0xC0, 0x01, \
              0x07, 0xC4, 0x02, 0x06, 0xC0, 0xC5, 0x02, 0x00, 0x46, 0x89, 0x3C
/* 64 bytes from offset 0, as host frame 1; the answer as element frames 1
 * (FIRST, 58 APDU bytes) and 2 (LAST, 10). */
#define HQ_C0 0x04, 0x00, 0x0B, 0x00, 0x81, 0x00, 0x00, 0x06, 0xE0, 0xE0, 0x00, 0x00, \
              0x00, 0x40, 0x43, 0x56
#define EA_C0_FIRST 0x05, 0x00, 0x3B, 0x01, 0x00, 0x00, 0x00, 0x40, 0x30, 0x37, 0x3E, \
    0x45, 0x4C, 0x53, 0x5A, 0x61, 0x68, 0x6F, 0x76, 0x7D, 0x84, 0x8B, 0x92, 0x99, \
    0xA0, 0xA7, 0xAE, 0xB5, 0xBC, 0xC3, 0xCA, 0xD1, 0xD8, 0xDF, 0xE6, 0xED, 0xF4, \
    0xFB, 0x02, 0x09, 0x10, 0x17, 0x1E, 0x25, 0x2C, 0x33, 0x3A, 0x41, 0x48, 0x4F, \
    0x56, 0x5D, 0x64, 0x6B, 0x72, 0x79, 0x80, 0x87, 0x8E, 0x95, 0x9C, 0xA3, 0xAB, 0x44
#define EA_C0_LAST 0x09, 0x00, 0x0B, 0x04, 0xAA, 0xB1, 0xB8, 0xBF, 0xC6, 0xCD, 0xD4, \
    0xDB, 0xE2, 0xE9, 0x1A, 0x59
/* 6 bytes from offset 64, as host frame 2; the answer as element frame 3. */
#define HQ_C1 0x0A, 0x00, 0x0B, 0x00, 0x81, 0x00, 0x00, 0x06, 0xE0, 0xE0, 0x00, 0x40, \
              0x00, 0x06, 0x83, 0x1C
#define EA_C1 0x0E, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x06, 0xF0, 0xF7, 0xFE, 0x05, \
              0x0C, 0x13, 0x30, 0x76

static const uint8_t cert[70] = {
    0x30, 0x37, 0x3E, 0x45, 0x4C, 0x53, 0x5A, 0x61, 0x68, 0x6F, 0x76, 0x7D, 0x84, 0x8B,
    0x92, 0x99, 0xA0, 0xA7, 0xAE, 0xB5, 0xBC, 0xC3, 0xCA, 0xD1, 0xD8, 0xDF, 0xE6, 0xED,
    0xF4, 0xFB, 0x02, 0x09, 0x10, 0x17, 0x1E, 0x25, 0x2C, 0x33, 0x3A, 0x41, 0x48, 0x4F,
    0x56, 0x5D, 0x64, 0x6B, 0x72, 0x79, 0x80, 0x87, 0x8E, 0x95, 0x9C, 0xA3, 0xAA, 0xB1,
    0xB8, 0xBF, 0xC6, 0xCD, 0xD4, 0xDB, 0xE2, 0xE9, 0xF0, 0xF7, 0xFE, 0x05, 0x0C, 0x13,
};

typedef struct {
    char    dir;
    uint8_t len;
    uint8_t b[64];
} step_t;

#define W(...) { 'W', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define R(...) { 'R', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define PLAY(s) play((s), (unsigned)(sizeof (s) / sizeof (s)[0]))

static const step_t *script;
static unsigned      steps;
static unsigned      at;
static uint8_t       reg;
static uint8_t       out[256];
static uint16_t      len;

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    (void)hz;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                              uint8_t *rd, uint8_t rlen)
{
    TEST_ASSERT_EQUAL_HEX8(0x30u, addr7);
    if (wlen == 1u) {
        reg = wr[0];
        return TFW_OK;
    }
    if (wlen) {
        if (wr[0] == 0x81u) {
            return TFW_OK;  /* DATA_REG_LEN asked; the fake grants 64 */
        }
        TEST_ASSERT_EQUAL_HEX8(0x80u, wr[0]);
        TEST_ASSERT_TRUE_MESSAGE(at < steps && script[at].dir == 'W',
                                 "the host wrote a frame the script does not hold");
        TEST_ASSERT_EQUAL_UINT8(script[at].len, wlen - 1u);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(script[at].b, &wr[1], script[at].len);
        at++;
        return TFW_OK;
    }
    switch (reg) {
    case 0x81u:
        rd[0] = 0x00u;
        rd[1] = 64u;
        break;
    case 0x82u:
        memset(rd, 0, rlen);
        if (at < steps && script[at].dir == 'R') {
            rd[0] = 0x40u;  /* RESP_RDY */
            rd[3] = script[at].len;
        }
        break;
    case 0x80u:
        TEST_ASSERT_TRUE_MESSAGE(at < steps && script[at].dir == 'R',
                                 "the host read DATA with no frame ready");
        TEST_ASSERT_EQUAL_UINT8(script[at].len, rlen);
        memcpy(rd, script[at].b, rlen);
        at++;
        break;
    default:
        TEST_FAIL_MESSAGE("a read with no register address written before it");
    }
    reg = 0u;
    return TFW_OK;
}

void tfw_hal_delay_us(uint16_t us) { (void)us; }

static void play(const step_t *s, unsigned n)
{
    script = s;
    steps  = n;
    at     = 0u;
}

static void played(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(steps, at, "frames of the script never went over the wire");
}

void setUp(void)
{
    static const step_t s[] = { W(RESYNC) };

    reg = 0u;
    len = 0xFFFFu;
    memset(out, 0xA5, sizeof out);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_open());
    played();
}

void tearDown(void) {}

static void test_the_device_certificate_is_read_across_frames(void)
{
    static const step_t s[] = {
        W(HQ_MD), R(ACK0), R(EA_MD), W(ACK0),
        W(HQ_C0), R(ACK1), R(EA_C0_FIRST), W(ACK1), R(EA_C0_LAST), W(ACK2),
        W(HQ_C1), R(ACK2), R(EA_C1), W(ACK3),
    };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
                          tfw_optiga_read_object(TFW_OPTIGA_OID_DEVICE_CERT, out, sizeof out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(sizeof cert, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cert, out, sizeof cert);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[sizeof cert]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_device_certificate_is_read_across_frames);
    return UNITY_END();
}
