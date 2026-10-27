/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OPTIGA Trust M transport (src/crypto/optiga/optiga_i2c.c) on the
 * Cortex-M bus: the MPS2 I2C master (src/hal/cortex-m/mps2/hal_bus.c) under
 * it, as mps2-an505's optiga:I2C=1 row links the two, bit-banged over a
 * simulated SBCon in place of sbcon.c, with an OPTIGA on the bus at 0x30.
 *
 * test_optiga_i2c.c pins the frames against the HAL's byte calls; this suite
 * pins what those calls become on the wire (Solution Reference Manual §2, the
 * physical layer): every register write one transaction -- START, the address
 * and W, the register, its data, STOP -- and every read a write of the
 * register address and then a read transaction of its own, never a repeated
 * start, each after the guard time; the element's late acknowledge, a NACK of
 * its address, repeated (§2, Figures 2-2 and 2-3); and frames at the smallest
 * and the largest DATA register the transport takes (Table 2-1).
 *
 * The element is a register file behind a slave that watches the edges, as
 * test_mps2_bus.c's device does, and writes what it saw to a log the same
 * way: S, the address byte and its A or N, the bytes written and each A or N,
 * <xx for each byte it sent and the master's A or N, and P. A DATA write is
 * held against a script of the frames test_optiga_i2c.c records; a DATA read
 * answers from it.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "sbcon.h"
#include "pl022.h"
#include "optiga_i2c.h"

#define SB_BOTH   (TFW_SB_SCL | TFW_SB_SDA)
#define FOREVER   0xFFFFu

/* Frames, as test_optiga_i2c.c records them: the FCS high byte first. */
#define RESYNC  0xC0, 0x00, 0x00, 0x0A, 0x9A
#define ACK0    0x80, 0x00, 0x00, 0x0C, 0xEC
#define ACK1    0x81, 0x00, 0x00, 0x56, 0x30
#define ACK2    0x82, 0x00, 0x00, 0xB9, 0x54
#define NAK0    0xA0, 0x00, 0x00, 0x0F, 0xD7
#define Q0      0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0, 0x57, 0x63
#define Q1      0x04, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0, 0x93, 0x96
#define A0      0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x00, 0x9D
#define A1_ACK1 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x9C, 0x26
/* At DATA_REG_LEN 16: the request 10..28 (25 bytes) as FIRST, MID, LAST ... */
#define CQ0 0x03, 0x00, 0x0B, 0x01, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xE1, 0x94
#define CQ1 0x07, 0x00, 0x0B, 0x02, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x68, 0x57
#define CQ2 0x0B, 0x00, 0x06, 0x04, 0x24, 0x25, 0x26, 0x27, 0x28, 0x5B, 0xBA
/* ... and the response 40..4D (14 bytes) as FIRST, LAST. */
#define CA0 0x02, 0x00, 0x0B, 0x01, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0xAB, 0x62
#define CA1 0x06, 0x00, 0x05, 0x04, 0x4A, 0x4B, 0x4C, 0x4D, 0xB7, 0xC8

static const uint8_t apdu[]  = { 0x01, 0x00, 0x00, 0x02, 0xE0, 0xE0 };
static const uint8_t reply[] = { 0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE };

/* ---- the element: its registers and its script ---------------------------- */

typedef struct {
    char    dir;
    uint8_t len;
    uint8_t b[TFW_OPTIGA_DATA_REG_LEN];
} step_t;

#define W(...) { 'W', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define R(...) { 'R', (uint8_t)sizeof((const uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define S(n)    { 'S', (uint8_t)(n), { 0 } }
#define PLAY(s) play((s), (unsigned)(sizeof (s) / sizeof (s)[0]))

static struct {
    const step_t *script;
    unsigned      steps, at;
    uint8_t       reg;          /* the register address last written; 0 after a read */
    uint16_t      reg_len;      /* what the element reads back from DATA_REG_LEN */
    uint16_t      asked;        /* what the host wrote to DATA_REG_LEN */
} el;

enum { IDLE, RX, ACK_OUT, TX, ACK_IN, DONE };

/* ---- the wire ----------------------------------------------------------- */

