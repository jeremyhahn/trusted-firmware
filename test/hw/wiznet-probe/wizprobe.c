/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Ethernet controller probe -- see wizprobe.h. A probe, not the driver.
 *
 * The three WIZnet framings are tried in turn against the same chip-select.
 * Each is harmless to the other two parts: the only writes are the W5100's
 * (opcode 0xF0), and a W5500 or W5200 reads F0 00 as an address and the next
 * byte as a read control word, so neither is written to. The ENC28J60's soft
 * reset (0xFF) is sent only once nothing WIZnet answered.
 */
#include "wizprobe.h"

#define W5100_OP_WRITE 0xF0u
#define W5100_OP_READ  0x0Fu
#define W5100_MR       0x0000u
#define W5100_RTR      0x0017u
#define W5100_RCR      0x0019u
#define W5100_MR_RST   0x80u

#define W5500_VERSIONR 0x0039u
#define W5500_CTRL_COMMON_READ 0x00u   /* BSB 00000, RWB 0, OM 00 */
#define W5500_VERSION  0x04u

#define W5200_VERSIONR 0x001Fu
#define W5200_VERSION  0x03u

#define ENC_OP_RCR     0x00u
#define ENC_OP_BFS     0x80u
#define ENC_OP_BFC     0xA0u
#define ENC_OP_SRC     0xFFu
#define ENC_ECON1      0x1Fu
#define ENC_EREVID     0x12u           /* bank 3 */
#define ENC_BSEL_MASK  0x03u

/* Busy-waits on the HAL's millisecond tick. Acceptable in a probe image that
 * does nothing else; the driver must schedule instead. */
static void wait_ms(tfw_ms_t ms)
{
    tfw_ms_t start = tfw_hal_millis();

    while ((tfw_ms_t)(tfw_hal_millis() - start) < ms) {
    }
}

static tfw_status_t w5100_frame(const tfw_pinref_t *cs, uint8_t op,
                                uint16_t addr, uint8_t data, uint8_t rx[4])
{
    uint8_t tx[4];

    tx[0] = op;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)addr;
    tx[3] = data;
    return tfw_hal_spi_xfer(cs, tx, rx, 4u);
}

static tfw_status_t w5100_read(const tfw_pinref_t *cs, uint16_t addr,
                               uint8_t *out)
{
    uint8_t      rx[4];
    tfw_status_t st = w5100_frame(cs, W5100_OP_READ, addr, 0x00u, rx);

    *out = rx[3];
    return st;
}

static tfw_status_t w5100_write(const tfw_pinref_t *cs, uint16_t addr,
                                uint8_t val)
{
    uint8_t rx[4];

    return w5100_frame(cs, W5100_OP_WRITE, addr, val, rx);
}

static tfw_status_t probe_w5500(const tfw_pinref_t *cs, wizprobe_result_t *r)
{
    const uint8_t tx[4] = { (uint8_t)(W5500_VERSIONR >> 8),
                            (uint8_t)W5500_VERSIONR,
                            W5500_CTRL_COMMON_READ, 0x00u };

    return tfw_hal_spi_xfer(cs, tx, r->w5500_rx, 4u);
}

static tfw_status_t probe_w5200(const tfw_pinref_t *cs, wizprobe_result_t *r)
{
    const uint8_t tx[5] = { (uint8_t)(W5200_VERSIONR >> 8),
                            (uint8_t)W5200_VERSIONR,
                            0x00u, 0x01u, 0x00u };

    return tfw_hal_spi_xfer(cs, tx, r->w5200_rx, 5u);
}

/* The MR/RTR pattern. The W5100 has no version register, so it is recognised
 * by what a reset leaves behind and by a register that holds what is written
 * to it. RTR is put back to its reset value afterwards. */
static tfw_status_t probe_w5100(const tfw_pinref_t *cs, wizprobe_result_t *r)
{
    tfw_status_t st;
    uint8_t      rx[4];

    st = w5100_frame(cs, W5100_OP_WRITE, W5100_MR, W5100_MR_RST,
                     r->w5100_reset_rx);
    if (st != TFW_OK) {
        return st;
    }
    wait_ms(10u);
    st = w5100_read(cs, W5100_MR, &r->w5100_mr);
    if (st == TFW_OK) {
        st = w5100_frame(cs, W5100_OP_READ, W5100_RTR, 0x00u, rx);
        r->w5100_rtr_rx[0] = rx[0];
        r->w5100_rtr_rx[1] = rx[1];
        r->w5100_rtr_rx[2] = rx[2];
        r->w5100_rtr_rx[3] = rx[3];
        r->w5100_rtr[0]    = rx[3];
    }
    if (st == TFW_OK) {
        st = w5100_read(cs, (uint16_t)(W5100_RTR + 1u), &r->w5100_rtr[1]);
    }
    if (st == TFW_OK) {
        st = w5100_read(cs, W5100_RCR, &r->w5100_rcr);
    }
    if (st == TFW_OK) {
        st = w5100_write(cs, W5100_RTR, 0x0Bu);
    }
    if (st == TFW_OK) {
        st = w5100_write(cs, (uint16_t)(W5100_RTR + 1u), 0xB8u);
    }
    if (st == TFW_OK) {
        st = w5100_read(cs, W5100_RTR, &r->w5100_rtr_wb[0]);
    }
    if (st == TFW_OK) {
        st = w5100_read(cs, (uint16_t)(W5100_RTR + 1u), &r->w5100_rtr_wb[1]);
    }
    if (st == TFW_OK) {
        st = w5100_write(cs, W5100_RTR, 0x07u);
    }
    if (st == TFW_OK) {
        st = w5100_write(cs, (uint16_t)(W5100_RTR + 1u), 0xD0u);
    }
    return st;
}

