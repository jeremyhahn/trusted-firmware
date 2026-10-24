/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OPTIGA Trust M command layer against literal APDUs (row
 * optiga-trust-m).
 *
 * tfw_optiga_transceive is faked here: each exchange the suite scripts holds
 * the command APDU the layer must send, byte for byte, and the response the
 * element gives. The command bytes are the Solution Reference Manual's tables
 * (Revision 3.70; §4.4.1.1 Table 40, §4.4.1.2 Table 41, §4.4.1.3 Table 42,
 * §4.4.1.6 Table 45, §4.4.1.12 Table 51 and §4.4.1.13 Table 52 with Table
 * 62's ECDSA and Table 57's P-256), the
 * identifier its Table 78 and the error codes its Table 39. The manual's one worked example, §6.1 Figure 35 (5 bytes of the
 * coprocessor UID from offset 2), is a figure with no text in the pinned PDF,
 * so its request is written here from Table 42 as the figure's caption
 * describes it.
 *
 * Every Cmd the layer sends, in every test, is checked against the six
 * commands that write no object: the fake fails the test on any other code, so no path
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
    { 0xB1u, 0x11u },  /* CalcSign, ECDSA */
    { 0xB2u, 0x11u },  /* VerifySign, ECDSA */
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
    static uint8_t       huge[4u + 71u];  /* past the 70-byte DER signature */
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

/* A random stream of any length but the one asked is refused before a byte
 * of it reaches the caller. */
static void test_a_random_of_the_wrong_length_writes_nothing(void)
{
    static const uint8_t    a[] = { 0x00, 0x00, 0x00, 0x09, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const exchange_t s[] = { EX(Q_RANDOM, a) };
    unsigned                i;

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_random(out, 32u));
    played();
    for (i = 0u; i < 33u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5u, out[i]);
    }
}

/* Metadata that is not tag 0x20 is refused unread: neither the buffer nor
 * the length is touched. */
static void test_metadata_of_the_wrong_tag_writes_nothing(void)
{
    static const uint8_t    a21[] = { 0x00, 0x00, 0x00, 0x02, 0x21, 0x00 };
    static const exchange_t s[]   = { EX(Q_MD_CERT, a21) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_get_metadata(0xE0E0u, out, sizeof out, &len));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[1]);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, len);
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

/* CalcSign, Table 51: ECDSA (Table 62, 0x11) over a 32-byte digest (tag
 * 0x01) with the key at 0xE0F0 (tag 0x03); the signature comes back as two
 * DER INTEGERs, r then s, with no SEQUENCE around them (note 64). */
static const uint8_t DIGEST[32] = {
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
};
static const uint8_t Q_SIGN[] = {
    0xB1, 0x11, 0x00, 0x28,
    0x01, 0x00, 0x20,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0x03, 0x00, 0x02, 0xE0, 0xF0,
};
static uint8_t a_sig[4u + 72u];

/* One DER INTEGER of n value bytes counting up from first, behind a sign
 * byte 0x00 when lead is set. */
static uint16_t der_int(uint8_t *a, uint8_t n, int lead, uint8_t first)
{
    uint16_t i = 2u;
    uint8_t  k;

    a[0] = 0x02u;
    a[1] = (uint8_t)(n + (lead ? 1u : 0u));
    if (lead) {
        a[i++] = 0x00u;
    }
    for (k = 0u; k < n; k++) {
        a[i++] = (uint8_t)(first + k);
    }
    return i;
}

/* A CalcSign response in a_sig holding r then s; returns its length. */
static uint16_t sig_rsp(uint8_t rn, int rlead, uint8_t rfirst, uint8_t sn, int slead, uint8_t sfirst)
{
    uint16_t n = 4u;

    n = (uint16_t)(n + der_int(&a_sig[n], rn, rlead, rfirst));
    n = (uint16_t)(n + der_int(&a_sig[n], sn, slead, sfirst));
    a_sig[0] = 0x00u;
    a_sig[1] = 0x00u;
    a_sig[2] = 0x00u;
    a_sig[3] = (uint8_t)(n - 4u);
    return n;
}

static void sign_through(uint16_t n, tfw_status_t want)
{
    const exchange_t s[] = { { Q_SIGN, (uint16_t)sizeof Q_SIGN, a_sig, n, TFW_OK } };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(want, tfw_optiga_calc_sign(DIGEST, out));
    played();
}