static struct {
    uint32_t m;                 /* the lines the master has released */
    uint8_t  s_sda;             /* the element's SDA, 1 released */
    uint8_t  state, shift, nbits, is_addr, rw, master_ack, ours;
    uint16_t hold_sda;          /* SCL rises it holds SDA low through */
    /* the transaction in progress */
    uint8_t  w[1u + TFW_OPTIGA_DATA_REG_LEN + 8u];
    uint8_t  wn, refused;
    uint8_t  tx[TFW_OPTIGA_DATA_REG_LEN];
    uint8_t  txlen, txi;
    /* faults */
    unsigned busy;              /* accesses whose address it NACKs first */
    uint8_t  nack_byte;         /* the byte, 1-based, of the next DATA write it refuses */
    /* what the test reads */
    unsigned starts, repeated, addr_nacks, refused_writes;
    unsigned scl_rises, delays, guard_waits;
    uint16_t delay_min, delay_max;
    uint8_t  max_write, max_read;
    char     log[4096];
} bus;

static void say(const char *s)
{
    if (bus.log[0] != '\0') {
        strncat(bus.log, " ", sizeof bus.log - strlen(bus.log) - 1u);
    }
    strncat(bus.log, s, sizeof bus.log - strlen(bus.log) - 1u);
}

static void say_byte(const char *prefix, uint8_t b)
{
    char s[8];

    (void)snprintf(s, sizeof s, "%s%02X", prefix, (unsigned)b);
    say(s);
}

/* A write transaction the element took whole: a register address alone, or
 * a register and what is written to it. */
