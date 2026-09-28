/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "atecc.h"
#include "ag/hal.h"

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
static uint8_t buf[AG_ATECC_MAX_DATA + 8u];

uint16_t ag_atecc_crc16(const uint8_t *data, uint8_t len)
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

ag_status_t ag_atecc_wake(void)
{
    uint8_t     rsp[4];
    ag_status_t st;

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
     * ag_hal_i2c_init(): the alternative is trusting every future caller to
     * know this precondition, and the failure mode if one does not is the
     * element silently going quiet, which reads as "no element" rather than
     * "wrong clock". */
    (void)ag_hal_i2c_init(AG_ATECC_I2C_HZ);
    const uint8_t zero = 0x00u;
    (void)ag_hal_i2c_xfer(0x00u, &zero, 1u, 0, 0u);

    /* tWHI: the part is not addressable for about 1.5 ms after the pulse. */
    ag_hal_delay_us(1500u);

    /* A woken element answers with the four-byte status 0x04 0x11 0x33 0x43.
     * Reading it both confirms the part is there and clears the response
     * buffer, so the next command's response cannot be the wake token. */
    st = ag_hal_i2c_xfer(AG_ATECC_ADDR, 0, 0u, rsp, (uint8_t)sizeof rsp);
    if (st != AG_OK) {
        return AG_ERR_CRYPTO;
    }
    if (rsp[0] != 0x04u || rsp[1] != 0x11u) {
        return AG_ERR_CRYPTO;
    }
    if (ag_atecc_crc16(rsp, 2u) != (uint16_t)(rsp[2] | ((uint16_t)rsp[3] << 8))) {
        return AG_ERR_CRC;
    }
    return AG_OK;
}

ag_status_t ag_atecc_sleep(void)
{
    const uint8_t wa = AG_ATECC_WA_SLEEP;

    /* Sleep clears TempKey and every other volatile register. Called after any
     * command that put a digest or a nonce in the part, so a reset between two
     * commands cannot leave one there for the next caller to sign. */
    return ag_hal_i2c_xfer(AG_ATECC_ADDR, &wa, 1u, 0, 0u);
}

/* Translates the element's own status byte. Its four-byte error response is a
 * different shape from a successful one, so this is reached only when the part
 * has said no; returning the reason rather than a generic failure is what makes
 * "the config zone is locked" distinguishable from "that slot is not readable"
 * at provisioning time, when the difference decides whether to continue. */
static ag_status_t translate(uint8_t code)
{
    switch (code) {
    case 0x00u: return AG_OK;
    case 0x01u: return AG_ERR_CRYPTO;          /* checkmac or verify miscompare */
    case 0x03u: return AG_ERR_RANGE;           /* parse error: bad opcode or parameter */
    case 0x05u: return AG_ERR_CRYPTO;          /* ECC fault */
    case 0x07u: return AG_ERR_CRYPTO;          /* self-test failed */
    case 0x08u: return AG_ERR_CRYPTO;          /* health test failed */
    case 0x0Fu: return AG_ERR_UNSUPPORTED;     /* execution error: preconditions not met */
    case 0x11u: return AG_ERR_CRYPTO;          /* after wake, before a command */
    case 0xFFu: return AG_ERR_CRC;             /* the element rejected our CRC */
    default:    return AG_ERR_CRYPTO;
    }
}

ag_status_t ag_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms)
{
    uint8_t     count, n, got;
    uint16_t    tries;
    uint16_t    crc;
    ag_status_t st;

    if (dlen > AG_ATECC_MAX_DATA) {
        return AG_ERR_RANGE;
    }
    /* count covers itself, opcode, both parameters, the data and the CRC. */
    count = (uint8_t)(7u + dlen);

    buf[0] = AG_ATECC_WA_COMMAND;
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
    crc = ag_atecc_crc16(&buf[1], (uint8_t)(count - 2u));
    buf[6u + dlen] = (uint8_t)(crc & 0xFFu);
    buf[7u + dlen] = (uint8_t)(crc >> 8);

    st = ag_hal_i2c_xfer(AG_ATECC_ADDR, buf, (uint8_t)(count + 1u), 0, 0u);
    if (st != AG_OK) {
        return st;
    }

    /*
     * The element NACKs every read while it is working, so the wait is a poll
     * rather than a sleep and a command that finishes early is read early. The
     * bound comes from the datasheet's maximum execution time for the command;
     * past it the part is not busy, it is broken. Sign is ~50 ms and GenKey
     * ~115 ms, so a fixed tries count would either give up on those or spin
     * pointlessly on Random.
     */
    tries = (uint16_t)(exec_ms * 2u) + 4u;    /* one try per 500 us, plus slack */
    n     = rlen ? (uint8_t)(rlen + 3u) : 4u;
    got   = 0u;
    while (tries-- && !got) {
        ag_hal_delay_us(500u);
        if (ag_hal_i2c_xfer(AG_ATECC_ADDR, 0, 0u, buf, n) == AG_OK) {
            got = 1u;
        }
    }
    /* An explicit flag, not "buf[0] is non-zero". The buffer is shared with the
     * command that was just sent, so after a run of NACKs buf[0] still holds
     * the word address 0x03 -- which reads as a three-byte response and sends
     * the code below off to check a CRC over the command it transmitted. A part
     * that never answered would have been reported as a CRC failure. */
    if (!got) {
        return AG_ERR_TIMEOUT;
    }

    /*
     * A four-byte response is the element reporting a STATUS, not returning
     * data. Commands that produce no data (Write, Lock) answer this way on
     * success too, with status zero -- which is why rlen == 0 takes the same
     * path rather than being a special case bolted on.
     */
    if (buf[0] == 4u) {
        if (ag_atecc_crc16(buf, 2u) != (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8))) {
            return AG_ERR_CRC;
        }
        if (rlen != 0u) {
            /* Data was expected and a status came back. Even status zero is a
             * failure here: the command did not produce what it was asked for. */
            ag_status_t why = translate(buf[1]);
            return why == AG_OK ? AG_ERR_CRYPTO : why;
        }
        return translate(buf[1]);
    }
    if (rlen == 0u || buf[0] != n) {
        return AG_ERR_CRYPTO;
    }
    if (ag_atecc_crc16(buf, (uint8_t)(n - 2u))
            != (uint16_t)(buf[n - 2u] | ((uint16_t)buf[n - 1u] << 8))) {
        return AG_ERR_CRC;
    }
    memcpy(resp, &buf[1], rlen);
    return AG_OK;
}
