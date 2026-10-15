/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "atecc.h"
#include "tfw/hal.h"

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

tfw_status_t tfw_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms)
{
    uint8_t     count, n, got;
    uint16_t    tries;
    uint16_t    crc;
    tfw_status_t st;

    if (dlen > TFW_ATECC_MAX_DATA) {
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
    if (st != TFW_OK) {
        return st;
    }

    /*
     * The element NACKs every read while it is working, so the wait is a poll
     * rather than a sleep and a command that finishes early is read early. The
     * bound comes from the datasheet's maximum execution time for the command;
     * past it the part is not busy, it is broken. Sign and GenKey take up to
     * 115 ms and Read 5 (atecc.h), so a fixed tries count would either give
     * up on those or spin pointlessly on Read.
     */
    tries = (uint16_t)(exec_ms * 2u) + 4u;    /* one try per 500 us, plus slack */
    n     = rlen ? (uint8_t)(rlen + 3u) : 4u;
    got   = 0u;
    while (tries-- && !got) {
        tfw_hal_delay_us(500u);
        if (tfw_hal_i2c_xfer(TFW_ATECC_ADDR, 0, 0u, buf, n) == TFW_OK) {
            got = 1u;
        }
    }
    /* An explicit flag, not "buf[0] is non-zero". The buffer is shared with the
     * command that was just sent, so after a run of NACKs buf[0] still holds
     * the word address 0x03 -- which reads as a three-byte response and sends
     * the code below off to check a CRC over the command it transmitted. A part
     * that never answered would have been reported as a CRC failure. */
    if (!got) {
        return TFW_ERR_TIMEOUT;
    }

    /*
     * A four-byte response is the element reporting a STATUS, not returning
     * data. Commands that produce no data (Write, Lock) answer this way on
     * success too, with status zero -- which is why rlen == 0 takes the same
     * path rather than being a special case bolted on.
     */
    if (buf[0] == 4u) {
        if (tfw_atecc_crc16(buf, 2u) != (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8))) {
            return TFW_ERR_CRC;
        }
        if (rlen != 0u) {
            /* Data was expected and a status came back. Even status zero is a
             * failure here: the command did not produce what it was asked for. */
            tfw_status_t why = translate(buf[1]);
            return why == TFW_OK ? TFW_ERR_CRYPTO : why;
        }
        return translate(buf[1]);
    }
    if (rlen == 0u || buf[0] != n) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_crc16(buf, (uint8_t)(n - 2u))
            != (uint16_t)(buf[n - 2u] | ((uint16_t)buf[n - 1u] << 8))) {
        return TFW_ERR_CRC;
    }
    memcpy(resp, &buf[1], rlen);
    return TFW_OK;
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
    uint16_t tries;
    uint8_t  got;

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

    /* The same poll tfw_atecc_exec runs for a status-only reply (Verify
     * External without a MAC answers with exactly one status byte, wrapped in
     * the same four-byte shape Write and Lock use) -- duplicated rather than
     * shared because the two functions no longer agree on where the command
     * buffer lives. */
    tries = (uint16_t)(exec_ms * 2u) + 4u;
    got   = 0u;
    while (tries-- && !got) {
        tfw_hal_delay_us(500u);
        if (tfw_hal_i2c_xfer(TFW_ATECC_ADDR, 0, 0u, rsp, 4u) == TFW_OK) {
            got = 1u;
        }
    }
    if (!got) {
        return TFW_ERR_TIMEOUT;
    }
    if (rsp[0] != 4u) {
        return TFW_ERR_CRYPTO;
    }
    if (tfw_atecc_crc16(rsp, 2u) != (uint16_t)(rsp[2] | ((uint16_t)rsp[3] << 8))) {
        return TFW_ERR_CRC;
    }
    return translate(rsp[1]);
}