static void element_write(const uint8_t *w, uint8_t n)
{
    if (n == 1u) {
        el.reg = w[0];
        return;
    }
    if (w[0] == TFW_OPTIGA_REG_DATA_REG_LEN) {
        TEST_ASSERT_EQUAL_UINT8(3u, n);
        el.asked = (uint16_t)((w[1] << 8) | w[2]);
        return;
    }
    TEST_ASSERT_EQUAL_HEX8(TFW_OPTIGA_REG_DATA, w[0]);
    TEST_ASSERT_TRUE_MESSAGE(el.at < el.steps && el.script[el.at].dir == 'W',
                             "the host wrote a frame the script does not hold");
    TEST_ASSERT_EQUAL_UINT8(el.script[el.at].len, n - 1u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(el.script[el.at].b, &w[1], el.script[el.at].len);
    el.at++;
}

/* A read transaction addressed to the element: what it will send. */
static void element_read(void)
{
    const step_t *s = el.at < el.steps ? &el.script[el.at] : NULL;

    bus.txi = 0u;
    switch (el.reg) {
    case TFW_OPTIGA_REG_DATA_REG_LEN:
        bus.tx[0] = (uint8_t)(el.reg_len >> 8);
        bus.tx[1] = (uint8_t)(el.reg_len & 0xFFu);
        bus.txlen = 2u;
        break;
    case TFW_OPTIGA_REG_I2C_STATE:
        memset(bus.tx, 0, 4u);
        if (s && (s->dir == 'R' || s->dir == 'S')) {
            bus.tx[0] = TFW_OPTIGA_STATE_RESP_RDY;
            bus.tx[3] = s->len;
            if (s->dir == 'S') {
                el.at++;
            }
        }
        bus.txlen = 4u;
        break;
    case TFW_OPTIGA_REG_DATA:
        TEST_ASSERT_TRUE_MESSAGE(s && s->dir == 'R', "the host read DATA with no frame ready");
        memcpy(bus.tx, s->b, s->len);
        bus.txlen = s->len;
        el.at++;
        break;
    default:
        TEST_FAIL_MESSAGE("a read with no register address written before it");
    }
    el.reg = 0u;
}

static uint32_t lines(void)
{
    uint32_t v = bus.m;

    if (!bus.s_sda || bus.hold_sda) {
        v &= ~TFW_SB_SDA;
    }
    return v;
}

static void tx_load(void)
{
    uint8_t b = (bus.txi < bus.txlen) ? bus.tx[bus.txi] : 0xFFu;

    say_byte("<", b);
    bus.shift = b;
    bus.s_sda = (uint8_t)((b >> 7) & 1u);
    bus.nbits = 1u;
    bus.state = TX;
}

static void on_rise(uint8_t sda)
{
    bus.scl_rises++;
    if (bus.hold_sda && bus.hold_sda != FOREVER) {
        bus.hold_sda--;
    }
    if (bus.state == RX) {
        bus.shift = (uint8_t)((bus.shift << 1) | sda);
        bus.nbits++;
    } else if (bus.state == ACK_IN) {
        bus.master_ack = (uint8_t)!sda;
        say(bus.master_ack ? "A" : "N");
    }
}

/* A byte in: the address, or a byte written to the element. */
static uint8_t byte_taken(void)
{
    if (bus.is_addr) {
        bus.rw   = (uint8_t)(bus.shift & 1u);
        bus.ours = (uint8_t)((bus.shift >> 1) == TFW_OPTIGA_ADDR);
        if (bus.ours && bus.busy) {
            bus.busy--;
            bus.ours = 0u;
        }
        if (!bus.ours) {
            bus.addr_nacks++;
        }
        return bus.ours;
    }
    if (bus.wn < sizeof bus.w) {
        bus.w[bus.wn] = bus.shift;
    }
    bus.wn++;
    if (bus.nack_byte && bus.w[0] == TFW_OPTIGA_REG_DATA && bus.wn == bus.nack_byte) {
        bus.nack_byte = 0u;
        bus.refused   = 1u;
        return 0u;
    }
    return 1u;
}

static void on_fall(void)
{
    uint8_t ack;

    switch (bus.state) {
    case RX:
        if (bus.nbits < 8u) {
            break;
        }
        say_byte("", bus.shift);
        ack = byte_taken();
        say(ack ? "A" : "N");
        bus.s_sda = (uint8_t)!ack;
        bus.state = ack ? ACK_OUT : DONE;
        break;
    case ACK_OUT:
        bus.s_sda = 1u;
        if (bus.is_addr && bus.rw) {
            element_read();
            tx_load();
        } else {
            bus.state = RX;
            bus.is_addr = 0u;
            bus.shift = 0u;
            bus.nbits = 0u;
        }
        break;
    case TX:
        if (bus.nbits < 8u) {
            bus.s_sda = (uint8_t)(((unsigned)bus.shift >> (7u - bus.nbits)) & 1u);
            bus.nbits++;
        } else {
            bus.s_sda = 1u;
            bus.state = ACK_IN;
        }
        break;
    case ACK_IN:
        if (bus.master_ack) {
            bus.txi++;
            tx_load();
        } else {
            /* The master NACKs its last byte: it read what the element had. */
            TEST_ASSERT_EQUAL_UINT8(bus.txlen, bus.txi + 1u);
            if (bus.txlen > bus.max_read) {
                bus.max_read = bus.txlen;
            }
            bus.state = DONE;
        }
        break;
    default:
        break;
    }
}

static void on_stop(void)
{
    say("P");
    if (bus.ours && !bus.rw && bus.wn) {
        if (bus.refused) {
            bus.refused_writes++;
        } else {
            if (bus.wn > bus.max_write) {
                bus.max_write = bus.wn;
            }
            element_write(bus.w, bus.wn);
        }
    }
    bus.state = IDLE;
    bus.ours  = 0u;
}

/* The master moved a line: what did the element see? */
static void master_sets(uint32_t m)
{
    uint32_t before = lines();
    uint32_t after;

    bus.m = m;
    after = lines();
    if ((before & after & TFW_SB_SCL) && (before & TFW_SB_SDA) != (after & TFW_SB_SDA)) {
        if (after & TFW_SB_SDA) {
            on_stop();
        } else {
            if (bus.state != IDLE) {
                bus.repeated++;
            }
            say(bus.state == IDLE ? "S" : "Sr");
            bus.starts++;
            bus.state = RX;
            bus.is_addr = 1u;
            bus.shift = 0u;
            bus.nbits = 0u;
            bus.wn = 0u;
            bus.refused = 0u;
        }
        bus.s_sda = 1u;
    } else if (!(before & TFW_SB_SCL) && (after & TFW_SB_SCL)) {
        on_rise((uint8_t)((after & TFW_SB_SDA) != 0u));
    } else if ((before & TFW_SB_SCL) && !(after & TFW_SB_SCL)) {
        on_fall();
    }
}

/* ---- sbcon.h, pl022.h and tfw_hal_delay_us, as hal_bus.c links them ------ */

uint32_t tfw_mps2_sb_lines(void)          { return lines(); }
void     tfw_mps2_sb_release(uint32_t m)   { master_sets(bus.m | m); }
void     tfw_mps2_sb_drive_low(uint32_t m) { master_sets(bus.m & ~m); }

/* The SPI master in the same file is never called here. */
uint32_t tfw_mps2_ssp_read(uint32_t off)            { (void)off; return 0u; }
void     tfw_mps2_ssp_write(uint32_t off, uint32_t v) { (void)off; (void)v; }
uint32_t tfw_mps2_misc_read(void)                   { return 0u; }
void     tfw_mps2_misc_write(uint32_t v)            { (void)v; }

void tfw_hal_delay_us(uint16_t us)
{
    bus.delays++;
    if (us == TFW_OPTIGA_GUARD_US) {
        bus.guard_waits++;
        return;
    }
    if (us < bus.delay_min) { bus.delay_min = us; }
    if (us > bus.delay_max) { bus.delay_max = us; }
}

/* ---- helpers ------------------------------------------------------------- */

static void play(const step_t *s, unsigned n)
{
    el.script = s;
    el.steps  = n;
    el.at     = 0u;
}

static void played(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(el.steps, el.at, "frames of the script never went over the wire");
}

static void open_at(uint16_t len)
{
    static const step_t s[] = { W(RESYNC) };

    el.reg_len = len;
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_open());
    played();
}

