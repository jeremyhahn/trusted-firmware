/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "optiga_i2c.h"
#include "../../caps/bus.h"
#include "tfw/hal.h"

/*
 * One buffer: the register address 0x80 and then a whole frame. It holds the
 * frame being sent and then the frame being read; a window of one (§3.2)
 * means the two are never live at once. A repeat rebuilds the sent frame from
 * the caller's APDU rather than keeping a second copy, and control frames,
 * which are five bytes, are built on the stack, so acknowledging a received
 * frame leaves its packet here for the caller to take.
 */
static uint8_t buf[1u + TFW_OPTIGA_DATA_REG_LEN];

/* Packet bytes start after the register address, FCTR and LEN. */
#define PKT 4u

static uint8_t frame_max;  /* the DATA_REG_LEN the element accepted; 0 until open */
static uint8_t tx_nr;      /* the number of the last data frame sent */
static uint8_t rx_nr;      /* the number of the last data frame received in sequence */

/* What one wait for a frame produced. Zero is nothing at all within the wait. */
#define EV_ACK   0x01u  /* our frame acknowledged, by a control frame or piggybacked */
#define EV_NAK   0x02u  /* our frame not acknowledged: send it again */
#define EV_DATA  0x04u  /* a data frame in sequence; its packet is at buf[PKT] */
#define EV_SKIP  0x08u  /* a frame that was answered or discarded and means nothing more */

/* The element's address is this transport's: a build that links it refuses
 * the address to I2C_XFER (src/caps/bus.h), in place of bus_held_none.c. */
uint8_t tfw_bus_i2c_held(uint8_t addr)
{
    return (uint8_t)(addr == TFW_OPTIGA_ADDR);
}

uint16_t tfw_optiga_fcs(const uint8_t *data, uint16_t len)
{
    uint16_t seed = 0u;

    /* §8.1.2, step for step. Every intermediate fits 16 bits, so this is the
     * same arithmetic on an AVR, whose int is 16 bits, as on the host. */
    while (len--) {
        uint16_t h1 = (uint16_t)((seed ^ *data++) & 0xFFu);
        uint16_t h2 = (uint16_t)(h1 & 0x0Fu);
        uint16_t h3 = (uint16_t)((uint16_t)(h2 << 4) ^ h1);
        uint16_t h4 = (uint16_t)(h3 >> 4);
        seed = (uint16_t)((uint16_t)((uint16_t)((uint16_t)((uint16_t)(h3 << 1) ^ h4) << 4) ^ h2) << 3)
               ^ h4 ^ (uint16_t)(seed >> 8);
    }
    return seed;
}

/*
 * One access to the element, repeated while it NACKs (the late acknowledge,
 * §2 Figures 2-2 and 2-3). Every attempt is preceded by the guard time, which
 * is what Table 8-2 asks between a read and the next write, and paces the
 * repeat. Any other bus failure is returned at once: only a NACK means busy.
 */
static tfw_status_t bus_access(const uint8_t *wr, uint8_t wlen, uint8_t *rd, uint8_t rlen)
{
    uint8_t      tries = TFW_OPTIGA_LATE_ACK_TRIES;
    tfw_status_t st;

    do {
        tfw_hal_delay_us(TFW_OPTIGA_GUARD_US);
        st = tfw_hal_i2c_xfer(TFW_OPTIGA_ADDR, wr, wlen, rd, rlen);
    } while (st == TFW_ERR_NACK && --tries != 0u);
    return st;
}

/* A register read is a write of its address, a STOP, and a read (Figure 2-3):
 * two accesses, not a repeated start, which the element may not support
 * (I2C_STATE's REP_START). */
static tfw_status_t reg_read(uint8_t reg, uint8_t *out, uint8_t n)
{
    tfw_status_t st = bus_access(&reg, 1u, 0, 0u);

    return st != TFW_OK ? st : bus_access(0, 0u, out, n);
}

/* Seals the frame in f[1..] -- f[0] is the register address -- and writes it
 * to DATA. LEN's high byte is always zero: no frame here is over 254 bytes. */
static tfw_status_t send(uint8_t *f, uint8_t fctr, uint8_t plen)
{
    uint16_t fcs;

    f[0] = TFW_OPTIGA_REG_DATA;
    f[1] = fctr;
    f[2] = 0u;
    f[3] = plen;
    fcs = tfw_optiga_fcs(&f[1], (uint16_t)(3u + plen));
    f[PKT + plen]      = (uint8_t)(fcs >> 8);
    f[PKT + plen + 1u] = (uint8_t)(fcs & 0xFFu);
    return bus_access(f, (uint8_t)(1u + TFW_OPTIGA_FRAME_OVERHEAD + plen), 0, 0u);
}

