/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OPTIGA Trust M command layer against literal APDUs (row
 * optiga-trust-m).
 *
 * tfw_optiga_transceive is faked here: each exchange the suite scripts holds
 * the command APDU the layer must send, byte for byte, and the response the
 * element gives. The command bytes are the Solution Reference Manual's tables
 * (Revision 3.60; §4.4.1.1 Table 40, §4.4.1.2 Table 41, §4.4.1.3 Table 42,
 * §4.4.1.6 Table 45), the identifier its Table 78 and the error codes its
 * Table 39. The manual's one worked example, §6.1 Figure 35 (5 bytes of the
 * coprocessor UID from offset 2), is a figure with no text in the pinned PDF,
 * so its request is written here from Table 42 as the figure's caption
 * describes it.
 *
 * Every Cmd the layer sends, in every test, is checked against the four
 * read-only commands: the fake fails the test on any other code, so no path
 * the suite drives emits SetDataObject (0x02), SetObjectProtected (0x03) or
 * any key, counter, lifecycle or binding command.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "optiga_cmd.h"
#include "optiga_i2c.h"

typedef struct {
    const uint8_t *q;
    uint16_t       qlen;
    const uint8_t *a;
    uint16_t       alen;
    tfw_status_t   st;  /* what the transport returns instead, when not TFW_OK */
} exchange_t;

#define EX(q, a)     { (q), (uint16_t)sizeof (q), (a), (uint16_t)sizeof (a), TFW_OK }
#define EX_ST(q, st) { (q), (uint16_t)sizeof (q), 0, 0u, (st) }
#define PLAY(s)      play((s), (unsigned)(sizeof (s) / sizeof (s)[0]))

static const exchange_t *script;
static unsigned          steps;
static unsigned          at;
static unsigned          cmds_seen;   /* bit per allowed (Cmd, Param) pair */
static uint8_t           out[256];
static uint16_t          len;

/* The only (Cmd, Param) pairs the layer may send. */
static const uint8_t allowed[][2] = {
    { 0xF0u, 0x00u },  /* OpenApplication, clean context */
    { 0xF1u, 0x00u },  /* CloseApplication, no save */
    { 0x81u, 0x00u },  /* GetDataObject, data */
    { 0x81u, 0x01u },  /* GetDataObject, metadata */
    { 0x01u, 0x00u },  /* GetDataObject of the last error code, no flush */
    { 0x8Cu, 0x00u },  /* GetRandom, TRNG */
};