static uint8_t  rsp[64];
static uint16_t rlen;

static tfw_status_t exchange(uint16_t rcap)
{
    return tfw_optiga_transceive(apdu, (uint16_t)sizeof apdu, rsp, rcap, &rlen, 5u);
}

static void got_reply(void)
{
    TEST_ASSERT_EQUAL_UINT16(sizeof reply, rlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reply, rsp, sizeof reply);
}

/* §8.1.1, the reference implementation, seed 0. */
static uint16_t fcs_reference(const uint8_t *d, unsigned n)
{
    uint16_t seed = 0u;

    while (n--) {
        uint8_t c = *d++;
        for (int i = 0; i < 8; i++) {
            seed = (uint16_t)(((seed ^ c) & 0x01u) ? (seed >> 1) ^ 0x8408u : seed >> 1);
            c    = (uint8_t)(c >> 1);
        }
    }
    return seed;
}

/* A data frame (§3.1 Figure 3-1): FCTR, LEN, PCTR 00 and the APDU, FCS. */
static uint8_t data_frame(uint8_t *f, uint8_t fctr, const uint8_t *a, uint8_t alen)
{
    uint8_t  plen = (uint8_t)(1u + alen);
    uint16_t fcs;

    f[0] = fctr;
    f[1] = 0u;
    f[2] = plen;
    f[3] = 0u;
    memcpy(&f[4], a, alen);
    fcs = fcs_reference(f, 3u + plen);
    f[3u + plen] = (uint8_t)(fcs >> 8);
    f[4u + plen] = (uint8_t)(fcs & 0xFFu);
    return (uint8_t)(TFW_OPTIGA_FRAME_OVERHEAD + plen);
}

void setUp(void)
{
    memset(&bus, 0, sizeof bus);
    bus.m = SB_BOTH;
    bus.s_sda = 1u;
    bus.delay_min = 0xFFFFu;
    memset(&el, 0, sizeof el);
    el.reg_len = 64u;
    rlen = 0xFFFFu;
    memset(rsp, 0xA5, sizeof rsp);
}

/* No access the transport makes is a write and a read in one: the element
 * takes no repeated start (§2.1). */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, bus.repeated, "a repeated start");
}

/* ---- tests --------------------------------------------------------------- */

