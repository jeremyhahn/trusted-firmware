/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A software WIZnet W5100 or W5500 behind the SPI HAL, for the driver suites.
 *
 * It IS tfw_hal_spi_init and tfw_hal_spi_xfer, and the two GPIO calls the
 * driver makes, so the driver under test runs exactly as on the device and
 * talks to something that decodes each part's real framing:
 *
 *   W5100  four bytes per select: 0xF0 write / 0x0F read, a 16-bit address on
 *          a flat map (common 0x0000, socket 0 0x0400, TX 0x4000, RX 0x6000),
 *          one data byte. MISO shifts 0x00 0x01 0x02 during the header.
 *   W5500  a 16-bit offset, a control byte (block select << 3, write bit 0x04,
 *          operating mode), then data with the offset auto-incrementing.
 *
 * Each part is fed the other's framing exactly as the silicon would take it, so
 * the identity checks are exercised against what the wrong shield really
 * answers. Sockets 0 and 1 are modelled, with the reset-default 2 KB buffers. Register offsets are written out here from the datasheets rather
 * than taken from src/net/w5x00.h: a model that shared the driver's constants
 * would agree with any mistake in them.
 *
 * UDP is modelled as far as the DHCP client uses it: OPEN in UDP mode reads
 * SOCK_UDP and SEND puts the queued bytes on the wire as one datagram, whose
 * destination the test reads from DIPR and DPORT. A test hands a datagram up
 * with w5x_model_peer_send() behind the 8-byte header the chip would prepend.
 * A unicast is answered at ARP unless it goes to the one silent address, when
 * it raises TIMEOUT and never reaches the wire.
 *
 * Not modelled: sockets 2 and up, MACRAW, interrupts on a pin, PHY
 * configuration, retransmission timing. The TCP peer is the test.
 */

#include <string.h>
#include "tfw/hal.h"
#include "w5x00_model.h"

#define BUF_SIZE   2048u
#define BUF_MASK   0x07FFu

/* Common registers, both parts. */
#define C_MR       0x00u
#define C_RTR_5100 0x17u           /* retry time, two bytes, 0x07D0 after reset */
#define C_RCR_5100 0x19u           /* retry count, 0x08 after reset */
#define C_RMSR     0x1Au           /* W5100 only */
#define C_TMSR     0x1Bu           /* W5100 only */
#define C_RTR_5500 0x19u
#define C_RCR_5500 0x1Bu
#define C_VERSIONR 0x39u           /* W5500 only */

/* Socket registers, both parts. */
#define S_MR       0x00u
#define S_CR       0x01u
#define S_IR       0x02u
#define S_SR       0x03u
#define S_DIPR     0x0Cu
#define S_TX_FSR   0x20u
#define S_TX_RD    0x22u
#define S_TX_WR    0x24u
#define S_RX_RSR   0x26u
#define S_RX_RD    0x28u
#define S_RX_WR    0x2Au

#define SPACE_COMMON 0u
#define SPACE_SOCK0  1u
#define SPACE_TX0    2u
#define SPACE_RX0    3u
#define SPACE_NONE   0xFFu
#define SOCKETS      2u            /* space >> 2 is the socket, as in a block select */

typedef struct {
    uint8_t  sock[0x30];
    uint8_t  txbuf[BUF_SIZE];
    uint8_t  rxbuf[BUF_SIZE];
    uint16_t rx_wr;               /* where the next byte from the peer lands */
    uint16_t rsr;                 /* received size, updated on arrival and RECV */
} sock_t;

static uint8_t  chip;
static uint8_t  common[0x40];
static sock_t   socks[SOCKETS];
static uint8_t  peer_sn;          /* the socket the test's peer calls act on */
static uint8_t  arp_silent[4];    /* the one address no host answers ARP for */
static uint8_t  wire[4096];       /* what SEND put on the wire, not yet read */
static uint16_t wire_n;
static uint8_t  sends;
static uint8_t  cr_stuck;
static uint8_t  cr_stuck_on;   /* the one command never taken; 0: none */
static uint8_t  unstable, flip;
static uint16_t tx_cap;
static uint16_t rsr_forced;
static uint16_t seed;
static uint32_t writes;
static uint16_t rtr_reset;        /* what RTR and RCR read after a reset */
static uint8_t  rcr_reset;