tfw_status_t tfw_optiga_transceive(const uint8_t *apdu, uint16_t alen,
                                   uint8_t *rsp, uint16_t rcap, uint16_t *rlen,
                                   uint16_t exec_ms)
{
    const exchange_t *e;
    unsigned          i;
    int               ok = 0;

    TEST_ASSERT_TRUE_MESSAGE(alen >= 4u, "an APDU shorter than its header");
    for (i = 0u; i < sizeof allowed / sizeof allowed[0]; i++) {
        if (apdu[0] == allowed[i][0] && apdu[1] == allowed[i][1]) {
            cmds_seen |= 1u << i;
            ok = 1;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(ok, "a command code or Param outside the read-only set");
    TEST_ASSERT_EQUAL_UINT16(200u, exec_ms);
    TEST_ASSERT_TRUE_MESSAGE(at < steps, "an APDU the script does not hold");
    e = &script[at++];
    TEST_ASSERT_EQUAL_UINT16(e->qlen, alen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(e->q, apdu, alen);
    if (e->st != TFW_OK) {
        return e->st;
    }
    if (e->alen > rcap) {
        return TFW_ERR_TOO_LONG;  /* as the transport answers an overflow */
    }
    memcpy(rsp, e->a, e->alen);
    *rlen = e->alen;
    return TFW_OK;
}

static void play(const exchange_t *s, unsigned n)
{
    script = s;
    steps  = n;
    at     = 0u;
}

static void played(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(steps, at, "the script was not played out");
}

void setUp(void)
{
    play(0, 0u);
    memset(out, 0xA5, sizeof out);
    len = 0xFFFFu;
}

void tearDown(void) {}

/* Requests, from the tables. */
static const uint8_t Q_OPEN[] = {
    0xF0, 0x00, 0x00, 0x10,
    0xD2, 0x76, 0x00, 0x00, 0x04,                                     /* RID */
    0x47, 0x65, 0x6E, 0x41, 0x75, 0x74, 0x68, 0x41, 0x70, 0x70, 0x6C, /* GenAuthAppl */
};
static const uint8_t Q_CLOSE[]   = { 0xF1, 0x00, 0x00, 0x00 };
static const uint8_t Q_UID[]     = { 0x81, 0x00, 0x00, 0x06, 0xE0, 0xC2, 0x00, 0x02, 0x00, 0x05 };
static const uint8_t Q_MD_CERT[] = { 0x81, 0x01, 0x00, 0x02, 0xE0, 0xE0 };
static const uint8_t Q_RANDOM[]  = { 0x8C, 0x00, 0x00, 0x02, 0x00, 0x20 };
static const uint8_t Q_LASTERR[] = { 0x01, 0x00, 0x00, 0x06, 0xF1, 0xC2, 0x00, 0x00, 0x00, 0x01 };

/* Responses. */
static const uint8_t A_EMPTY[] = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t A_FAIL[]  = { 0xFF, 0x00, 0x00, 0x00 };
static const uint8_t A_UID[]   = { 0x00, 0x00, 0x00, 0x05, 0xCD, 0x16, 0x33, 0x82, 0x01 };
/* 0x20, length 11: LcsO op (0xC0 01 07), max 1728 (0xC4 02 06C0), used 500
 * (0xC5 02 01F4). */
static const uint8_t A_MD_CERT[] = {
    0x00, 0x00, 0x00, 0x0D,
    0x20, 0x0B, 0xC0, 0x01, 0x07, 0xC4, 0x02, 0x06, 0xC0, 0xC5, 0x02, 0x01, 0xF4,
};
static const uint8_t A_RANDOM[] = {
    0x00, 0x00, 0x00, 0x20,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static void test_open_application_sends_table_40_with_the_table_78_identifier(void)
{
    static const exchange_t s[] = { EX(Q_OPEN, A_EMPTY) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_open_application());
    played();
}

static void test_close_application_sends_table_41_without_saving(void)
{
    static const exchange_t s[] = { EX(Q_CLOSE, A_EMPTY) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_close_application());
    played();
}

static void test_get_data_sends_table_42_with_offset_and_length(void)
{
    static const exchange_t s[] = { EX(Q_UID, A_UID) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(5u, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_UID[4], out, 5u);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[5]);
}

static void test_get_data_takes_fewer_at_the_end_of_the_object(void)
{
    static const uint8_t    a[] = { 0x00, 0x00, 0x00, 0x02, 0xCD, 0x16 };
    static const exchange_t s[] = { EX(Q_UID, a) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    TEST_ASSERT_EQUAL_UINT16(2u, len);
}

static void test_get_metadata_sends_table_42_param_1(void)
{
    static const exchange_t s[] = { EX(Q_MD_CERT, A_MD_CERT) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(13u, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_MD_CERT[4], out, 13u);
}

static void test_get_random_sends_table_45_from_the_trng(void)
{
    static const exchange_t s[] = { EX(Q_RANDOM, A_RANDOM) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_random(out, 32u));
    played();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_RANDOM[4], out, 32u);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[32]);
}

/* Table 39, each code onto its status. */
static const struct {
    uint8_t      code;
    tfw_status_t st;
} table39[] = {
    { 0x00u, TFW_ERR_CRYPTO },  /* 0xFF with no error recorded */
    { 0x01u, TFW_ERR_RANGE },
    { 0x03u, TFW_ERR_RANGE },
    { 0x04u, TFW_ERR_RANGE },
    { 0x05u, TFW_ERR_RANGE },
    { 0x06u, TFW_ERR_CRYPTO },
    { 0x07u, TFW_ERR_PROTECTED_REGION },
    { 0x08u, TFW_ERR_RANGE },
    { 0x09u, TFW_ERR_TOO_LONG },
    { 0x0Au, TFW_ERR_UNSUPPORTED },
    { 0x0Bu, TFW_ERR_BAD_STATE },
    { 0x0Cu, TFW_ERR_BAD_STATE },
    { 0x0Du, TFW_ERR_TOO_LONG },
    { 0x0Eu, TFW_ERR_CRYPTO },
    { 0x0Fu, TFW_ERR_CRYPTO },
    { 0x10u, TFW_ERR_CRYPTO },
    { 0x11u, TFW_ERR_CRYPTO },
    { 0x24u, TFW_ERR_CRYPTO },
    { 0x25u, TFW_ERR_CRYPTO },
    { 0x29u, TFW_ERR_CRYPTO },
    { 0x2Au, TFW_ERR_CRYPTO },
    { 0x2Cu, TFW_ERR_CRYPTO },
    { 0x2Du, TFW_ERR_CRYPTO },
    { 0x2Eu, TFW_ERR_CRYPTO },
    { 0x2Fu, TFW_ERR_AUTH_REQUIRED },
    { 0x02u, TFW_ERR_CRYPTO },  /* not in the table */
    { 0xFFu, TFW_ERR_CRYPTO },
};

static void test_every_table_39_code_is_read_back_and_mapped(void)
{
    unsigned i;

    for (i = 0u; i < sizeof table39 / sizeof table39[0]; i++) {
        const uint8_t    a[] = { 0x00, 0x00, 0x00, 0x01, table39[i].code };
        const exchange_t s[] = { EX(Q_UID, A_FAIL), EX(Q_LASTERR, a) };

        PLAY(s);
        TEST_ASSERT_EQUAL_INT(table39[i].st, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
        played();
        TEST_ASSERT_EQUAL_INT(table39[i].st, tfw_optiga_error_status(table39[i].code));
        TEST_ASSERT_EQUAL_UINT16(0xFFFFu, len);
    }
}

static void test_each_command_reads_the_error_back_on_a_general_error(void)
{
    static const uint8_t    e7[] = { 0x00, 0x00, 0x00, 0x01, 0x07 };
    static const uint8_t    eB[] = { 0x00, 0x00, 0x00, 0x01, 0x0B };
    static const exchange_t so[] = { EX(Q_OPEN, A_FAIL), EX(Q_LASTERR, eB) };
    static const exchange_t sc[] = { EX(Q_CLOSE, A_FAIL), EX(Q_LASTERR, eB) };
    static const exchange_t sm[] = { EX(Q_MD_CERT, A_FAIL), EX(Q_LASTERR, e7) };
    static const exchange_t sr[] = { EX(Q_RANDOM, A_FAIL), EX(Q_LASTERR, e7) };

    PLAY(so);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_optiga_open_application());
    played();
    PLAY(sc);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_optiga_close_application());
    played();
    PLAY(sm);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
    PLAY(sr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_optiga_get_random(out, 32u));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
}

static void test_a_sta_other_than_00_or_ff_is_framing(void)
{
    static const uint8_t    a01[] = { 0x01, 0x00, 0x00, 0x00 };
    static const uint8_t    aFE[] = { 0xFE, 0x00, 0x00, 0x00 };
    static const uint8_t    a80[] = { 0x80, 0x00, 0x00, 0x05, 0xCD, 0x16, 0x33, 0x82, 0x01 };
    static const exchange_t s1[]  = { EX(Q_CLOSE, a01) };
    static const exchange_t s2[]  = { EX(Q_CLOSE, aFE) };
    static const exchange_t s3[]  = { EX(Q_UID, a80) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    played();
}

static void test_a_general_error_with_out_data_is_framing(void)
{
    static const uint8_t    a[] = { 0xFF, 0x00, 0x00, 0x01, 0x07 };
    static const exchange_t s[] = { EX(Q_CLOSE, a) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
}

static void test_a_failed_or_malformed_error_read(void)
{
    static const uint8_t    two[] = { 0x00, 0x00, 0x00, 0x02, 0x07, 0x07 };
    static const uint8_t    pad[] = { 0x00, 0x00, 0x00, 0x01, 0x07, 0x00 };
    static const exchange_t s1[]  = { EX(Q_CLOSE, A_FAIL), EX(Q_LASTERR, A_FAIL) };
    static const exchange_t s2[]  = { EX(Q_CLOSE, A_FAIL), EX(Q_LASTERR, two) };
    static const exchange_t s3[]  = { EX(Q_CLOSE, A_FAIL), EX(Q_LASTERR, A_EMPTY) };
    static const exchange_t s4[]  = { EX(Q_CLOSE, A_FAIL), EX(Q_LASTERR, pad) };
    static const exchange_t s5[]  = { EX(Q_CLOSE, A_FAIL), EX_ST(Q_LASTERR, TFW_ERR_TIMEOUT) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_optiga_close_application());
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s4);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s5);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_optiga_close_application());
    played();
}

static void test_a_short_response_is_framing(void)
{
    static const uint8_t    hdr3[] = { 0x00, 0x00, 0x00 };
    static const uint8_t    cut[]  = { 0x00, 0x00, 0x00, 0x05, 0xCD, 0x16, 0x33, 0x82 };
    static const exchange_t s1[]   = { EX(Q_CLOSE, hdr3) };
    static const exchange_t s2[]   = { EX(Q_UID, cut) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, len);
}

static void test_an_over_long_response(void)
{
    /* More bytes than OutLen; more data than asked; more than any buffer. */
    static const uint8_t extra[] = { 0x00, 0x00, 0x00, 0x04, 0xCD, 0x16, 0x33, 0x82, 0x01 };
    static const uint8_t six[]   = { 0x00, 0x00, 0x00, 0x06, 0xCD, 0x16, 0x33, 0x82, 0x01, 0x02 };
    static const uint8_t rnd33[] = {
        0x00, 0x00, 0x00, 0x21,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20,
    };
    static const uint8_t open1[] = { 0x00, 0x00, 0x00, 0x01, 0x00 };
    static uint8_t       huge[4u + 65u];
    static const exchange_t s1[] = { EX(Q_UID, extra) };
    static const exchange_t s2[] = { EX(Q_UID, six) };
    static const exchange_t s3[] = { EX(Q_RANDOM, rnd33) };
    static const exchange_t s4[] = { EX(Q_OPEN, open1) };
    static const exchange_t s5[] = { EX(Q_CLOSE, open1) };
    static const exchange_t s6[] = { EX(Q_MD_CERT, huge) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_data(0xE0C2u, 2u, 5u, out, &len));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_random(out, 32u));
    played();
    PLAY(s4);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_open_application());
    played();
    PLAY(s5);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_close_application());
    played();
    PLAY(s6);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TOO_LONG, tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
}