static void test_calc_sign_sends_table_51_with_the_device_key(void)
{
    uint8_t i;

    sign_through(sig_rsp(32u, 0, 0x01u, 32u, 0, 0x21u), TFW_OK);
    TEST_ASSERT_EQUAL_HEX8(0x44u, a_sig[3]);
    for (i = 0u; i < 64u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x01u + i), out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[64]);
}

static void test_calc_sign_drops_the_sign_byte_of_r_and_s(void)
{
    uint8_t i;

    /* 70 bytes, the longest signature: both values have their top bit set. */
    sign_through(sig_rsp(32u, 1, 0x80u, 32u, 1, 0xC0u), TFW_OK);
    TEST_ASSERT_EQUAL_HEX8(0x46u, a_sig[3]);
    for (i = 0u; i < 32u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x80u + i), out[i]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xC0u + i), out[32u + i]);
    }
}

static void test_calc_sign_pads_short_r_and_s_on_the_left(void)
{
    static const uint8_t r80[] = { 0x02, 0x02, 0x00, 0x80, 0x02, 0x01, 0x01 };
    uint8_t              i;

    sign_through(sig_rsp(31u, 0, 0x60u, 1u, 0, 0x05u), TFW_OK);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    for (i = 0u; i < 31u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x60u + i), out[1u + i]);
    }
    for (i = 32u; i < 63u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x05u, out[63]);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_ecdsa_der_to_raw(r80, sizeof r80, out));
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[30]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[31]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[63]);
}

