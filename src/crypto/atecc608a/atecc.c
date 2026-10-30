/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "atecc.h"
#include "tfw/hal.h"
#include "../../core/secure_zero.h"

/*
 * One buffer, used for the command and then for the response.
 *
 * Reusing it is safe because the whole transaction is synchronous: the command
 * has been written to the bus before a single response byte exists. Two buffers
 * would cost another 71 bytes of SRAM on a part that has 2 KB, to separate two
 * things that are never live at once.
 */
/* Eight bytes of overhead, not seven: the word address, the count, the opcode,
 * both parameter bytes and two CRC bytes. The count field itself counts only
 * seven of those -- it does not count the word address, which is addressing
 * rather than payload -- and using the count's seven to size the buffer is a
 * one-byte overrun at the largest command. cppcheck caught it. */
static uint8_t buf[TFW_ATECC_MAX_DATA + 8u];

uint16_t tfw_atecc_crc16(const uint8_t *data, uint8_t len)
{
    unsigned int reg = 0u;
    uint8_t      i;

    for (i = 0u; i < len; i++) {
        uint8_t mask;
        /* Least-significant bit first, which is the one thing about this CRC
         * that differs from the frame CRC three directories away and the one
         * thing a reader will assume is the same. */
        for (mask = 0x01u; mask != 0u; mask = (uint8_t)(mask << 1)) {
            unsigned int data_bit = (data[i] & mask) ? 1u : 0u;
            unsigned int crc_bit  = (reg >> 15) & 1u;
            reg = (reg << 1) & 0xFFFFu;
            if (data_bit != crc_bit) {
                reg = (reg ^ 0x8005u) & 0xFFFFu;
            }
        }
    }
    return (uint16_t)reg;
}

tfw_status_t tfw_atecc_wake(void)
{
    uint8_t     rsp[4];
    tfw_status_t st;

    /*
     * The wake pulse is SDA held low for at least 60 us, which no I2C API can
     * express directly. Writing a zero byte to address 0x00 produces it as a
     * side effect: nine bit times at 100 kHz is 90 us of low line, and the
     * element wakes on the low period rather than on the address. The NACK that
     * comes back is expected and ignored -- nothing is listening at 0x00.
     *
     * This is why the element's bus must be at or below 100 kHz. At 400 kHz the
     * same write is 22 us and the part never wakes, which presents as an
     * element that is simply not there.
     *
     * REASSERTED HERE, every wake, rather than trusted from boot. src/caps/
     * bus.c's I2C_CONFIG refuses to touch this peripheral on an element build
     * for exactly this reason, so this call is belt-and-suspenders against
     * that path specifically -- but it is the cheap, correct default
     * regardless of what else in the tree might one day call
     * tfw_hal_i2c_init(): the alternative is trusting every future caller to
     * know this precondition, and the failure mode if one does not is the
     * element silently going quiet, which reads as "no element" rather than
     * "wrong clock". */
    (void)tfw_hal_i2c_init(TFW_ATECC_I2C_HZ);
    const uint8_t zero = 0x00u;
    (void)tfw_hal_i2c_xfer(0x00u, &zero, 1u, 0, 0u);

    /* tWHI: the part is not addressable for about 1.5 ms after the pulse. */
    tfw_hal_delay_us(1500u);

    /* A woken element answers with the four-byte status 0x04 0x11 0x33 0x43.
     * Reading it both confirms the part is there and clears the response
     * buffer, so the next command's response cannot be the wake token. */
    st = tfw_hal_i2c_xfer(TFW_ATECC_ADDR, 0, 0u, rsp, (uint8_t)sizeof rsp);
    if (st != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    if (rsp[0] != 0x04u || rsp[1] != 0x11u) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_crc16(rsp, 2u) != (uint16_t)(rsp[2] | ((uint16_t)rsp[3] << 8))) {
        return TFW_ERR_CRC;
    }
    return TFW_OK;
}

tfw_status_t tfw_atecc_sleep(void)
{
    const uint8_t wa = TFW_ATECC_WA_SLEEP;

    /* Sleep clears TempKey and every other volatile register. Called after any
     * command that put a digest or a nonce in the part, so a reset between two
     * commands cannot leave one there for the next caller to sign. */
    return tfw_hal_i2c_xfer(TFW_ATECC_ADDR, &wa, 1u, 0, 0u);
}

/* Translates the element's own status byte. Its four-byte error response is a
 * different shape from a successful one, so this is reached only when the part
 * has said no; returning the reason rather than a generic failure is what makes
 * "the config zone is locked" distinguishable from "that slot is not readable"
 * at provisioning time, when the difference decides whether to continue. */