static void test_random_short_of_the_length_asked_is_framing(void)
{
    static const uint8_t    a[] = { 0x00, 0x00, 0x00, 0x08, 0, 1, 2, 3, 4, 5, 6, 7 };
    static const exchange_t s[] = { EX(Q_RANDOM, a) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_random(out, 32u));
    played();
}

static void test_metadata_that_is_not_tag_20_or_overflows_the_caller(void)
{
    static const uint8_t    a21[] = { 0x00, 0x00, 0x00, 0x02, 0x21, 0x00 };
    static const exchange_t s1[]  = { EX(Q_MD_CERT, a21) };
    static const exchange_t s2[]  = { EX(Q_MD_CERT, A_EMPTY) };
    static const exchange_t s3[]  = { EX(Q_MD_CERT, A_MD_CERT) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TOO_LONG, tfw_optiga_get_metadata(0xE0E0u, out, 12u, &len));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_metadata(0xE0E0u, out, 13u, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(13u, len);
}

static void test_arguments_out_of_range_send_nothing(void)
{
    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_get_data(0xE0C2u, 0u, 0u, out, &len));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_get_data(0xE0C2u, 0u, 65u, out, &len));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_get_random(out, 7u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_get_random(out, 65u));
    played();
}

static void test_the_edges_of_the_ranges_are_sent(void)
{
    static const uint8_t    q64[] = { 0x81, 0x00, 0x00, 0x06, 0xF1, 0xD0, 0x01, 0x00, 0x00, 0x40 };
    static const uint8_t    a0[]  = { 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t    q8[]  = { 0x8C, 0x00, 0x00, 0x02, 0x00, 0x08 };
    static const uint8_t    a8[]  = { 0x00, 0x00, 0x00, 0x08, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const uint8_t    q6[]  = { 0x8C, 0x00, 0x00, 0x02, 0x00, 0x40 };
    static uint8_t          a64[4u + 64u];
    static const exchange_t s[]   = { EX(q64, a0), EX(q8, a8), EX(q6, a64) };

    memset(a64, 0x5A, sizeof a64);
    a64[0] = 0x00u;
    a64[1] = 0x00u;
    a64[2] = 0x00u;
    a64[3] = 0x40u;
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_data(0xF1D0u, 0x0100u, 64u, out, &len));
    TEST_ASSERT_EQUAL_UINT16(0u, len);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_random(out, 8u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&a8[4], out, 8u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_get_random(out, 64u));
    played();
}

static void test_a_transport_failure_is_returned_as_it_came(void)
{
    static const exchange_t s1[] = { EX_ST(Q_OPEN, TFW_ERR_TIMEOUT) };
    static const exchange_t s2[] = { EX_ST(Q_RANDOM, TFW_ERR_CRC) };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_optiga_open_application());
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, tfw_optiga_get_random(out, 32u));
    played();
}