static void test_der_signature_that_is_not_two_p256_integers_is_framing(void)
{
    static const uint8_t tag[]   = { 0x03, 0x01, 0x05, 0x02, 0x01, 0x06 };
    static const uint8_t stag[]  = { 0x02, 0x01, 0x05, 0x30, 0x01, 0x06 };
    static const uint8_t zero[]  = { 0x02, 0x00, 0x02, 0x01, 0x06 };
    static const uint8_t neg[]   = { 0x02, 0x01, 0x80, 0x02, 0x01, 0x06 };
    static const uint8_t pad[]   = { 0x02, 0x02, 0x00, 0x05, 0x02, 0x01, 0x06 };
    static const uint8_t past[]  = { 0x02, 0x05, 0x05, 0x02, 0x01, 0x06 };
    static const uint8_t trail[] = { 0x02, 0x01, 0x05, 0x02, 0x01, 0x06, 0x00 };
    static const uint8_t no_s[]  = { 0x02, 0x01, 0x05 };
    static const uint8_t no_l[]  = { 0x02, 0x01, 0x05, 0x02 };
    static uint8_t       wide[4u + 72u];
    uint16_t             n;

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(tag, sizeof tag, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(stag, sizeof stag, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(zero, sizeof zero, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(neg, sizeof neg, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(pad, sizeof pad, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(past, sizeof past, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(trail, sizeof trail, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(no_s, sizeof no_s, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(no_l, sizeof no_l, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(no_s, 0u, out));
    /* 33 bytes whose first is not the sign byte, then 34: wider than P-256. */
    n = der_int(wide, 33u, 0, 0x01u);
    n = (uint16_t)(n + der_int(&wide[n], 1u, 0, 0x06u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(wide, n, out));
    n = der_int(wide, 33u, 1, 0x80u);
    n = (uint16_t)(n + der_int(&wide[n], 1u, 0, 0x06u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_ecdsa_der_to_raw(wide, n, out));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[63]);
}

/* Each DER length byte, and OutLen, one up or one down, fails. */
static void test_calc_sign_any_length_off_by_one_fails(void)
{
    static const uint8_t pos[] = { 3u, 5u, 39u };  /* OutLen, r's length, s's length */
    uint16_t             n;
    unsigned             i;
    int                  d;

    for (i = 0u; i < sizeof pos; i++) {
        for (d = -1; d <= 1; d += 2) {
            n = sig_rsp(32u, 0, 0x01u, 32u, 0, 0x21u);
            a_sig[pos[i]] = (uint8_t)(a_sig[pos[i]] + d);
            sign_through(n, TFW_ERR_FRAMING);
        }
    }
    for (i = 0u; i < 2u; i++) {
        for (d = -1; d <= 1; d += 2) {
            n = sig_rsp(32u, 1, 0x80u, 32u, 1, 0xC0u);
            a_sig[i == 0u ? 5u : 40u] = (uint8_t)(a_sig[i == 0u ? 5u : 40u] + d);
            sign_through(n, TFW_ERR_FRAMING);
        }
    }
}

static void test_calc_sign_errors(void)
{
    static const uint8_t    e7[]   = { 0x00, 0x00, 0x00, 0x01, 0x07 };
    static const uint8_t    hdr3[] = { 0x00, 0x00, 0x00 };
    static const uint8_t    sta1[] = { 0x01, 0x00, 0x00, 0x00 };
    static const exchange_t s1[]   = { EX(Q_SIGN, A_FAIL), EX(Q_LASTERR, e7) };
    static const exchange_t s2[]   = { EX(Q_SIGN, hdr3) };
    static const exchange_t s3[]   = { EX(Q_SIGN, sta1) };
    static const exchange_t s4[]   = { EX(Q_SIGN, A_EMPTY) };
    static const exchange_t s5[]   = { EX_ST(Q_SIGN, TFW_ERR_TIMEOUT) };
    uint16_t                n;

    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_optiga_calc_sign(DIGEST, out));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_calc_sign(DIGEST, out));
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_calc_sign(DIGEST, out));
    played();
    PLAY(s4);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_calc_sign(DIGEST, out));
    played();
    PLAY(s5);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_optiga_calc_sign(DIGEST, out));
    played();
    /* Short: one byte cut off the end. Over-long: one byte past OutLen, and
     * 71 bytes of signature, more than the response buffer holds. */
    n = sig_rsp(32u, 0, 0x01u, 32u, 0, 0x21u);
    sign_through((uint16_t)(n - 1u), TFW_ERR_FRAMING);
    a_sig[n] = 0x00u;
    sign_through((uint16_t)(n + 1u), TFW_ERR_FRAMING);
    n = sig_rsp(32u, 1, 0x80u, 33u, 1, 0xC0u);
    TEST_ASSERT_EQUAL_UINT16(4u + 71u, n);
    sign_through(n, TFW_ERR_TOO_LONG);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
}

/* VerifySign (§4.4.1.13, Table 52) over DIGEST, with the key in the command:
 * tag 0x05 Table 57's 0x03 (NIST P-256), tag 0x06 the key as a DER BIT
 * STRING (note 70), X = 0x40.., Y = 0x60... The signature is two DER
 * INTEGERs (note 68). In Q_VERIFY r = 0x80..0x9F needs the sign byte of
 * §6.2.3's note and s = 0x01..0x20 does not. */
static const uint8_t PUB[64] = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
};
static const uint8_t SIG[64] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
};
static const uint8_t Q_VERIFY[] = {
    0xB2, 0x11, 0x00, 0xB6,
    0x01, 0x00, 0x20,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0x02, 0x00, 0x45,
    0x02, 0x21, 0x00, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C,
    0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C,
    0x9D, 0x9E, 0x9F,
    0x02, 0x20, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x1F, 0x20,
    0x05, 0x00, 0x01, 0x03,
    0x06, 0x00, 0x44, 0x03, 0x42, 0x00, 0x04,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
};

static void test_verify_sign_sends_table_52_with_the_key_in_the_command(void)
{
    static const exchange_t s[] = { EX(Q_VERIFY, A_EMPTY) };

    TEST_ASSERT_EQUAL_UINT(186u, sizeof Q_VERIFY);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_verify_sign(DIGEST, SIG, PUB));
    played();
}

/* r = 00 7F 02..1F loses its zero byte and needs no sign byte; s = 31 zero
 * bytes then 0x80 loses all 31 and takes a sign byte back (X.690 §8.3.2). */
static void test_verify_sign_encodes_short_r_and_s_minimally(void)
{
    static const uint8_t q[] = {
        0xB2, 0x11, 0x00, 0x96,
        0x01, 0x00, 0x20,
        0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0x02, 0x00, 0x25,
        0x02, 0x1F, 0x7F, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
        0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
        0x1F,
        0x02, 0x02, 0x00, 0x80,
        0x05, 0x00, 0x01, 0x03,
        0x06, 0x00, 0x44, 0x03, 0x42, 0x00, 0x04,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    };
    static const exchange_t s[] = { EX(q, A_EMPTY) };
    uint8_t                 sig[64];
    uint8_t                 i;

    memset(sig, 0, sizeof sig);
    sig[1] = 0x7Fu;
    for (i = 2u; i < 32u; i++) {
        sig[i] = i;
    }
    sig[63] = 0x80u;
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_verify_sign(DIGEST, sig, PUB));
    played();
}