static tfw_status_t probe_enc28j60(const tfw_pinref_t *cs, wizprobe_result_t *r)
{
    const uint8_t src[1]     = { ENC_OP_SRC };
    const uint8_t bank3[2]   = { (uint8_t)(ENC_OP_BFS | ENC_ECON1), ENC_BSEL_MASK };
    const uint8_t econ1[2]   = { (uint8_t)(ENC_OP_RCR | ENC_ECON1), 0x00u };
    const uint8_t erevid[2]  = { (uint8_t)(ENC_OP_RCR | ENC_EREVID), 0x00u };
    const uint8_t bank0[2]   = { (uint8_t)(ENC_OP_BFC | ENC_ECON1), ENC_BSEL_MASK };
    uint8_t       rx[2];
    tfw_status_t  st;

    r->enc_tried = 1u;
    st = tfw_hal_spi_xfer(cs, src, rx, 1u);
    if (st != TFW_OK) {
        return st;
    }
    /* Errata: CLKRDY is not to be trusted after a soft reset; wait instead. */
    wait_ms(2u);
    st = tfw_hal_spi_xfer(cs, bank3, rx, 2u);
    if (st == TFW_OK) {
        st = tfw_hal_spi_xfer(cs, econ1, rx, 2u);
        r->enc_econ1 = rx[1];
    }
    if (st == TFW_OK) {
        st = tfw_hal_spi_xfer(cs, erevid, rx, 2u);
        r->enc_erevid = rx[1];
    }
    if (st == TFW_OK) {
        st = tfw_hal_spi_xfer(cs, bank0, rx, 2u);
    }
    return st;
}

static uint8_t w5100_matches(const wizprobe_result_t *r)
{
    return (uint8_t)(r->w5100_mr == 0x00u
                     && r->w5100_rtr[0] == 0x07u && r->w5100_rtr[1] == 0xD0u
                     && r->w5100_rcr == 0x08u
                     && r->w5100_rtr_wb[0] == 0x0Bu
                     && r->w5100_rtr_wb[1] == 0xB8u);
}

/* 1 when every byte the WIZnet framings clocked back is the same idle level,
 * 0x00 or 0xFF: MISO never moved, so nothing answered. */
static uint8_t miso_silent(const wizprobe_result_t *r)
{
    const uint8_t *p   = r->w5500_rx;
    const uint8_t *end = &r->w5100_rtr_wb[1];
    uint8_t        lvl = *p;

    if (lvl != 0x00u && lvl != 0xFFu) {
        return 0u;
    }
    for (; p <= end; p++) {
        if (*p != lvl) {
            return 0u;
        }
    }
    return 1u;
}

tfw_status_t wizprobe_run(const tfw_pinref_t *cs, wizprobe_result_t *r)
{
    tfw_status_t st;
    uint8_t     *p   = (uint8_t *)r;
    uint8_t     *end = p + sizeof *r;

    for (; p < end; p++) {
        *p = 0u;
    }
    st = probe_w5500(cs, r);
    if (st == TFW_OK) {
        st = probe_w5200(cs, r);
    }
    if (st == TFW_OK) {
        st = probe_w5100(cs, r);
    }
    if (st != TFW_OK) {
        return st;
    }

    if (r->w5500_rx[3] == W5500_VERSION) {
        r->chip = WIZPROBE_W5500;
    } else if (w5100_matches(r)) {
        r->chip = WIZPROBE_W5100;
    } else if (r->w5200_rx[4] == W5200_VERSION) {
        r->chip = WIZPROBE_W5200;
    } else {
        st = probe_enc28j60(cs, r);
        if ((r->enc_econ1 & ENC_BSEL_MASK) == ENC_BSEL_MASK
            && r->enc_erevid != 0x00u && r->enc_erevid != 0xFFu) {
            r->chip = WIZPROBE_ENC28J60;
        } else if (miso_silent(r)) {
            r->chip = WIZPROBE_NONE;
        } else {
            r->chip = WIZPROBE_UNKNOWN;
        }
    }
    return st;
}

const char *wizprobe_chip_name(wizprobe_chip_t chip)
{
    switch (chip) {
    case WIZPROBE_W5100:    return "W5100";
    case WIZPROBE_W5200:    return "W5200";
    case WIZPROBE_W5500:    return "W5500";
    case WIZPROBE_ENC28J60: return "ENC28J60";
    case WIZPROBE_NONE:     return "none (MISO idle)";
    default:                return "unknown";
    }
}