static tfw_status_t send_control(uint8_t seq, uint8_t nr)
{
    uint8_t f[1u + TFW_OPTIGA_FRAME_OVERHEAD];

    return send(f, (uint8_t)(TFW_OPTIGA_FCTR_CONTROL | seq | nr), 0u);
}

/* The special control frame that resets both frame counters (§3.2), after
 * which the first data frame each side sends is number 0. */
static tfw_status_t resync(void)
{
    tx_nr = TFW_OPTIGA_FRAME_NR;
    rx_nr = TFW_OPTIGA_FRAME_NR;
    return send_control(TFW_OPTIGA_SEQ_RESYNC, 0u);
}

/*
 * Waits up to `ms` for one frame from the element and handles what the
 * receiver must (Table 3-2): a corrupt frame is answered with a NAK naming the
 * frame expected, an out-of-sequence data frame with an ACK for the last one
 * taken, an in-sequence one with an ACK for itself. `*bad` records why the
 * last frame was refused, for the caller that runs out of patience.
 */
static tfw_status_t wait_frame(uint16_t ms, uint8_t *ev, uint8_t *plen, tfw_status_t *bad)
{
    uint8_t      state[4];
    uint16_t     polls = (uint16_t)(ms + 1u);
    uint16_t     len;
    uint8_t      fctr, seq, nr;
    uint16_t     flen;
    tfw_status_t st;

    *ev = 0u;
    for (;;) {
        st = reg_read(TFW_OPTIGA_REG_I2C_STATE, state, (uint8_t)sizeof state);
        if (st != TFW_OK) {
            return st;
        }
        len = (uint16_t)(((uint16_t)state[2] << 8) | state[3]);
        if ((state[0] & TFW_OPTIGA_STATE_RESP_RDY) && len != 0u) {
            break;
        }
        if (--polls == 0u) {
            return TFW_OK;
        }
    }

    /* A length no frame can have, or more than the buffer holds (a layer 1
     * overflow): nothing is read, and the NAK asks for the frame again. */
    if (len < TFW_OPTIGA_FRAME_OVERHEAD || len > frame_max) {
        *bad = TFW_ERR_FRAMING;
        *ev  = EV_SKIP;
        return send_control(TFW_OPTIGA_SEQ_NAK, (uint8_t)((rx_nr + 1u) & TFW_OPTIGA_FRAME_NR));
    }
    st = reg_read(TFW_OPTIGA_REG_DATA, &buf[1], (uint8_t)len);
    if (st != TFW_OK) {
        return st;
    }

    fctr = buf[1];
    seq  = (uint8_t)(fctr & TFW_OPTIGA_FCTR_SEQ);
    nr   = (uint8_t)(fctr & TFW_OPTIGA_FRAME_NR);
    flen = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    *ev  = EV_SKIP;

    if (tfw_optiga_fcs(&buf[1], (uint16_t)(len - 2u))
            != (uint16_t)(((uint16_t)buf[len - 1u] << 8) | buf[len])) {
        *bad = TFW_ERR_CRC;
    } else if (flen != (uint16_t)(len - TFW_OPTIGA_FRAME_OVERHEAD)
               || (fctr & TFW_OPTIGA_FCTR_RFU) || seq == TFW_OPTIGA_SEQ_RFU) {
        *bad = TFW_ERR_FRAMING;
    } else if (fctr & TFW_OPTIGA_FCTR_CONTROL) {
        /* Control frames carry no packet; one that does is discarded, not
         * NAKed (Table 3-2). One that resets the counters is obeyed, and
         * whatever was in flight is lost with them. */
        if (flen != 0u) {
            return TFW_OK;
        }
        if (seq == TFW_OPTIGA_SEQ_RESYNC) {
            tx_nr = TFW_OPTIGA_FRAME_NR;
            rx_nr = TFW_OPTIGA_FRAME_NR;
            return TFW_ERR_FRAMING;
        }
        /* An ACK or NAK for a frame not in flight is discarded (Table 3-3). */
        if (nr == tx_nr) {
            *ev = (uint8_t)(seq == TFW_OPTIGA_SEQ_ACK ? EV_ACK : EV_NAK);
        }
        return TFW_OK;
    } else if (flen == 0u || seq == TFW_OPTIGA_SEQ_RESYNC) {
        /* A data frame with no PCTR, or with the resync sequence that is for
         * control frames only. */
        *bad = TFW_ERR_FRAMING;
    } else {
        if (nr == tx_nr) {
            *ev = (uint8_t)(seq == TFW_OPTIGA_SEQ_ACK ? EV_ACK : EV_NAK);
        }
        nr = (uint8_t)((fctr >> TFW_OPTIGA_FCTR_FRNR_SHIFT) & TFW_OPTIGA_FRAME_NR);
        if (nr != ((rx_nr + 1u) & TFW_OPTIGA_FRAME_NR)) {
            /* Already taken -- our ACK was lost -- or not yet expected:
             * acknowledge the last one taken and drop this. */
            *ev = (uint8_t)(*ev | EV_SKIP);
            return send_control(TFW_OPTIGA_SEQ_ACK, rx_nr);
        }
        rx_nr = nr;
        *plen = (uint8_t)flen;
        *ev   = (uint8_t)((*ev & EV_ACK) | EV_DATA);
        return send_control(TFW_OPTIGA_SEQ_ACK, rx_nr);
    }

    return send_control(TFW_OPTIGA_SEQ_NAK, (uint8_t)((rx_nr + 1u) & TFW_OPTIGA_FRAME_NR));
}

