/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The W5x00 driver's ring arithmetic under a chip that answers anything.
 *
 * Linked as test_w5x00.c is -- src/net/w5x00.c over one chip file, against the
 * software W5100 / W5500 of test/model/w5x00_model.c -- with the one addition
 * of -Wl,--wrap=tfw_hal_spi_xfer: every SPI frame the driver sends reaches the
 * model unchanged, and on the way back the wrapper may replace what the model
 * answered for the socket registers the driver computes with:
 *
 *   Sn_RX_RSR  received size: how many bytes the next batch is
 *   Sn_RX_RD   where the batch starts in the receive ring
 *   Sn_TX_FSR  free size: how many bytes may be written before SEND
 *   Sn_TX_WR   where they go in the transmit ring
 *   Sn_SR      socket status: what tfw_eth_state() reports
 *   Sn_IR      interrupt bits: what tfw_eth_send() waits on
 *   Sn_CR      the command register: whether a command was taken
 *
 * Each iteration of a seeded xorshift32 picks a class of hostile answer and
 * drives receive, transmit, send and state through it. The wrapper checks every
 * frame on the way: each address falls in a window the chip has, and no batch
 * reads more of the receive ring, nor does any fill write more of the transmit
 * ring, than the size the chip last reported or the 2 KB the ring holds.
 * Afterwards a clean connection must carry bytes both ways as test_w5x00 pins.
 *
 * * Received sizes above the ring's 2048 are drawn too, 0x0801 and 0xFFFF among
 * the edges, and no Sn_RX_RD write may move the pointer past the size the chip
 * reported or the ring. This suite found tfw_eth_rx() taking Sn_RX_RSR as it
 * came, so that on both chips RSR 0x0801 with any RX_RD read the 2 KB ring
 * round again and handed the framer the 2049th byte, which the chip never
 * received. Fixed by "fix: a W5x00 received size above the RX ring is
 * refused, not read round the ring": such a size closes the socket. Free
 * sizes are drawn up to 0xFFFF: the driver sends at the end of every reply,
 * long before a fill could pass the ring.
 */

#include <string.h>
#include "unity.h"
#include "tfw/eth.h"
#include "tfw/hal.h"
#include "w5x00_model.h"

#define CHIP       ((uint8_t)W5X_TEST_CHIP)
#define RING       2048u
#define ITERATIONS 4000u
#define SEED       0x7F4A7C15u

/* Socket register offsets, from the datasheets. */
#define S_CR      0x01u
#define S_IR      0x02u
#define S_SR      0x03u
#define S_TX_FSR  0x20u
#define S_TX_WR   0x24u
#define S_RX_RSR  0x26u
#define S_RX_RD   0x28u

#define SP_COMMON 0u
#define SP_SOCK0  1u
#define SP_TX0    2u
#define SP_RX0    3u

/* How a register answers while a class runs. */
#define H_MODEL   0u              /* what the model says */
#define H_FIXED   1u              /* one value, the same on every read */
#define H_FLIP    2u              /* a fresh random value on every read */
#define H_SIZE    3u              /* a fresh size within the ring on every 16-bit read */

typedef struct { uint8_t mode; uint16_t v; } hostile_t;

static const tfw_netcfg_t cfg = {
    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u },
    { 192u, 0u, 2u, 10u },
    { 255u, 255u, 255u, 0u },
    { 192u, 0u, 2u, 1u },
    { 192u, 0u, 2u, 20u },
    7020u
};

static uint32_t rng;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* ------------------------------------------------------------- the wrapper */

static struct {
    hostile_t rsr, rx_rd, fsr, tx_wr, sr, ir, cr;
    uint8_t   hi;                 /* high byte of the 16-bit read in progress */
    uint16_t  last_rsr, last_fsr; /* sizes the driver last read in full */
    uint32_t  rx_since, tx_since; /* ring bytes moved since that read */
    uint32_t  stray;              /* frames outside every window */
    uint32_t  over_rx, over_tx;   /* ring bytes moved past the size or the ring */
    uint32_t  rx_reads, tx_writes;
    uint16_t  last_rx_rd;         /* Sn_RX_RD the driver last read in full */
    uint8_t   wr_hi;              /* high byte of the Sn_RX_RD write in progress */
    uint32_t  over_rd;            /* Sn_RX_RD writes past the size or the ring */
    uint32_t  rsr_above;          /* received sizes above the ring the driver read */
} bus;

tfw_status_t __real_tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                                     uint8_t *rd, uint8_t len);
tfw_status_t __wrap_tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                                     uint8_t *rd, uint8_t len);

