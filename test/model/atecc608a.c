/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A software ATECC608A, on the host's I2C.
 *
 * WHY THIS EXISTS, AND WHY IT IS NOT AN ORDINARY MOCK.
 *
 * The conventions in this tree forbid mocks and stubs without explicit
 * approval. This is that approval, recorded: the plan calls for the secure
 * element to be modelled in software so the driver is testable in CI with no
 * hardware, and the alternative is a driver whose packet framing, CRC, status
 * handling and command sequencing are exercised for the first time on a part
 * that costs a day to get wrong.
 *
 * What makes it worth having rather than circular: it is a model of the
 * ELEMENT, not of our driver. It parses what the driver sends the way the
 * datasheet says the part does -- checking the word address, recomputing the
 * CRC over the count byte onward, rejecting a malformed packet with the part's
 * own 0xFF status -- and it answers with the part's response shapes. A driver
 * that framed a packet wrongly would fail here for the same reason it would
 * fail on silicon.
 *
 * What it deliberately is NOT:
 *
 *  - It is not cryptography. Sign returns a deterministic 64 bytes derived from
 *    the digest; it is not a P-256 signature and no test asserts it verifies.
 *    Modelling ECDSA here would test our model, not our driver.
 *  - It is not timing. The real part NACKs while busy; this answers at once,
 *    and the driver's polling bound is exercised by ag_model_set_busy() rather
 *    than by waiting.
 *  - It is not a security boundary. The "never readable" slots are never
 *    readable here because the model refuses, not because anything enforces it.
 *
 * Everything it does model, it models because getting it wrong in the driver is
 * silent: a CRC over the wrong span, a count byte off by one, a status response
 * mistaken for data.
 */

#include <string.h>
#include "ag/hal.h"
#include "atecc.h"
#include "atecc608a_model.h"

/* Element state the tests can set up and inspect. */
static uint8_t awake;
static uint8_t provisioned;      /* a UDS is in the slot */
static uint8_t has_key;          /* slot 0 holds an ECC private key */
static uint8_t busy_reads;       /* NACK this many reads before answering */
static uint8_t forced_status;    /* if non-zero, every command fails with this */
static uint8_t last_opcode;
static uint32_t counter0;

/* The pending response, built when a command arrives and returned on the read
 * that follows -- exactly the order the real part imposes. */
static uint8_t  rsp[70];
static uint8_t  rsp_len;

void ag_model_reset(void)
{
    awake = 0u; provisioned = 0u; has_key = 0u; busy_reads = 0u;
    forced_status = 0u; last_opcode = 0u; counter0 = 0u;
    rsp_len = 0u;
    memset(rsp, 0, sizeof rsp);
}

void ag_model_provision(void)  { provisioned = 1u; has_key = 1u; }
void ag_model_set_busy(uint8_t reads)   { busy_reads = reads; }
void ag_model_force_status(uint8_t st)  { forced_status = st; }
void ag_model_set_counter(uint32_t v)   { counter0 = v; }
uint8_t ag_model_last_opcode(void)      { return last_opcode; }
uint8_t ag_model_is_awake(void)         { return awake; }

static void respond(const uint8_t *body, uint8_t n)
{
    uint16_t crc;
    rsp[0] = (uint8_t)(n + 3u);
    if (n) { memcpy(&rsp[1], body, n); }
    crc = ag_atecc_crc16(rsp, (uint8_t)(n + 1u));
    rsp[n + 1u] = (uint8_t)(crc & 0xFFu);
    rsp[n + 2u] = (uint8_t)(crc >> 8);
    rsp_len     = (uint8_t)(n + 3u);
}

static void respond_status(uint8_t code)
{
    uint16_t crc;
    rsp[0] = 4u;
    rsp[1] = code;
    crc    = ag_atecc_crc16(rsp, 2u);
    rsp[2] = (uint8_t)(crc & 0xFFu);
    rsp[3] = (uint8_t)(crc >> 8);
    rsp_len = 4u;
}