static uint8_t  spi_ok;           /* last init was mode 0 or 3, MSB first */
static uint16_t cs_port;
static uint8_t  cs_bit;

typedef struct { uint16_t port; uint8_t bit, used, out, high; } pin_t;
static pin_t pins[8];

static uint16_t get16(const uint8_t *r, uint8_t off)
{
    return (uint16_t)((uint16_t)r[off] << 8 | r[off + 1u]);
}

static void put16(uint8_t *r, uint8_t off, uint16_t v)
{
    r[off]      = (uint8_t)(v >> 8);
    r[off + 1u] = (uint8_t)v;
}

static void chip_reset(void)
{
    uint8_t n;

    memset(common, 0, sizeof common);
    for (n = 0u; n < SOCKETS; n++) {
        memset(socks[n].sock, 0, sizeof socks[n].sock);
        socks[n].rx_wr = 0u;
        socks[n].rsr   = 0u;
    }
    if (chip == W5X_MODEL_W5100) {
        put16(common, C_RTR_5100, rtr_reset);
        common[C_RCR_5100] = rcr_reset;
        common[C_RMSR] = 0x55u;
        common[C_TMSR] = 0x55u;
    } else {
        put16(common, C_RTR_5500, rtr_reset);
        common[C_RCR_5500] = rcr_reset;
        common[C_VERSIONR] = 0x04u;
    }
}

void w5x_model_reset(uint8_t which)
{
    chip     = which;
    memset(socks, 0, sizeof socks);
    memset(arp_silent, 0, sizeof arp_silent);
    peer_sn  = 0u;
    memset(pins, 0, sizeof pins);
    wire_n   = 0u;
    sends    = 0u;
    cr_stuck = 0u;
    cr_stuck_on = 0u;
    unstable = 0u;
    flip     = 0u;
    tx_cap   = 0xFFFFu;
    rsr_forced = 0u;
    seed     = 0u;
    writes   = 0u;
    spi_ok   = 0u;
    cs_port  = 0xFFFFu;
    cs_bit   = 0xFFu;
    rtr_reset = 0x07D0u;
    rcr_reset = 0x08u;
    chip_reset();
}

/* ------------------------------------------------------------ the socket */

static void command(sock_t *s, uint8_t cr)
{
    uint16_t p;
    uint8_t  noarp;

    switch (cr) {
    case 0x01u:                               /* OPEN */
        if ((s->sock[S_MR] & 0x0Fu) == 0x01u || (s->sock[S_MR] & 0x0Fu) == 0x02u) {
            /* TCP opens to INIT, UDP to SOCK_UDP */
            s->sock[S_SR] = (s->sock[S_MR] & 0x0Fu) == 0x01u ? 0x13u : 0x22u;
            put16(s->sock, S_TX_RD, seed);
            put16(s->sock, S_TX_WR, seed);
            put16(s->sock, S_RX_RD, seed);
            put16(s->sock, S_RX_WR, seed);
            s->rx_wr = seed;
            s->rsr   = 0u;
        }
        break;
    case 0x04u:                               /* CONNECT */
        if (s->sock[S_SR] == 0x13u) {
            s->sock[S_SR] = 0x15u;               /* SYNSENT */
        }
        break;
    case 0x08u:                               /* DISCON */
    case 0x10u:                               /* CLOSE */
        s->sock[S_SR] = 0x00u;
        break;
    case 0x20u:                               /* SEND */
        if (s->sock[S_SR] == 0x17u || s->sock[S_SR] == 0x1Cu || s->sock[S_SR] == 0x22u) {
            /* A UDP unicast goes only once its address answers ARP; the
             * silent one never does, and the chip drops the datagram. */
            noarp = (uint8_t)(s->sock[S_SR] == 0x22u && arp_silent[0] != 0u &&
                              memcmp(&s->sock[S_DIPR], arp_silent, 4u) == 0);
            for (p = get16(s->sock, S_TX_RD); p != get16(s->sock, S_TX_WR); p++) {
                if (!noarp && wire_n < sizeof wire) {
                    wire[wire_n++] = s->txbuf[p & BUF_MASK];
                }
            }
            put16(s->sock, S_TX_RD, p);
            s->sock[S_IR] |= noarp ? 0x08u : 0x10u; /* TIMEOUT : SEND_OK */
            sends++;
        }
        break;
    case 0x40u:                               /* RECV */
        s->rsr = (uint16_t)(s->rx_wr - get16(s->sock, S_RX_RD));
        break;
    default:
        break;
    }
}