/*
 * Sends one packet -- PCTR and `dlen` bytes -- as the next data frame, and
 * repeats it on a NAK or a silent `wait_ms` until it is acknowledged (Table
 * 3-3), at most TFW_OPTIGA_TRANS_REPEAT times. A frame that is refused or
 * discarded while waiting is not a reason to repeat; it is bounded the same.
 */
static tfw_status_t send_packet(const uint8_t *data, uint8_t dlen, uint8_t pctr,
                                uint16_t wait_ms, uint8_t *ev, uint8_t *plen)
{
    uint8_t      sends, waits;
    tfw_status_t bad = TFW_ERR_TIMEOUT;

    tx_nr = (uint8_t)((tx_nr + 1u) & TFW_OPTIGA_FRAME_NR);
    for (sends = 0u; sends <= TFW_OPTIGA_TRANS_REPEAT; sends++) {
        tfw_status_t st;

        buf[PKT] = pctr;
        if (dlen) {
            memcpy(&buf[PKT + 1u], data, dlen);
        }
        st = send(buf, (uint8_t)((tx_nr << TFW_OPTIGA_FCTR_FRNR_SHIFT) | rx_nr),
                  (uint8_t)(dlen + 1u));
        if (st != TFW_OK) {
            return st;
        }
        for (waits = 0u; waits <= TFW_OPTIGA_TRANS_REPEAT; waits++) {
            st = wait_frame(wait_ms, ev, plen, &bad);
            if (st != TFW_OK) {
                return st;
            }
            if (*ev & EV_ACK) {
                return TFW_OK;
            }
            if (*ev & EV_DATA) {
                /* An answer that does not acknowledge the question. */
                return TFW_ERR_FRAMING;
            }
            if (*ev == 0u || (*ev & EV_NAK)) {
                break;
            }
        }
    }
    return bad;
}

/*
 * Appends the packet at buf[PKT] to the response, checking the chain (§5.1,
 * §5.2). `*chained` is set inside a chain. `*done` is set by its end.
 * `*refuse` is set where §5.2 asks the receiver to tell the element its chain
 * was broken.
 */
static tfw_status_t take(uint8_t plen, uint8_t *rsp, uint16_t rcap, uint16_t *rlen,
                         uint8_t *chained, uint8_t *done, uint8_t *refuse)
{
    uint8_t pctr  = buf[PKT];
    uint8_t chain = (uint8_t)(pctr & TFW_OPTIGA_PCTR_CHAIN);
    uint8_t n     = (uint8_t)(plen - 1u);
    uint8_t full  = (uint8_t)(frame_max - TFW_OPTIGA_FRAME_OVERHEAD);

    if (pctr & (TFW_OPTIGA_PCTR_CHAN | TFW_OPTIGA_PCTR_PRESENCE)) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (chain == TFW_OPTIGA_CHAIN_ERROR) {
        return TFW_ERR_FRAMING;  /* the element refused our chain */
    }
    *refuse = 1u;
    switch (chain) {
    case TFW_OPTIGA_CHAIN_NONE:
        if (*chained) {
            return TFW_ERR_FRAMING;
        }
        *done = 1u;
        break;
    case TFW_OPTIGA_CHAIN_FIRST:
        if (*chained || plen != full) {
            return TFW_ERR_FRAMING;
        }
        *chained = 1u;
        break;
    case TFW_OPTIGA_CHAIN_MID:
        if (!*chained || plen != full) {
            return TFW_ERR_FRAMING;
        }
        break;
    case TFW_OPTIGA_CHAIN_LAST:
        if (!*chained || plen < 2u) {
            return TFW_ERR_FRAMING;
        }
        *done = 1u;
        break;
    default:
        return TFW_ERR_FRAMING;
    }
    *refuse = 0u;
    if (n > rcap - *rlen) {
        return TFW_ERR_TOO_LONG;
    }
    memcpy(&rsp[*rlen], &buf[PKT + 1u], n);
    *rlen = (uint16_t)(*rlen + n);
    return TFW_OK;
}