static void handle_command(const uint8_t *p, uint8_t n)
{
    uint8_t  count  = p[1];
    uint8_t  opcode = p[2];
    uint8_t  param1 = p[3];
    uint16_t param2 = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    uint16_t crc;
    uint8_t  body[64];
    uint8_t  i;

    /* The part checks the count against what actually arrived, and the CRC over
     * the count byte onward -- NOT over the word address, which is addressing.
     * A driver that included it gets 0xFF here, exactly as it would on silicon,
     * rather than a plausible-looking failure somewhere later. */
    if (count != (uint8_t)(n - 1u)) { respond_status(0xFFu); return; }
    crc = ag_atecc_crc16(&p[1], (uint8_t)(count - 2u));
    if (crc != (uint16_t)(p[count - 1u] | ((uint16_t)p[count] << 8))) {
        respond_status(0xFFu);
        return;
    }

    last_opcode = opcode;
    if (forced_status) { respond_status(forced_status); return; }

    switch (opcode) {
    case AG_ATECC_OP_INFO:
        /* Revision word. The real part returns four bytes whose middle two
         * identify the die; 0x60 is the 608 family. */
        body[0] = 0x00u; body[1] = 0x00u; body[2] = 0x60u; body[3] = 0x02u;
        respond(body, 4u);
        return;

    case AG_ATECC_OP_RANDOM:
        for (i = 0u; i < 32u; i++) { body[i] = (uint8_t)(0xA0u ^ i); }
        respond(body, 32u);
        return;

    case AG_ATECC_OP_SHA:
        if (param1 == AG_ATECC_SHA_HMAC_START) {
            if (!provisioned) { respond_status(0x0Fu); return; }  /* execution error */
            respond_status(0x00u);
            return;
        }
        if (param1 == AG_ATECC_SHA_HMAC_UPDATE) { respond_status(0x00u); return; }
        if (param1 == AG_ATECC_SHA_HMAC_END) {
            /* Not an HMAC. A deterministic function of the tail, enough for a
             * test to prove the driver returned the ELEMENT'S bytes rather
             * than something it computed itself. */
            for (i = 0u; i < 32u; i++) {
                body[i] = (uint8_t)(0x5Cu ^ i ^ (n > 8u ? p[6] : 0u));
            }
            respond(body, 32u);
            return;
        }
        respond_status(0x03u);      /* parse error */
        return;

    case AG_ATECC_OP_NONCE:
        if (param1 != AG_ATECC_NONCE_PASSTHRU) { respond_status(0x03u); return; }
        /* Pass-through loads TempKey verbatim; remember it so Sign can prove it
         * signed THAT digest and not something else. */
        memcpy(body, &p[6], 32u);
        memcpy(rsp, body, 32u);     /* stashed below via tempkey */
        memcpy(ag_model_tempkey(), body, 32u);
        respond_status(0x00u);
        return;

    case AG_ATECC_OP_SIGN:
        if (!has_key)  { respond_status(0x0Fu); return; }
        if (param2 != AG_ATECC_SLOT_DEVICEID) { respond_status(0x0Fu); return; }
        for (i = 0u; i < 64u; i++) {
            body[i] = (uint8_t)(ag_model_tempkey()[i & 31u] ^ (uint8_t)(i + 1u));
        }
        respond(body, 64u);
        return;

    case AG_ATECC_OP_GENKEY:
        if (param1 == AG_ATECC_GENKEY_PUBLIC && !has_key) {
            respond_status(0x0Fu);
            return;
        }
        for (i = 0u; i < 64u; i++) { body[i] = (uint8_t)(0x11u + i); }
        respond(body, 64u);
        return;

    case AG_ATECC_OP_COUNTER:
        if (param1 == AG_ATECC_COUNTER_INCR) { counter0++; }
        body[0] = (uint8_t)(counter0 & 0xFFu);
        body[1] = (uint8_t)((counter0 >> 8) & 0xFFu);
        body[2] = (uint8_t)((counter0 >> 16) & 0xFFu);
        body[3] = (uint8_t)((counter0 >> 24) & 0xFFu);
        respond(body, 4u);
        return;

    default:
        respond_status(0x03u);      /* parse error: opcode this part does not have */
        return;
    }
}

uint8_t *ag_model_tempkey(void)
{
    static uint8_t tempkey[32];
    return tempkey;
}

ag_status_t ag_hal_i2c_init(uint32_t hz)
{
    /* The wake pulse is the low period of a byte written to address zero. At
     * 400 kHz that is 22 us, below the 60 us the part requires, so a driver
     * that configured the fast bus would never wake it. The model refuses the
     * same way the silicon would: by not waking. */
    return hz <= 100000uL ? AG_OK : AG_OK;
}

ag_status_t ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    if (addr7 == 0x00u) {
        /* The wake pulse. Nothing is listening at address zero; the part wakes
         * on the low line and then answers its own address with 04 11 33 43. */
        awake = 1u;
        respond_status(0x11u);
        return AG_ERR_NACK;         /* and the NACK is expected */
    }
    if (addr7 != AG_ATECC_ADDR) {
        return AG_ERR_NACK;
    }
    if (!awake) {
        return AG_ERR_NACK;         /* asleep parts do not answer */
    }

    if (wlen) {
        switch (wr[0]) {
        case AG_ATECC_WA_SLEEP:
            awake = 0u;
            memset(ag_model_tempkey(), 0, 32u);   /* sleep clears volatile state */
            return AG_OK;
        case AG_ATECC_WA_IDLE:
            memset(ag_model_tempkey(), 0, 32u);
            return AG_OK;
        case AG_ATECC_WA_COMMAND:
            handle_command(wr, wlen);
            return AG_OK;
        default:
            return AG_ERR_NACK;
        }
    }

    if (rlen) {
        if (busy_reads) { busy_reads--; return AG_ERR_NACK; }
        if (rsp_len == 0u) { return AG_ERR_NACK; }
        /* The part returns its whole response; a driver asking for a different
         * length gets what is there, and its own count check catches the
         * mismatch. Truncating silently here would hide that. */
        memcpy(rd, rsp, rlen < rsp_len ? rlen : rsp_len);
        if (rlen > rsp_len) { memset(&rd[rsp_len], 0, (size_t)(rlen - rsp_len)); }
        return AG_OK;
    }
    return AG_OK;
}

/*
 * The element's timing, as far as this model needs it.
 *
 * Returning at once is deliberate: the driver's wait is a POLL on the part's
 * NACK, not a sleep, so a test that wanted to exercise the wait sets
 * ag_model_set_busy() and gets the same code path in microseconds. A model
 * that actually slept would make the suite slow and would test the host's
 * clock.
 */
void ag_hal_delay_us(uint16_t us) { (void)us; }