/* A zero value keeps one zero byte: 02 01 00 (X.690 §8.3.2). The element
 * refuses such a signature; the request is still well formed. */
static void test_verify_sign_encodes_a_zero_value_as_one_byte(void)
{
    static uint8_t          q[sizeof Q_VERIFY];
    static const uint8_t    e2c[] = { 0x00, 0x00, 0x00, 0x01, 0x2C };
    uint8_t                 sig[64];
    const exchange_t        s[] = {
        { q, (uint16_t)(sizeof Q_VERIFY - 32u), A_FAIL, (uint16_t)sizeof A_FAIL, TFW_OK },
        EX(Q_LASTERR, e2c),
    };

    /* Q_VERIFY with r = 02 01 00 in place of its 35 bytes. */
    memcpy(q, Q_VERIFY, 4u + 35u + 3u);
    q[3]  = (uint8_t)(0xB6u - 32u);
    q[41] = (uint8_t)(0x45u - 32u);
    q[42] = 0x02u;
    q[43] = 0x01u;
    q[44] = 0x00u;
    memcpy(&q[45], &Q_VERIFY[77], sizeof Q_VERIFY - 77u);
    memset(sig, 0, 32u);
    memcpy(&sig[32], &SIG[32], 32u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_optiga_verify_sign(DIGEST, sig, PUB));
    played();
}

static void test_verify_sign_errors(void)
{
    static const uint8_t    e2c[]  = { 0x00, 0x00, 0x00, 0x01, 0x2C };
    static const uint8_t    e05[]  = { 0x00, 0x00, 0x00, 0x01, 0x05 };
    static const uint8_t    data[] = { 0x00, 0x00, 0x00, 0x01, 0x00 };
    static const exchange_t s1[]   = { EX(Q_VERIFY, A_FAIL), EX(Q_LASTERR, e2c) };
    static const exchange_t s2[]   = { EX(Q_VERIFY, A_FAIL), EX(Q_LASTERR, e05) };
    static const exchange_t s3[]   = { EX(Q_VERIFY, data) };
    static const exchange_t s4[]   = { EX_ST(Q_VERIFY, TFW_ERR_TIMEOUT) };

    /* Table 39 0x2C, Signature verification failure. */
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_optiga_verify_sign(DIGEST, SIG, PUB));
    played();
    /* 0x05, Invalid parameter in data field: a key not on the curve. */
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_verify_sign(DIGEST, SIG, PUB));
    played();
    /* OutData is absent (Table 52). */
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_optiga_verify_sign(DIGEST, SIG, PUB));
    played();
    PLAY(s4);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_optiga_verify_sign(DIGEST, SIG, PUB));
    played();
}

/* Runs last: every allowed (Cmd, Param) pair was sent by some test above, and
 * the fake refused every other one in all of them. */
static void test_only_the_read_only_commands_were_ever_sent(void)
{
    TEST_ASSERT_EQUAL_HEX(0xFFu, cmds_seen);
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
    RUN_TEST(test_a_random_of_the_wrong_length_writes_nothing);
    RUN_TEST(test_metadata_of_the_wrong_tag_writes_nothing);
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
    RUN_TEST(test_calc_sign_sends_table_51_with_the_device_key);
    RUN_TEST(test_calc_sign_drops_the_sign_byte_of_r_and_s);
    RUN_TEST(test_calc_sign_pads_short_r_and_s_on_the_left);
    RUN_TEST(test_der_signature_that_is_not_two_p256_integers_is_framing);
    RUN_TEST(test_calc_sign_any_length_off_by_one_fails);
    RUN_TEST(test_calc_sign_errors);
    RUN_TEST(test_verify_sign_sends_table_52_with_the_key_in_the_command);
    RUN_TEST(test_verify_sign_encodes_short_r_and_s_minimally);
    RUN_TEST(test_verify_sign_encodes_a_zero_value_as_one_byte);
    RUN_TEST(test_verify_sign_errors);
    RUN_TEST(test_only_the_read_only_commands_were_ever_sent);
    return UNITY_END();
}