tfw_status_t tfw_optiga_open(void)
{
    uint8_t      w[3] = { TFW_OPTIGA_REG_DATA_REG_LEN, 0u, TFW_OPTIGA_DATA_REG_LEN };
    uint8_t      r[2];
    tfw_status_t st;

    frame_max = 0u;
    (void)tfw_hal_i2c_init(TFW_OPTIGA_I2C_HZ);
    st = bus_access(w, (uint8_t)sizeof w, 0, 0u);
    if (st == TFW_OK) {
        st = reg_read(TFW_OPTIGA_REG_DATA_REG_LEN, r, (uint8_t)sizeof r);
    }
    if (st != TFW_OK) {
        return st;
    }
    if (r[0] != 0u || r[1] < TFW_OPTIGA_DATA_REG_LEN_MIN || r[1] > TFW_OPTIGA_DATA_REG_LEN) {
        return TFW_ERR_RANGE;
    }
    st = resync();
    if (st == TFW_OK) {
        frame_max = r[1];
    }
    return st;
}

tfw_status_t tfw_optiga_transceive(const uint8_t *apdu, uint16_t alen,
                                   uint8_t *rsp, uint16_t rcap, uint16_t *rlen,
                                   uint16_t exec_ms)
{
    /* Data bytes per packet: the largest packet less its PCTR. */
    uint8_t      room = (uint8_t)(frame_max - TFW_OPTIGA_FRAME_OVERHEAD - 1u);
    uint8_t      n, pctr, ev = 0u, plen = 0u;
    uint8_t      chained = 0u, done = 0u, refuse = 0u, waits = 0u;
    uint16_t     off = 0u;
    tfw_status_t bad = TFW_ERR_TIMEOUT;
    tfw_status_t st;

    if (frame_max == 0u) {
        return TFW_ERR_INTERNAL;
    }
    if (alen == 0u) {
        return TFW_ERR_RANGE;
    }
    *rlen = 0u;

    /* The request: one packet, or a chain whose every packet but the last is
     * full (§5.1). The last one waits for the command to run. */
    do {
        n    = (uint8_t)(alen - off > room ? room : alen - off);
        pctr = (uint8_t)(off == 0u
                         ? (n == alen ? TFW_OPTIGA_CHAIN_NONE : TFW_OPTIGA_CHAIN_FIRST)
                         : (off + n == alen ? TFW_OPTIGA_CHAIN_LAST : TFW_OPTIGA_CHAIN_MID));
        st = send_packet(&apdu[off], n, pctr,
                         off + n == alen ? exec_ms : TFW_OPTIGA_TRANS_TIMEOUT_MS, &ev, &plen);
        if (st != TFW_OK) {
            goto fail;
        }
        if ((ev & EV_DATA) && off + n != alen) {
            st = TFW_ERR_FRAMING;  /* an answer before the question was whole */
            goto fail;
        }
        off = (uint16_t)(off + n);
    } while (off < alen);

    /* The response: the first packet may already have come with the
     * acknowledge. Silence or refused frames are bounded; each packet taken
     * restarts the count, because the element is making progress. */
    for (;;) {
        if (ev & EV_DATA) {
            st = take(plen, rsp, rcap, rlen, &chained, &done, &refuse);
            if (st != TFW_OK) {
                goto fail;
            }
            if (done) {
                return TFW_OK;
            }
            waits = 0u;
        } else if (++waits > TFW_OPTIGA_TRANS_REPEAT + 1u) {
            st = bad;
            goto fail;
        }
        st = wait_frame(chained ? TFW_OPTIGA_TRANS_TIMEOUT_MS : exec_ms, &ev, &plen, &bad);
        if (st != TFW_OK) {
            goto fail;
        }
    }

fail:
    if (refuse) {
        /* §5.2: a packet of PCTR alone, CHAIN 111, so the element can drop
         * the chain; its own failure changes nothing about ours. */
        (void)send_packet(0, 0u, TFW_OPTIGA_CHAIN_ERROR, TFW_OPTIGA_TRANS_TIMEOUT_MS, &ev, &plen);
    }
    (void)resync();
    return st;
}