static tfw_status_t translate(uint8_t code)
{
    switch (code) {
    case 0x00u: return TFW_OK;
    case 0x01u: return TFW_ERR_CRYPTO;          /* checkmac or verify miscompare */
    case 0x03u: return TFW_ERR_RANGE;           /* parse error: bad opcode or parameter */
    case 0x05u: return TFW_ERR_CRYPTO;          /* ECC fault */
    case 0x07u: return TFW_ERR_CRYPTO;          /* self-test failed */
    case 0x08u: return TFW_ERR_CRYPTO;          /* health test failed */
    case 0x0Fu: return TFW_ERR_UNSUPPORTED;     /* execution error: preconditions not met */
    case 0x11u: return TFW_ERR_CRYPTO;          /* after wake, before a command */
    case 0xFFu: return TFW_ERR_CRC;             /* the element rejected our CRC */
    default:    return TFW_ERR_CRYPTO;
    }
}

/*
 * Waits for the element's answer to a command just sent, reads it into rx and
 * checks it: rlen data bytes, copied out to resp, or a four-byte STATUS. Both
 * tfw_atecc_exec (rx = the shared buf) and tfw_atecc_verify_external (rx = its
 * own four bytes, rlen 0) end here, so the poll and the checks are written
 * once: on the element image every byte of flash counts.
 *
 * The element NACKs every read while it is working, so the wait is a poll
 * rather than a sleep and a command that finishes early is read early. The
 * bound comes from the datasheet's maximum execution time for the command;
 * past it the part is not busy, it is broken. Sign and GenKey take up to
 * 115 ms and Read 5 (atecc.h), so a fixed tries count would either give up on
 * those or spin pointlessly on Read.
 */
static tfw_status_t answer(uint8_t *rx, uint8_t *resp, uint8_t rlen, uint16_t exec_ms)
{
    uint16_t     tries = (uint16_t)(exec_ms * 2u) + 4u;  /* one try per 500 us, plus slack */
    uint8_t      n     = rlen ? (uint8_t)(rlen + 3u) : 4u;
    uint8_t      len;

    /* Out on the read that succeeded, not on "rx[0] is non-zero". rx may be
     * the buffer the command was just sent from, so after a run of NACKs
     * rx[0] still holds the word address 0x03 -- which reads as a three-byte
     * response and sends the code below off to check a CRC over the command
     * it transmitted. A part that never answered would have been reported as
     * a CRC failure. */
    for (;;) {
        if (tries-- == 0u) {
            return TFW_ERR_TIMEOUT;
        }
        tfw_hal_delay_us(500u);
        if (tfw_hal_i2c_xfer(TFW_ATECC_ADDR, 0, 0u, rx, n) == TFW_OK) {
            break;
        }
    }

    /*
     * A four-byte response is the element reporting a STATUS, not returning
     * data. Commands that produce no data (Write, Lock, Verify) answer this
     * way on success too, with status zero -- which is why rlen == 0 takes the
     * same path rather than being a special case bolted on. Either shape
     * carries its CRC in its last two bytes, so one check covers both.
     */
    len = rx[0];
    if (len != 4u && (rlen == 0u || len != n)) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_crc16(rx, (uint8_t)(len - 2u))
            != (uint16_t)(rx[len - 2u] | ((uint16_t)rx[len - 1u] << 8))) {
        return TFW_ERR_CRC;
    }
    if (len == 4u) {
        /* Data was expected and a status came back: even status zero is a
         * failure then, as the command did not produce what it was asked for. */
        tfw_status_t st = translate(rx[1]);
        return (rlen != 0u && st == TFW_OK) ? TFW_ERR_CRYPTO : st;
    }
    memcpy(resp, &rx[1], rlen);
    return TFW_OK;
}

tfw_status_t tfw_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms)
{
    uint8_t     count;
    uint16_t    crc;
    tfw_status_t st;

    if (dlen > TFW_ATECC_MAX_DATA) {
        return TFW_ERR_RANGE;
    }
    /* answer() reads the response into buf: its count byte, the data and two
     * CRC bytes. A response that cannot fit is refused here, before buf is
     * built or the bus is touched, rather than read past buf's end. */
    if (rlen > sizeof buf - 3u) {
        return TFW_ERR_RANGE;
    }
    /* count covers itself, opcode, both parameters, the data and the CRC. */
    count = (uint8_t)(7u + dlen);

    buf[0] = TFW_ATECC_WA_COMMAND;
    buf[1] = count;
    buf[2] = opcode;
    buf[3] = p1;
    buf[4] = (uint8_t)(p2 & 0xFFu);
    buf[5] = (uint8_t)(p2 >> 8);
    if (dlen) {
        memcpy(&buf[6], data, dlen);
    }
    /* The CRC covers the count byte onward and NOT the word address, which is
     * addressing rather than payload. Including it produces a packet the part
     * rejects with 0xFF, which reads like a bus fault and is not one. */
    crc = tfw_atecc_crc16(&buf[1], (uint8_t)(count - 2u));
    buf[6u + dlen] = (uint8_t)(crc & 0xFFu);
    buf[7u + dlen] = (uint8_t)(crc >> 8);

    st = tfw_hal_i2c_xfer(TFW_ATECC_ADDR, buf, (uint8_t)(count + 1u), 0, 0u);
    if (st == TFW_OK) {
        st = answer(buf, resp, rlen, exec_ms);
    }
    /* A data response may be a secret: the CDI the provider's init asks for
     * (HMAC over the UDS slot) is one. So may the command itself, which buf
     * still holds with the caller's data in it (a Write payload, a MAC). TCG
     * DICE HW Req 6.4 wants both cleared once no longer needed, and the
     * caller's copy is the only one that is still needed, so this file-static
     * copy goes now, through tfw_secure_zero (src/core/secure_zero.h) rather
     * than memset. Every path past the packet build is cleared: the failed
     * send, the refused answers and the timeout included, one call site,
     * whatever the HAL or answer() returned. */
    tfw_secure_zero(buf, sizeof buf);
    return st;
}