static void test_metadata_size_takes_c5_then_c4(void)
{
    static const uint8_t used[] = { 0x20, 0x0B, 0xC0, 0x01, 0x07, 0xC4, 0x02, 0x06, 0xC0, 0xC5, 0x02, 0x01, 0xF4 };
    static const uint8_t c5_1[] = { 0x20, 0x03, 0xC5, 0x01, 0x1B };
    static const uint8_t maxo[] = { 0x20, 0x07, 0xC0, 0x01, 0x07, 0xC4, 0x02, 0x06, 0xC0 };
    static const uint8_t c5c4[] = { 0x20, 0x08, 0xC5, 0x02, 0x00, 0x96, 0xC4, 0x02, 0x06, 0xC0 };
    /* A last TLV with an empty value, tag and length only. */
    static const uint8_t tail[] = { 0x20, 0x05, 0xC5, 0x01, 0x1B, 0xC0, 0x00 };
    uint16_t             size   = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_metadata_size(used, sizeof used, &size));
    TEST_ASSERT_EQUAL_UINT16(500u, size);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_metadata_size(c5_1, sizeof c5_1, &size));
    TEST_ASSERT_EQUAL_UINT16(27u, size);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_metadata_size(maxo, sizeof maxo, &size));
    TEST_ASSERT_EQUAL_UINT16(1728u, size);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_metadata_size(c5c4, sizeof c5c4, &size));
    TEST_ASSERT_EQUAL_UINT16(150u, size);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_metadata_size(tail, sizeof tail, &size));
    TEST_ASSERT_EQUAL_UINT16(27u, size);
}