/* Open on the wire: DATA_REG_LEN written as 64 in one transaction, read back
 * as an address write and a read of two bytes, then the resync frame written
 * to DATA -- each after the guard time, at the element's clock (100 kHz is a
 * 5 us half period). */
static void test_open_on_the_wire(void)
{
    open_at(64u);
    TEST_ASSERT_EQUAL_STRING("S 60 A 81 A 00 A 40 A P "
                             "S 60 A 81 A P S 61 A <00 A <40 N P "
                             "S 60 A 80 A C0 A 00 A 00 A 0A A 9A A P", bus.log);
    TEST_ASSERT_EQUAL_UINT16(64u, el.asked);
    TEST_ASSERT_EQUAL_UINT(4u, bus.starts);
    TEST_ASSERT_EQUAL_UINT(bus.starts, bus.guard_waits);
    TEST_ASSERT_EQUAL_UINT16(1000000uL / TFW_ELEMENT_I2C_HZ / 2u, bus.delay_min);
    TEST_ASSERT_EQUAL_UINT16(bus.delay_min, bus.delay_max);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

/* Two exchanges over the SBCon: the request, I2C_STATE polled, the ACK and
 * the response read, ours sent; the second frame counted on from the first. */
static void test_two_exchanges_over_the_sbcon(void)
{
    static const step_t s[] = {
        W(Q0), R(ACK0), R(A0), W(ACK0),
        W(Q1), R(A1_ACK1), W(ACK1),
    };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    got_reply();
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    got_reply();
    played();
    TEST_ASSERT_EQUAL_UINT(bus.starts, bus.guard_waits);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

/* At the largest register the transport asks for, 64: a 58-byte APDU is one
 * frame of 64, the longest write the bus carries (65 bytes with the register
 * address), and the response the same, the longest read. */
static void test_the_largest_frame_each_way_at_register_64(void)
{
    static step_t s[] = { S(0), R(ACK0), S(0), W(ACK0) };
    uint8_t q[TFW_OPTIGA_DATA_REG_LEN - TFW_OPTIGA_FRAME_OVERHEAD - 1u];
    uint8_t a[sizeof q];
    uint8_t f[sizeof s[0].b];

    /* The builder against a recorded frame first. */
    TEST_ASSERT_EQUAL_UINT8(12u, data_frame(f, 0x03u, apdu, sizeof apdu));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((const uint8_t[]){ Q0 }), f, 12u);

    for (unsigned i = 0u; i < sizeof q; i++) {
        q[i] = (uint8_t)(0x10u + i);
        a[i] = (uint8_t)(0x80u + i);
    }
    s[0].dir = 'W';
    s[0].len = data_frame(s[0].b, 0x03u, q, sizeof q);
    s[2].dir = 'R';
    s[2].len = data_frame(s[2].b, 0x00u, a, sizeof a);
    TEST_ASSERT_EQUAL_UINT8(TFW_OPTIGA_DATA_REG_LEN, s[0].len);

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_transceive(q, sizeof q, rsp, sizeof rsp, &rlen, 5u));
    played();
    TEST_ASSERT_EQUAL_UINT16(sizeof a, rlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, rsp, sizeof a);
    TEST_ASSERT_EQUAL_UINT8(1u + TFW_OPTIGA_DATA_REG_LEN, bus.max_write);
    TEST_ASSERT_EQUAL_UINT8(TFW_OPTIGA_DATA_REG_LEN, bus.max_read);
}

/* At the smallest, 16: a request chained in three frames and a response in
 * two, no transaction longer than the register and its address. */