static uint8_t answer(hostile_t *h, uint8_t model, uint8_t low)
{
    if (h->mode == H_SIZE && !low) {
        h->v = (uint16_t)(next() % (RING + 1u));
    }
    switch (h->mode) {
    case H_SIZE:  return low ? (uint8_t)h->v : (uint8_t)(h->v >> 8);
    case H_FIXED: return low ? (uint8_t)h->v : (uint8_t)(h->v >> 8);
    case H_FLIP:  return (uint8_t)next();
    default:      return model;
    }
}

/* Returns the register's hostile control for a read of socket offset off, and
 * through *low whether off is the low byte of a 16-bit register. */
static hostile_t *control(uint16_t off, uint8_t *low)
{
    *low = 1u;
    switch (off) {
    case S_CR:            return &bus.cr;
    case S_IR:            return &bus.ir;
    case S_SR:            return &bus.sr;
    case S_TX_FSR + 1u:   return &bus.fsr;
    case S_TX_WR + 1u:    return &bus.tx_wr;
    case S_RX_RSR + 1u:   return &bus.rsr;
    case S_RX_RD + 1u:    return &bus.rx_rd;
    default:              break;
    }
    *low = 0u;
    switch (off) {
    case S_TX_FSR:        return &bus.fsr;
    case S_TX_WR:         return &bus.tx_wr;
    case S_RX_RSR:        return &bus.rsr;
    case S_RX_RD:         return &bus.rx_rd;
    default:              return 0;
    }
}

/* Decodes the frame into its register space and offset, the way each part
 * takes it. 0 if the frame addresses nothing the chip has. */
static uint8_t decode(const uint8_t *f, uint8_t len, uint8_t *space, uint16_t *off, uint8_t *write)
{
    uint16_t a;

    if (len != 4u) {
        return 0u;
    }
    if (CHIP == W5X_MODEL_W5100) {
        a = (uint16_t)((uint16_t)f[1] << 8 | f[2]);
        *write = (uint8_t)(f[0] == 0xF0u);
        if (f[0] != 0xF0u && f[0] != 0x0Fu) {
            return 0u;
        }
        if (a < 0x0030u) {
            *space = SP_COMMON; *off = a;
        } else if (a >= 0x0400u && a < 0x0430u) {
            *space = SP_SOCK0;  *off = (uint16_t)(a - 0x0400u);
        } else if (a >= 0x4000u && a < 0x4000u + RING) {
            *space = SP_TX0;    *off = (uint16_t)(a - 0x4000u);
        } else if (a >= 0x6000u && a < 0x6000u + RING) {
            *space = SP_RX0;    *off = (uint16_t)(a - 0x6000u);
        } else {
            return 0u;
        }
        return 1u;
    }
    *off   = (uint16_t)((uint16_t)f[0] << 8 | f[1]);
    *space = (uint8_t)(f[2] >> 3);
    *write = (uint8_t)(f[2] & 0x04u);
    if ((f[2] & 0x03u) != 0u || *space > SP_RX0) {
        return 0u;                            /* not variable-length, or no block */
    }
    return (uint8_t)(*space != SP_COMMON || *off < 0x0040u);
}

tfw_status_t __wrap_tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                                     uint8_t *rd, uint8_t len)
{
    uint8_t          f[4], space = 0u, write = 0u, low;
    uint16_t         off = 0u;
    hostile_t *h;
    tfw_status_t     st;

    if (len == 4u) {
        memcpy(f, wr, 4u);                    /* the HAL is in-place safe */
    }
    st = __real_tfw_hal_spi_xfer(cs, wr, rd, len);
    if (len != 4u || !decode(f, len, &space, &off, &write)) {
        bus.stray++;
        return st;
    }
    if (space == SP_RX0 && !write) {
        bus.rx_reads++;
        bus.rx_since++;
        if (bus.rx_since > bus.last_rsr || bus.rx_since > RING) {
            bus.over_rx++;
        }
    } else if (space == SP_TX0 && write) {
        bus.tx_writes++;
        bus.tx_since++;
        if (bus.tx_since > bus.last_fsr || bus.tx_since > RING) {
            bus.over_tx++;
        }
    } else if (space == SP_SOCK0 && !write) {
        h = control(off, &low);
        if (h) {
            rd[3] = answer(h, rd[3], low);
            if (!low) {
                bus.hi = rd[3];
            } else if (off == S_RX_RSR + 1u) {
                bus.last_rsr = (uint16_t)((uint16_t)bus.hi << 8 | rd[3]);
                bus.rx_since = 0u;
                if (bus.last_rsr > RING) {
                    bus.rsr_above++;
                }
            } else if (off == S_RX_RD + 1u) {
                bus.last_rx_rd = (uint16_t)((uint16_t)bus.hi << 8 | rd[3]);
            } else if (off == S_TX_FSR + 1u) {
                bus.last_fsr = (uint16_t)((uint16_t)bus.hi << 8 | rd[3]);
                bus.tx_since = 0u;
            }
        }
    } else if (space == SP_SOCK0 && write) {
        if (off == S_RX_RD) {
            bus.wr_hi = f[3];
        } else if (off == S_RX_RD + 1u &&
                   ((uint16_t)((uint16_t)((uint16_t)bus.wr_hi << 8 | f[3]) - bus.last_rx_rd) > bus.last_rsr ||
                    (uint16_t)((uint16_t)((uint16_t)bus.wr_hi << 8 | f[3]) - bus.last_rx_rd) > RING)) {
            bus.over_rd++;
        }
    }
    return st;
}