static void test_metadata_size_refuses_broken_tlv(void)
{
    static const uint8_t none[]  = { 0x20, 0x03, 0xC0, 0x01, 0x07 };
    static const uint8_t tag[]   = { 0x21, 0x03, 0xC5, 0x01, 0x1B };
    static const uint8_t outer[] = { 0x20, 0x04, 0xC5, 0x01, 0x1B };
    static const uint8_t past[]  = { 0x20, 0x03, 0xC5, 0x02, 0x1B };
    static const uint8_t nolen[] = { 0x20, 0x04, 0xC5, 0x01, 0x1B, 0xC0 };
    static const uint8_t wide[]  = { 0x20, 0x05, 0xC5, 0x03, 0x00, 0x00, 0x1B };
    static const uint8_t empty[] = { 0x20, 0x00, 0x00 };
    uint16_t             size    = 0xBEEFu;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(none, sizeof none, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(tag, sizeof tag, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(outer, sizeof outer, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(past, sizeof past, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(nolen, sizeof nolen, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(wide, sizeof wide, &size));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_metadata_size(empty, 1u, &size));
    TEST_ASSERT_EQUAL_UINT16(0xBEEFu, size);
}

/* A 150-byte object at 0xF1D0: its metadata, then 64, 64 and 22 bytes. */
static const uint8_t Q_MD_F1D0[] = { 0x81, 0x01, 0x00, 0x02, 0xF1, 0xD0 };
static const uint8_t A_MD_150[]  = {
    0x00, 0x00, 0x00, 0x0A,
    0x20, 0x08, 0xC5, 0x02, 0x00, 0x96, 0xC4, 0x02, 0x06, 0x40,
};
static const uint8_t Q_D0_0[]   = { 0x81, 0x00, 0x00, 0x06, 0xF1, 0xD0, 0x00, 0x00, 0x00, 0x40 };
static const uint8_t Q_D0_64[]  = { 0x81, 0x00, 0x00, 0x06, 0xF1, 0xD0, 0x00, 0x40, 0x00, 0x40 };
static const uint8_t Q_D0_128[] = { 0x81, 0x00, 0x00, 0x06, 0xF1, 0xD0, 0x00, 0x80, 0x00, 0x16 };
static uint8_t       a_d0_0[4u + 64u];
static uint8_t       a_d0_64[4u + 64u];
static uint8_t       a_d0_128[4u + 22u];

static void fill(uint8_t *a, uint8_t outlen, uint8_t first)
{
    uint8_t i;

    a[0] = 0x00u;
    a[1] = 0x00u;
    a[2] = 0x00u;
    a[3] = outlen;
    for (i = 0u; i < outlen; i++) {
        a[4u + i] = (uint8_t)(first + i);
    }
}

static void test_read_object_reads_the_used_size_in_pieces(void)
{
    static const exchange_t s[] = {
        EX(Q_MD_F1D0, A_MD_150), EX(Q_D0_0, a_d0_0), EX(Q_D0_64, a_d0_64), EX(Q_D0_128, a_d0_128),
    };
    unsigned i;

    fill(a_d0_0, 64u, 0u);
    fill(a_d0_64, 64u, 64u);
    fill(a_d0_128, 22u, 128u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_read_object(0xF1D0u, out, sizeof out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(150u, len);
    for (i = 0u; i < 150u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)i, out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[150]);
}

static void test_read_object_too_big_for_the_buffer_reads_no_data(void)
{
    static const exchange_t s[] = { EX(Q_MD_F1D0, A_MD_150) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TOO_LONG, tfw_optiga_read_object(0xF1D0u, out, 149u, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, len);
}

static void test_read_object_fills_a_buffer_of_exactly_its_size(void)
{
    static const exchange_t s[] = {
        EX(Q_MD_F1D0, A_MD_150), EX(Q_D0_0, a_d0_0), EX(Q_D0_64, a_d0_64), EX(Q_D0_128, a_d0_128),
    };

    fill(a_d0_0, 64u, 0u);
    fill(a_d0_64, 64u, 64u);
    fill(a_d0_128, 22u, 128u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_read_object(0xF1D0u, out, 150u, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(150u, len);
}

static void test_read_object_a_piece_short_of_the_metadata_is_framing(void)
{
    static const exchange_t s[] = {
        EX(Q_MD_F1D0, A_MD_150), EX(Q_D0_0, a_d0_0), EX(Q_D0_64, a_d0_128),
    };

    fill(a_d0_0, 64u, 0u);
    fill(a_d0_128, 22u, 128u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_read_object(0xF1D0u, out, sizeof out, &len));
    played();
}

static void test_read_object_stops_at_the_first_failure(void)
{
    static const uint8_t    e8[]   = { 0x00, 0x00, 0x00, 0x01, 0x08 };
    static const uint8_t    nomd[] = { 0x00, 0x00, 0x00, 0x05, 0x20, 0x03, 0xC0, 0x01, 0x07 };
    static const exchange_t s1[]   = { EX(Q_MD_F1D0, A_FAIL), EX(Q_LASTERR, e8) };
    static const exchange_t s2[]   = { EX(Q_MD_F1D0, nomd) };
    static const exchange_t s3[]   = {
        EX(Q_MD_F1D0, A_MD_150), EX(Q_D0_0, a_d0_0), EX(Q_D0_64, A_FAIL), EX(Q_LASTERR, e8),
    };

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_read_object(0xF1D0u, out, sizeof out, &len));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_read_object(0xF1D0u, out, sizeof out, &len));
    played();
    fill(a_d0_0, 64u, 0u);
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_read_object(0xF1D0u, out, sizeof out, &len));
    played();
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, len);
}

/* Runs last: every allowed (Cmd, Param) pair was sent by some test above, and
 * the fake refused every other one in all of them. */
static void test_only_the_read_only_commands_were_ever_sent(void)
{
    TEST_ASSERT_EQUAL_HEX(0x3Fu, cmds_seen);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_application_sends_table_40_with_the_table_78_identifier);
    RUN_TEST(test_close_application_sends_table_41_without_saving);
    RUN_TEST(test_get_data_sends_table_42_with_offset_and_length);
    RUN_TEST(test_get_data_takes_fewer_at_the_end_of_the_object);
    RUN_TEST(test_get_metadata_sends_table_42_param_1);
    RUN_TEST(test_get_random_sends_table_45_from_the_trng);
    RUN_TEST(test_every_table_39_code_is_read_back_and_mapped);
    RUN_TEST(test_each_command_reads_the_error_back_on_a_general_error);
    RUN_TEST(test_a_sta_other_than_00_or_ff_is_framing);
    RUN_TEST(test_a_general_error_with_out_data_is_framing);
    RUN_TEST(test_a_failed_or_malformed_error_read);
    RUN_TEST(test_a_short_response_is_framing);
    RUN_TEST(test_an_over_long_response);
    RUN_TEST(test_random_short_of_the_length_asked_is_framing);
    RUN_TEST(test_metadata_that_is_not_tag_20_or_overflows_the_caller);
    RUN_TEST(test_arguments_out_of_range_send_nothing);
    RUN_TEST(test_the_edges_of_the_ranges_are_sent);
    RUN_TEST(test_a_transport_failure_is_returned_as_it_came);
    RUN_TEST(test_metadata_size_takes_c5_then_c4);
    RUN_TEST(test_metadata_size_refuses_broken_tlv);
    RUN_TEST(test_read_object_reads_the_used_size_in_pieces);
    RUN_TEST(test_read_object_too_big_for_the_buffer_reads_no_data);
    RUN_TEST(test_read_object_fills_a_buffer_of_exactly_its_size);
    RUN_TEST(test_read_object_a_piece_short_of_the_metadata_is_framing);
    RUN_TEST(test_read_object_stops_at_the_first_failure);
    RUN_TEST(test_only_the_read_only_commands_were_ever_sent);
    return UNITY_END();
}