/* The socket a space belongs to, with the space folded onto socket 0's
 * numbers; a space no socket has folds to SPACE_NONE. */
static sock_t *fold(uint8_t *space)
{
    uint8_t n = (uint8_t)(*space >> 2);

    if (*space == SPACE_COMMON) {
        return &socks[0];
    }
    if (n >= SOCKETS || (*space & 3u) == 0u) {
        *space = SPACE_NONE;
        return &socks[0];
    }
    *space = (uint8_t)(*space & 3u);
    return &socks[n];
}

static uint8_t reg_read(uint8_t space, uint16_t off)
{
    sock_t  *s = fold(&space);
    uint16_t v;

    switch (space) {
    case SPACE_COMMON:
        return off < sizeof common ? common[off] : 0u;
    case SPACE_SOCK0:
        if (off >= sizeof s->sock) {
            return 0u;
        }
        if (off == S_TX_FSR || off == S_TX_FSR + 1u) {
            v = (uint16_t)(BUF_SIZE - (uint16_t)(get16(s->sock, S_TX_WR) - get16(s->sock, S_TX_RD)));
            if (v > tx_cap) {
                v = tx_cap;
            }
            if (unstable) {
                flip = (uint8_t)(flip + (off == S_TX_FSR));
                v = (uint16_t)(v ^ (flip & 1u));
            }
            return off == S_TX_FSR ? (uint8_t)(v >> 8) : (uint8_t)v;
        }
        if (off == S_RX_RSR || off == S_RX_RSR + 1u) {
            v = rsr_forced ? rsr_forced : s->rsr;
            if (unstable) {
                flip = (uint8_t)(flip + (off == S_RX_RSR));
                v = (uint16_t)(v ^ (flip & 1u));
            }
            return off == S_RX_RSR ? (uint8_t)(v >> 8) : (uint8_t)v;
        }
        return s->sock[off];
    case SPACE_TX0:
        return s->txbuf[off & BUF_MASK];
    case SPACE_RX0:
        return s->rxbuf[off & BUF_MASK];
    default:
        return 0u;
    }
}

static void reg_write(uint8_t space, uint16_t off, uint8_t v)
{
    sock_t *s = fold(&space);

    switch (space) {
    case SPACE_COMMON:
        if (off == C_MR && (v & 0x80u)) {
            chip_reset();                     /* the reset bit self-clears */
        } else if (off < sizeof common && off != C_VERSIONR) {
            common[off] = v;
        } else {
            return;
        }
        break;
    case SPACE_SOCK0:
        if (off == S_CR) {
            if (cr_stuck || (cr_stuck_on != 0u && v == cr_stuck_on)) {
                s->sock[S_CR] = v;
            } else {
                command(s, v);
            }
        } else if (off == S_IR) {
            s->sock[S_IR] = (uint8_t)(s->sock[S_IR] & (uint8_t)~v);
        } else if (off == S_SR || off == S_TX_FSR || off == S_TX_FSR + 1u ||
                   off == S_TX_RD || off == S_TX_RD + 1u ||
                   off == S_RX_RSR || off == S_RX_RSR + 1u ||
                   off == S_RX_WR || off == S_RX_WR + 1u || off >= sizeof s->sock) {
            return;                           /* read-only */
        } else {
            s->sock[off] = v;
        }
        break;
    case SPACE_TX0:
        s->txbuf[off & BUF_MASK] = v;
        break;
    default:
        return;                               /* RX is the chip's to write */
    }
    writes++;
}