/* The data-zone address (Param2) of word `word` in slot `slot`: the block
 * offset in the high byte, the slot in bits 3..6, the word within its block in
 * bits 0..2. The slot field is four bits wide, so a slot of DATA_SLOTS or more
 * would spill into the block field and name another slot's address; the entry
 * points refuse it before this is called. */
#define DATA_SLOTS 16u

static uint16_t data_addr(uint8_t slot, uint8_t word)
{
    return (uint16_t)(((uint16_t)(word / 8u) << 8) | ((uint16_t)slot << 3) | (uint16_t)(word % 8u));
}

/* The Param1 of a data-zone Read or Write of `len` bytes, or 0xFF for a length
 * the part has no form for. 0xFF is no zone byte this driver sends. */
static uint8_t data_p1(uint8_t len)
{
    if (len == 32u) {
        return (uint8_t)(TFW_ATECC_ZONE_DATA | TFW_ATECC_ZONE_32);
    }
    if (len == 4u) {
        return TFW_ATECC_ZONE_DATA;
    }
    return 0xFFu;
}

tfw_status_t tfw_atecc_data_read(uint8_t slot, uint8_t word, uint8_t *out, uint8_t len)
{
    uint8_t p1 = data_p1(len);

    if (p1 == 0xFFu || slot >= DATA_SLOTS) {
        return TFW_ERR_RANGE;
    }
    return tfw_atecc_exec(TFW_ATECC_OP_READ, p1, data_addr(slot, word),
                          0, 0u, out, len, TFW_ATECC_T_READ);
}

tfw_status_t tfw_atecc_data_write(uint8_t slot, uint8_t word, const uint8_t *in, uint8_t len)
{
    uint8_t p1 = data_p1(len);

    if (p1 == 0xFFu || slot >= DATA_SLOTS) {
        return TFW_ERR_RANGE;
    }
    return tfw_atecc_exec(TFW_ATECC_OP_WRITE, p1, data_addr(slot, word),
                          in, len, 0, 0u, TFW_ATECC_T_WRITE);
}

tfw_status_t tfw_atecc_verify_external(const uint8_t sig[64], const uint8_t pubkey[64],
                                     uint16_t exec_ms)
{
    /* On the CALLER's stack, not file-static -- see atecc.h's comment on why
     * this command does not grow the shared buf[] above. 8 bytes of overhead
     * (word address, count, opcode, both parameters, two CRC bytes) plus the
     * 128-byte signature-and-key payload, exactly as tfw_atecc_exec sizes its
     * own buffer. */
    uint8_t  cmd[128u + 8u];
    uint8_t  rsp[4];
    uint8_t  count = (uint8_t)(7u + 128u);
    uint16_t crc;

    cmd[0] = TFW_ATECC_WA_COMMAND;
    cmd[1] = count;
    cmd[2] = TFW_ATECC_OP_VERIFY;
    cmd[3] = TFW_ATECC_VERIFY_EXTERNAL;
    cmd[4] = (uint8_t)(TFW_ATECC_VERIFY_KEYID_P256 & 0xFFu);
    cmd[5] = (uint8_t)(TFW_ATECC_VERIFY_KEYID_P256 >> 8);
    memcpy(&cmd[6], sig, 64u);
    memcpy(&cmd[70], pubkey, 64u);
    crc = tfw_atecc_crc16(&cmd[1], (uint8_t)(count - 2u));
    cmd[6u + 128u] = (uint8_t)(crc & 0xFFu);
    cmd[7u + 128u] = (uint8_t)(crc >> 8);

    if (tfw_hal_i2c_xfer(TFW_ATECC_ADDR, cmd, (uint8_t)(count + 1u), 0, 0u) != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }

    /* The same answer tfw_atecc_exec reads for a status-only reply (Verify
     * External without a MAC answers with exactly one status byte, wrapped in
     * the same four-byte shape Write and Lock use), read into this function's
     * own four bytes because the two no longer share a command buffer. */
    return answer(rsp, 0, 0u, exec_ms);
}