/* -------------------------------------------------------------- the fixture */

static void calm(void)
{
    memset(&bus.rsr, 0, sizeof bus.rsr);
    bus.rx_rd = bus.fsr = bus.tx_wr = bus.sr = bus.ir = bus.cr = bus.rsr;
}

void setUp(void)
{
    memset(&bus, 0, sizeof bus);
    w5x_model_reset(CHIP);
}
void tearDown(void) {}

static void connect(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 50000u));
    w5x_model_peer_accept();
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

/* A size at the ring's edges, or anywhere up to it. Never above: see the top. */
static uint16_t size_edge(void)
{
    static const uint16_t edge[] = { 0u, 1u, 2u, RING - 1u, RING, RING + 1u, 0xFFFFu };

    return (next() & 1u) ? edge[next() % (sizeof edge / sizeof edge[0])]
                         : (uint16_t)(next() % (RING + 1u));
}

/* A free size at the ring's edges, past it, or anything at all. */
static uint16_t free_edge(void)
{
    static const uint16_t edge[] = { 0u, 1u, RING - 1u, RING, RING + 1u, 0xFFFFu };

    return (next() & 1u) ? edge[next() % (sizeof edge / sizeof edge[0])]
                         : (uint16_t)next();
}

/* A ring pointer just before, on or just after the 16-bit wrap or the 2 KB one. */
static uint16_t ptr_edge(void)
{
    static const uint16_t base[] = { 0x0000u, 0x07FFu, 0x0800u, 0xF800u, 0xFFFFu };
    uint16_t d = (uint16_t)(next() % 7u);

    if (next() % 4u == 0u) {
        return (uint16_t)next();
    }
    return (uint16_t)(base[next() % (sizeof base / sizeof base[0])] + d - 3u);
}

/* What the peer puts in the receive ring: a frame-shaped run with bits
 * flipped and the tail cut, or noise. */
static void peer_noise(void)
{
    static const uint8_t shape[] = { 0x7Eu, 0x01u, 0x31u, 0x06u, 0x00u, 0x00u, 0x9Bu, 0x2Cu, 0x7Eu };
    uint8_t  b[64];
    uint16_t n, i;

    if (next() & 1u) {
        memcpy(b, shape, sizeof shape);
        for (i = (uint16_t)(next() % 4u); i > 0u; i--) {
            b[next() % sizeof shape] ^= (uint8_t)(1u << (next() % 8u));
        }
        n = (uint16_t)(next() % (sizeof shape + 1u));
    } else {
        n = (uint16_t)(next() % (sizeof b + 1u));
        for (i = 0u; i < n; i++) {
            b[i] = (uint8_t)next();
        }
    }
    w5x_model_peer_send(b, n);
}

/* --------------------------------------------------------------- the runs */

#define CLASSES 8u