/* ----------------------------------------------------------- the framings */

static void w5100_frame(const uint8_t *wr, uint8_t *rd, uint8_t len)
{
    uint16_t addr = (uint16_t)((uint16_t)wr[1] << 8 | wr[2]);
    uint8_t  space = SPACE_NONE;
    uint16_t off = 0u;
    uint8_t  op = wr[0], data = wr[3];

    memset(rd, 0, len);
    if (len != 4u || (op != 0xF0u && op != 0x0Fu)) {
        return;                               /* not a frame: ignored */
    }
    rd[1] = 0x01u;
    rd[2] = 0x02u;
    rd[3] = 0x03u;
    if (addr < 0x0030u) {
        space = SPACE_COMMON; off = addr;
    } else if (addr >= 0x0400u && addr < 0x0500u) {
        space = SPACE_SOCK0;  off = (uint16_t)(addr - 0x0400u);
    } else if (addr >= 0x4000u && addr < 0x4800u) {
        space = SPACE_TX0;    off = (uint16_t)(addr - 0x4000u);
    } else if (addr >= 0x6000u && addr < 0x6800u) {
        space = SPACE_RX0;    off = (uint16_t)(addr - 0x6000u);
    } else if (addr >= 0x0500u && addr < 0x0600u) {
        space = SPACE_SOCK0 + 4u; off = (uint16_t)(addr - 0x0500u);
    } else if (addr >= 0x4800u && addr < 0x5000u) {
        space = SPACE_TX0 + 4u;   off = (uint16_t)(addr - 0x4800u);
    } else if (addr >= 0x6800u && addr < 0x7000u) {
        space = SPACE_RX0 + 4u;   off = (uint16_t)(addr - 0x6800u);
    }
    if (op == 0xF0u) {
        reg_write(space, off, data);
    } else {
        rd[3] = reg_read(space, off);
    }
}

static void w5500_frame(const uint8_t *wr, uint8_t *rd, uint8_t len)
{
    static const uint8_t fdm[4] = { 0u, 1u, 2u, 4u };
    uint16_t off;
    uint8_t  bsb, write, om, n, i;

    memset(rd, 0, len);
    if (len < 4u) {
        return;
    }
    off   = (uint16_t)((uint16_t)wr[0] << 8 | wr[1]);
    bsb   = (uint8_t)(wr[2] >> 3);
    write = (uint8_t)(wr[2] & 0x04u);
    om    = (uint8_t)(wr[2] & 0x03u);
    n     = (uint8_t)(len - 3u);
    if (om != 0u && fdm[om] < n) {
        n = fdm[om];
    }
    for (i = 0u; i < n; i++, off++) {
        uint8_t space = bsb;                  /* reg_read and reg_write fold it */

        if (write) {
            reg_write(space, off, wr[3u + i]);
        } else {
            rd[3u + i] = reg_read(space, off);
        }
    }
}

/* ------------------------------------------------------------- the HAL */

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb)
{
    (void)clkdiv;
    spi_ok = (uint8_t)((mode == 0u || mode == 3u) && lsb == 0u);
    return TFW_OK;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                              uint8_t *rd, uint8_t len)
{
    uint8_t in[64];

    if (len > sizeof in) {
        return TFW_ERR_RANGE;
    }
    memcpy(in, wr, len);                      /* the HAL is in-place safe */
    cs_port = cs->port;
    cs_bit  = cs->bit;
    if (!spi_ok || chip == W5X_MODEL_NONE_LOW) {
        memset(rd, 0x00, len);
        return TFW_OK;
    }
    if (chip == W5X_MODEL_NONE_HIGH) {
        memset(rd, 0xFF, len);
        return TFW_OK;
    }
    if (chip == W5X_MODEL_W5100) {
        w5100_frame(in, rd, len);
    } else {
        w5500_frame(in, rd, len);
    }
    return TFW_OK;
}