static void test_a_chain_each_way_at_register_16(void)
{
    static const step_t s[] = {
        W(CQ0), R(ACK0), W(CQ1), R(ACK1), W(CQ2), R(ACK2),
        R(CA0), W(ACK0), R(CA1), W(ACK1),
    };
    uint8_t q[25];

    for (unsigned i = 0u; i < sizeof q; i++) {
        q[i] = (uint8_t)(0x10u + i);
    }
    open_at(16u);
    TEST_ASSERT_EQUAL_UINT16(64u, el.asked);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_optiga_transceive(q, sizeof q, rsp, sizeof rsp, &rlen, 5u));
    played();
    TEST_ASSERT_EQUAL_UINT16(14u, rlen);
    for (unsigned i = 0u; i < 14u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x40u + i, rsp[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(17u, bus.max_write);
    TEST_ASSERT_EQUAL_UINT8(16u, bus.max_read);
}

/* A register the element reads back outside 16..64 is refused before any
 * frame goes out, and the transport stays closed. */
static void test_open_refuses_a_register_out_of_range(void)
{
    static const uint16_t bad[] = { 15u, 65u };

    for (unsigned i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        setUp();
        el.reg_len = bad[i];
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_optiga_open());
        TEST_ASSERT_EQUAL_UINT(3u, bus.starts);    /* the write, and its read back */
        TEST_ASSERT_EQUAL_UINT(0u, el.at);         /* no resync */
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, exchange(sizeof rsp));
    TEST_ASSERT_EQUAL_UINT(3u, bus.starts);        /* closed: nothing went out */
}

/* I2C_STATE announcing 65 bytes at register 64 is NAKed and DATA left unread:
 * no read on the bus is longer than the frame that follows. */
static void test_a_length_past_the_register_is_naked_unread(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), S(65u), W(NAK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
    TEST_ASSERT_EQUAL_UINT8(12u, bus.max_read);
}

/* The element busy NACKs its address; the access is repeated after the guard
 * time until it answers, and given up as TFW_ERR_NACK after 200. */
static void test_a_late_acknowledge_is_repeated_on_the_wire(void)
{
    static const char want[] = "S 60 N P S 60 N P S 60 N P S 60 A 81 A 00 A 40 A P";

    bus.busy = 3u;
    open_at(64u);
    TEST_ASSERT_EQUAL_STRING_LEN(want, bus.log, sizeof want - 1u);
    TEST_ASSERT_EQUAL_UINT(3u, bus.addr_nacks);
    TEST_ASSERT_EQUAL_UINT(bus.starts, bus.guard_waits);

    setUp();
    bus.busy = 1000u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NACK, tfw_optiga_open());
    TEST_ASSERT_EQUAL_UINT(200u, bus.addr_nacks);
    TEST_ASSERT_EQUAL_UINT(200u, bus.starts);
    TEST_ASSERT_EQUAL_UINT(200u, bus.guard_waits);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, lines());
}

/* A frame byte the element refuses stops the bus; the frame goes again whole,
 * and only the whole one is taken. */
static void test_a_frame_refused_mid_write_is_sent_again_whole(void)
{
    static const step_t s[] = { W(Q0), R(ACK0), R(A0), W(ACK0) };

    open_at(64u);
    PLAY(s);
    bus.nack_byte = 5u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, exchange(sizeof rsp));
    played();
    got_reply();
    TEST_ASSERT_EQUAL_UINT(1u, bus.refused_writes);
}

/* SDA held low through the nine clocks of a bus clear is a stuck bus: the
 * open returns TFW_ERR_TIMEOUT at once, with no START, not repeated as a
 * late acknowledge would be. */
static void test_a_stuck_bus_is_a_timeout_not_repeated(void)
{
    bus.hold_sda = FOREVER;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_optiga_open());
    TEST_ASSERT_EQUAL_UINT(0u, bus.starts);
    TEST_ASSERT_EQUAL_UINT(1u, bus.guard_waits);
    TEST_ASSERT_EQUAL_UINT(9u, bus.scl_rises);
    TEST_ASSERT_EQUAL_HEX32(SB_BOTH, bus.m);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_on_the_wire);
    RUN_TEST(test_two_exchanges_over_the_sbcon);
    RUN_TEST(test_the_largest_frame_each_way_at_register_64);
    RUN_TEST(test_a_chain_each_way_at_register_16);
    RUN_TEST(test_open_refuses_a_register_out_of_range);
    RUN_TEST(test_a_length_past_the_register_is_naked_unread);
    RUN_TEST(test_a_late_acknowledge_is_repeated_on_the_wire);
    RUN_TEST(test_a_frame_refused_mid_write_is_sent_again_whole);
    RUN_TEST(test_a_stuck_bus_is_a_timeout_not_repeated);
    return UNITY_END();
}