static void hostile(uint8_t c)
{
    calm();
    switch (c) {
    case 0u:                                  /* received size and pointer at the edges */
        bus.rsr.mode   = H_FIXED; bus.rsr.v   = size_edge();
        bus.rx_rd.mode = H_FIXED; bus.rx_rd.v = ptr_edge();
        break;
    case 1u:                                  /* free size short of the frame, pointer at the edges */
        bus.fsr.mode   = H_FIXED; bus.fsr.v   = (uint16_t)(next() % 8u);
        bus.tx_wr.mode = H_FIXED; bus.tx_wr.v = ptr_edge();
        break;
    case 2u:                                  /* status and interrupts flip mid-transfer */
        bus.sr.mode = H_FLIP;
        bus.ir.mode = H_FLIP;
        break;
    case 3u:                                  /* a command register that never clears, or flips */
        bus.cr.mode = (next() & 1u) ? H_FIXED : H_FLIP;
        bus.cr.v    = (uint16_t)(1u + next() % 255u);
        break;
    case 4u:                                  /* sizes that never read the same twice */
        bus.rsr.mode = H_SIZE;
        bus.fsr.mode = H_SIZE;
        break;
    case 5u:                                  /* every register random, the received size within the ring */
        bus.rsr.mode   = H_FIXED; bus.rsr.v   = (uint16_t)(next() % (RING + 1u));
        bus.fsr.mode   = H_FIXED; bus.fsr.v   = (uint16_t)next();
        bus.rx_rd.mode = H_FIXED; bus.rx_rd.v = (uint16_t)next();
        bus.tx_wr.mode = H_FIXED; bus.tx_wr.v = (uint16_t)next();
        bus.sr.mode    = H_FIXED; bus.sr.v    = (uint16_t)(next() & 0xFFu);
        bus.ir.mode    = H_FIXED; bus.ir.v    = (uint16_t)(next() & 0xFFu);
        break;
    case 6u:                                  /* both sizes and both pointers at the edges */
        bus.rsr.mode   = H_FIXED; bus.rsr.v   = size_edge();
        bus.fsr.mode   = H_FIXED; bus.fsr.v   = free_edge();
        bus.rx_rd.mode = H_FIXED; bus.rx_rd.v = ptr_edge();
        bus.tx_wr.mode = H_FIXED; bus.tx_wr.v = ptr_edge();
        break;
    default:                                  /* the model's own answers over a seeded wrap */
        break;
    }
}

static void exercise(void)
{
    uint8_t  got;
    uint16_t i, n;

    for (i = 0u; i < RING + 2u; i++) {
        if ((next() % 97u) == 0u) {
            (void)tfw_eth_state();
        }
        if (!tfw_eth_rx(&got)) {
            break;
        }
    }
    n = (uint16_t)(next() % 300u);
    for (i = 0u; i < n; i++) {
        if ((next() % 61u) == 0u) {
            tfw_eth_send();
        }
        if (!tfw_eth_tx((uint8_t)next())) {
            break;
        }
    }
    tfw_eth_send();
    (void)tfw_eth_state();
}

static void test_hostile_registers_never_move_the_rings_past_what_they_hold(void)
{
    uint32_t it, ran[CLASSES];
    uint8_t  c, dirty = 0u, wire[8];
    static const uint8_t a[3] = { 1u, 2u, 3u };
    static const uint8_t b[5] = { 'h', 'e', 'l', 'l', 'o' };

    memset(ran, 0, sizeof ran);
    rng = SEED;
    w5x_model_seed_pointers(0xFFFDu);
    connect();
    for (it = 0u; it < ITERATIONS; it++) {
        c = (uint8_t)(next() % CLASSES);
        /* A class that closed the socket, or that fed the driver a size or a
         * pointer it then wrote back -- after which the model's own sizes are
         * as wrong as what was written and may exceed the ring: a fresh part,
         * the driver's state carried over. */
        if (w5x_model_sock(S_SR) != 0x17u || dirty) {
            dirty = 0u;
            calm();
            w5x_model_reset(CHIP);
            w5x_model_seed_pointers(ptr_edge());
            connect();
        }
        peer_noise();
        (void)w5x_model_peer_recv(wire, sizeof wire);
        hostile(c);
        exercise();
        dirty = (uint8_t)(c != 2u && c != 3u && c != 7u);
        ran[c]++;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, bus.stray, "a frame outside the chip's windows");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, bus.over_rx, "a batch read past its size or the ring");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, bus.over_tx, "a fill wrote past its free size or the ring");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, bus.over_rd, "an Sn_RX_RD write past the size or the ring");
    }
    for (c = 0u; c < CLASSES; c++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0u, ran[c]);
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, bus.rx_reads);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, bus.rsr_above);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, bus.tx_writes);

    /* The link recovers: a clean part carries bytes both ways, in order. */
    calm();
    w5x_model_reset(CHIP);
    connect();
    w5x_model_peer_send(a, 3u);
    for (it = 0u; it < 3u; it++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&wire[it]));
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, wire, 3);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&wire[0]));
    for (it = 0u; it < sizeof b; it++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(b[it]));
    }
    tfw_eth_send();
    TEST_ASSERT_EQUAL_UINT16(5u, w5x_model_peer_recv(wire, sizeof wire));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, wire, 5);
    TEST_ASSERT_EQUAL_UINT8(1u, w5x_model_sends());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hostile_registers_never_move_the_rings_past_what_they_hold);
    return UNITY_END();
}