static pin_t *pin(const tfw_pinref_t *p, uint8_t create)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++) {
        if (pins[i].used && pins[i].port == p->port && pins[i].bit == p->bit) {
            return &pins[i];
        }
    }
    for (i = 0u; create && i < 8u; i++) {
        if (!pins[i].used) {
            pins[i].used = 1u;
            pins[i].port = p->port;
            pins[i].bit  = p->bit;
            return &pins[i];
        }
    }
    return 0;
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    pin_t *s = pin(p, 1u);

    if (s) {
        s->out = (uint8_t)(mode == TFW_PIN_OUTPUT);
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    pin_t *s = pin(p, 1u);

    if (s) {
        s->high = (uint8_t)(level != 0u);
    }
    return TFW_OK;
}

/* ------------------------------------------------------------- controls */

void w5x_model_peer_accept(void)
{
    sock_t *s = &socks[peer_sn];

    if (s->sock[S_SR] == 0x15u) {
        s->sock[S_SR] = 0x17u;                   /* ESTABLISHED */
    }
}

void w5x_model_peer_close(void)
{
    sock_t *s = &socks[peer_sn];

    if (s->sock[S_SR] == 0x17u) {
        s->sock[S_SR] = 0x1Cu;                   /* CLOSE_WAIT */
    }
}

void w5x_model_peer_send(const uint8_t *b, uint16_t n)
{
    sock_t  *s = &socks[peer_sn];
    uint16_t i;

    for (i = 0u; i < n; i++) {
        s->rxbuf[s->rx_wr & BUF_MASK] = b[i];
        s->rx_wr++;
    }
    put16(s->sock, S_RX_WR, s->rx_wr);
    s->rsr = (uint16_t)(s->rsr + n);
}

uint16_t w5x_model_peer_recv(uint8_t *out, uint16_t cap)
{
    uint16_t n = wire_n < cap ? wire_n : cap;

    memcpy(out, wire, n);
    memmove(wire, wire + n, (size_t)(wire_n - n));
    wire_n = (uint16_t)(wire_n - n);
    return n;
}

uint8_t  w5x_model_sends(void)                 { return sends; }
void     w5x_model_set_sr(uint8_t sr)          { socks[peer_sn].sock[S_SR] = sr; }
void     w5x_model_cr_stuck(uint8_t on)        { cr_stuck = on; }
void     w5x_model_cr_stuck_on(uint8_t cr)     { cr_stuck_on = cr; }
void     w5x_model_size_unstable(uint8_t on)   { unstable = on; }
void     w5x_model_tx_cap(uint16_t cap)        { tx_cap = cap; }
void     w5x_model_rsr_reads(uint16_t v)       { rsr_forced = v; }
void     w5x_model_seed_pointers(uint16_t p)   { seed = p; }
void     w5x_model_retry_reset(uint16_t rtr, uint8_t rcr) { rtr_reset = rtr; rcr_reset = rcr; }
uint8_t  w5x_model_common(uint16_t off)        { return off < sizeof common ? common[off] : 0u; }
uint8_t  w5x_model_sock(uint16_t off)          { return off < sizeof socks[0].sock ? socks[peer_sn].sock[off] : 0u; }
uint16_t w5x_model_sock16(uint16_t off)        { return get16(socks[peer_sn].sock, (uint8_t)off); }
void     w5x_model_socket(uint8_t n)           { peer_sn = n < SOCKETS ? n : 0u; }
void     w5x_model_arp_silent(const uint8_t *ip) { memcpy(arp_silent, ip, 4u); }
uint32_t w5x_model_writes(void)                { return writes; }
uint16_t w5x_model_last_cs_port(void)          { return cs_port; }
uint8_t  w5x_model_last_cs_bit(void)           { return cs_bit; }

uint8_t w5x_model_pin_output(uint16_t port, uint8_t bit)
{
    tfw_pinref_t p = { port, bit };
    const pin_t *s = pin(&p, 0u);

    return s ? s->out : 0u;
}

uint8_t w5x_model_pin_high(uint16_t port, uint8_t bit)
{
    tfw_pinref_t p = { port, bit };
    const pin_t *s = pin(&p, 0u);

    return s ? s->high : 0u;
}
