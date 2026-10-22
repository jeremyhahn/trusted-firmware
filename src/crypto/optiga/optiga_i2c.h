/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OPTIGA Trust M transport: one APDU in, one APDU out, over the IFX I2C
 * protocol -- SPEC row optiga-trust-m.
 *
 * The protocol is Infineon's *IFX I2C Protocol, Protocol Specification*,
 * Revision 2.03 (2020-09-22), listed in docs/reference/README.md. Every
 * section number below is that document's. This file implements its layers
 * 1 to 4 for one master:
 *
 *   physical   the register set and the late acknowledge (§2, §2.1)
 *   data link  frames, FCS, numbering, ACK/NAK and resync (§3)
 *   network    channel 0 only, the single-channel case (§4)
 *   transport  chaining (§5)
 *
 * and not layer 6, the shielded connection (§6): a packet that announces it is
 * refused. Whether this tier needs it is the onboarding ADR's open question 4
 * (docs/multi-arch/adr-optiga-trust-m-onboarding.md), and it needs a platform
 * binding secret written into the element, which no lane may do.
 *
 * The window size is 1 (§3.2 allows 1 or 2): one frame in flight, so one
 * frame buffer. The element's command set -- what the APDUs say -- is not
 * here. That is the provider's, the same split atecc.c and provider.c keep for
 * the ATECC608: this file moves bytes and checks checksums, and that is what
 * lets the protocol be tested against recorded frames with no element.
 *
 * The implementation is ours (L2: Infineon's host library is not linked,
 * the onboarding ADR's open question 8). Where the specification leaves a
 * value to the product, the value Infineon's own host library uses for the
 * OPTIGA Trust M is taken and cited as that; each is a fact to confirm on the
 * bench.
 */
#ifndef TFW_OPTIGA_I2C_H
#define TFW_OPTIGA_I2C_H

#include <stdint.h>
#include "tfw/config.h"
#include "tfw/status.h"

/* The element's 7-bit base address. §2.1 makes 0x20 the default "if not
 * differently defined by a particular project"; the OPTIGA Trust M is such a
 * project and answers at 0x30 (the host library's IFX_I2C_BASE_ADDR,
 * optiga-trust-m ifx_i2c_config.h). To confirm against the board. */
#define TFW_OPTIGA_ADDR    0x30u

/* The bus rate. Sm and Fm are both in the element's default I2C_MODE (Table
 * 2-3, "SM & Fm (fab out default)"), so the board's element rate serves. */
#define TFW_OPTIGA_I2C_HZ  TFW_ELEMENT_I2C_HZ

/* Registers (Table 2-1). Multi-byte registers are big-endian (§2.1 Note 1). */
#define TFW_OPTIGA_REG_DATA          0x80u
#define TFW_OPTIGA_REG_DATA_REG_LEN  0x81u
#define TFW_OPTIGA_REG_I2C_STATE     0x82u

/* I2C_STATE bits 31:24, its first byte on the wire (Table 2-4). Bits 15:0 are
 * the length of the frame waiting to be read. */
#define TFW_OPTIGA_STATE_BUSY        0x80u
#define TFW_OPTIGA_STATE_RESP_RDY    0x40u

/*
 * The DATA register length this firmware asks for at open, which is the
 * largest frame either side sends (§2.1 Table 2-1: MAX_PACKET_SIZE =
 * DATA_REG_LEN - 5). The specification allows 0x0010 to 0xFFFF; the element
 * may answer with less, and its answer is what is used.
 *
 * 64, not the host library's 277, because the frame buffer is static RAM on a
 * part with 8 KB and the HAL moves at most 255 bytes per transaction. A
 * longer APDU is chained (§5.1); that costs bus time, not RAM.
 */
#define TFW_OPTIGA_DATA_REG_LEN      64u
#define TFW_OPTIGA_DATA_REG_LEN_MIN  16u
_Static_assert(TFW_OPTIGA_DATA_REG_LEN >= TFW_OPTIGA_DATA_REG_LEN_MIN &&
               TFW_OPTIGA_DATA_REG_LEN <= 254u,
               "a frame and its register address must fit one HAL transaction");

/* A frame is FCTR, LEN (2), the packet, FCS (2): §3.1 Figure 3-1. */
#define TFW_OPTIGA_FRAME_OVERHEAD    5u

/* FCTR (Table 3-1; every value in Table 8-1). */
#define TFW_OPTIGA_FCTR_CONTROL      0x80u  /* FTYPE: control frame */
#define TFW_OPTIGA_FCTR_SEQ          0x60u  /* SEQCTR */
#define TFW_OPTIGA_SEQ_ACK           0x00u
#define TFW_OPTIGA_SEQ_NAK           0x20u
#define TFW_OPTIGA_SEQ_RESYNC        0x40u  /* control frames only */
#define TFW_OPTIGA_SEQ_RFU           0x60u
#define TFW_OPTIGA_FCTR_RFU          0x10u
#define TFW_OPTIGA_FCTR_FRNR_SHIFT   2u
#define TFW_OPTIGA_FRAME_NR          0x03u  /* FRNR and ACKNR are modulo 4 */

/* PCTR, the packet's first byte: CHAN (Table 3-4), PRESENCE (Table 6-1),
 * CHAIN (Table 5-1). */
#define TFW_OPTIGA_PCTR_CHAN         0xF0u
#define TFW_OPTIGA_PCTR_PRESENCE     0x08u
#define TFW_OPTIGA_PCTR_CHAIN        0x07u
#define TFW_OPTIGA_CHAIN_NONE        0x00u
#define TFW_OPTIGA_CHAIN_FIRST       0x01u
#define TFW_OPTIGA_CHAIN_MID         0x02u
#define TFW_OPTIGA_CHAIN_LAST        0x04u
#define TFW_OPTIGA_CHAIN_ERROR       0x07u

/*
 * Timing.
 *
 * GUARD_TIME: 500 us from a read's STOP to the next write's START on the
 * element, the default of Table 8-2. Waited before every access, which also
 * paces the late-acknowledge poll; one I2C_STATE poll is two accesses, so
 * about 1 ms.
 *
 * LATE_ACK_TRIES: how many times an access NACKed by a busy element is
 * repeated (§2, Figures 2-2 and 2-3). The specification sets no bound; 200 is
 * the host library's PL_POLLING_MAX_CNT.
 *
 * TRANS_TIMEOUT: how long a sent frame waits for its acknowledge before it is
 * repeated (Table 8-2, 1 to 1000 ms; product-defined). 10 ms is the host
 * library's PL_TRANS_TIMEOUT_MS. The last packet of a request waits the
 * command's execution time instead, which the caller passes.
 *
 * TRANS_REPEAT: repetitions before the link is resynchronised (Table 8-2, 1
 * to 4). 3 is the host library's DL_TRANS_REPEAT.
 */
#define TFW_OPTIGA_GUARD_US          500u
#define TFW_OPTIGA_LATE_ACK_TRIES    200u
#define TFW_OPTIGA_TRANS_TIMEOUT_MS  10u
#define TFW_OPTIGA_TRANS_REPEAT      3u

/*
 * The frame checksum (§3.3): CRC with the CCITT polynomial x^16 + x^12 + x^5
 * + 1, seed 0, over FCTR, LEN and the packet; this is §8.1.2's "more efficient
 * implementation", which the tests pin against §8.1.1's reference one.
 *
 * ON THE WIRE THE HIGH BYTE OF THIS VALUE GOES FIRST. §3.3 reads "low byte ||
 * high byte"; Infineon's host library, which is what the silicon has been
 * talking to, sends bits 15:8 first and checks them first
 * (ifx_i2c_data_link_layer.c), and the frames it puts on the bus -- the
 * acknowledge 80 00 00 0C EC -- are what this firmware must interoperate
 * with. The test vectors are written that way. To confirm on the bench.
 */
uint16_t     tfw_optiga_fcs(const uint8_t *data, uint16_t len);

/*
 * Opens the link: sets the bus rate, asks for TFW_OPTIGA_DATA_REG_LEN and
 * adopts what the element accepted (§2.1 Table 2-1 recommends reading it
 * back), then resynchronises both frame counters (§3.2), so a host that reset
 * while the element did not starts in step.
 *
 * TFW_ERR_RANGE if the element's DATA_REG_LEN is outside 16 to ours; any bus
 * failure as the HAL returned it.
 */
tfw_status_t tfw_optiga_open(void);

/*
 * Sends one command APDU and returns the response APDU (§7).
 *
 * `exec_ms` bounds the wait for the response after the request's last packet
 * is sent: the command's maximum execution time, which belongs to the
 * provider. `*rlen` is the response length on TFW_OK.
 *
 *   TFW_ERR_INTERNAL     not opened
 *   TFW_ERR_RANGE        an empty APDU
 *   TFW_ERR_TIMEOUT      a frame never acknowledged, or no response, after
 *                        TFW_OPTIGA_TRANS_REPEAT repeats
 *   TFW_ERR_CRC          the element's frames kept failing their FCS
 *   TFW_ERR_FRAMING      a broken frame, chain or frame sequence
 *   TFW_ERR_TOO_LONG     the response is longer than `rcap`
 *   TFW_ERR_UNSUPPORTED  a packet on another channel, or with the
 *                        presentation layer
 *
 * On any of these but the first two the link is resynchronised before
 * returning, so the next call starts clean.
 */
tfw_status_t tfw_optiga_transceive(const uint8_t *apdu, uint16_t alen,
                                   uint8_t *rsp, uint16_t rcap, uint16_t *rlen,
                                   uint16_t exec_ms);

#endif /* TFW_OPTIGA_I2C_H */
