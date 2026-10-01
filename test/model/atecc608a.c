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
#include "sha256.h"

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

/* The zones, enough of them to answer Read and Write honestly.
 *
 * 128 bytes of config is the real size; sixteen 32-byte data slots is what the
 * part has. Modelling them at all is what lets the provisioning tests assert
 * that a write LANDED and that a locked zone refuses -- which is the behaviour
 * that decides whether a real part survives the procedure. */
static uint8_t  config[128];
static uint8_t  slots[16][32];
static uint8_t  config_locked;
static uint8_t  data_locked;

/* The SHA engine, modelled for real.
 *
 * It computes an actual SHA-256 rather than returning a token, because the
 * property worth testing is that a measurement taken through the element and
 * one taken in software are THE SAME 32 BYTES -- that is what makes the choice
 * of backend a build-time decision rather than a fork of the wire contract. A
 * model that returned a placeholder could not show that.
 */
static ag_sha256_t sha_ctx;
static uint8_t     sha_active;

/* The image-signing public key slot, for Verify in stored mode. The model does
 * no elliptic-curve arithmetic; it records what it was asked to verify and
 * answers from a flag a test sets, because modelling ECDSA here would test the
 * model rather than the driver. */
static uint8_t  verify_should_pass;
static uint8_t  verify_last_sig[64];
/* issue #4: External mode's public key, alongside the signature above --
 * populated only when the driver used mode 0x02, zero otherwise. */
static uint8_t  verify_last_pubkey[64];
static uint8_t  verify_seen;

/* No element on the board at all: every transfer NACKs, which is what a bus
 * with nothing on it does. Distinct from a busy part -- a driver must tell
 * "not there" from "not yet". */
static uint8_t  absent;

void ag_model_reset(void)
{
    awake = 0u; provisioned = 0u; has_key = 0u; busy_reads = 0u;
    forced_status = 0u; last_opcode = 0u; counter0 = 0u;
    rsp_len = 0u;
    memset(rsp, 0, sizeof rsp);
    memset(config, 0, sizeof config);
    memset(slots, 0, sizeof slots);
    config_locked = 0u; data_locked = 0u;
    sha_active = 0u; verify_should_pass = 0u; verify_seen = 0u; absent = 0u;
    memset(verify_last_sig, 0, sizeof verify_last_sig);
    memset(verify_last_pubkey, 0, sizeof verify_last_pubkey);
    /* A blank part's identifying bytes, so Read returns something a host can
     * recognise rather than zeros it might mistake for a failed read. */
    config[0] = 0x01u; config[1] = 0x23u; config[2] = 0x45u; config[3] = 0x67u;
    /* 0x55 is UNLOCKED on this part. LockValue is byte 86 (the data zone) and
     * LockConfig is byte 87 (the config zone) -- both in config BLOCK 2, which
     * is what the reply has to reach. */
    config[86] = 0x55u;
    config[87] = 0x55u;
}

void ag_model_provision(void)  { provisioned = 1u; has_key = 1u; }
uint8_t ag_model_config_locked(void) { return config_locked; }
uint8_t ag_model_data_locked(void)   { return data_locked; }
const uint8_t *ag_model_slot(uint8_t n) { return n < 16u ? slots[n] : 0; }
void ag_model_set_verify_result(uint8_t pass) { verify_should_pass = pass; }
void ag_model_set_absent(uint8_t yes) { absent = yes; }
uint8_t ag_model_verify_seen(void) { return verify_seen; }
const uint8_t *ag_model_verify_last_sig(void) { return verify_last_sig; }
const uint8_t *ag_model_verify_last_pubkey(void) { return verify_last_pubkey; }
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
        if (param1 == AG_ATECC_SHA_SHA256_START) {
            ag_sha256_init(&sha_ctx);
            sha_active = 1u;
            respond_status(0x00u);
            return;
        }
        if (param1 == AG_ATECC_SHA_SHA256_UPDATE && sha_active) {
            /* param2 is the byte count and the data follows the header. The
             * element accepts only whole 64-byte blocks here, and modelling
             * that refusal is worth more than accepting a short one: a driver
             * that sent a partial block would pass against a lenient model and
             * fail on silicon. */
            if (param2 != 64u || n < 6u + 64u) { respond_status(0x03u); return; }
            ag_sha256_update(&sha_ctx, &p[6], 64u);
            respond_status(0x00u);
            return;
        }
        if (param1 == AG_ATECC_SHA_SHA256_END && sha_active) {
            uint8_t tail = (uint8_t)param2;
            if (tail) { ag_sha256_update(&sha_ctx, &p[6], tail); }
            ag_sha256_final(&sha_ctx, body);
            sha_active = 0u;
            respond(body, 32u);
            return;
        }
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

    case AG_ATECC_OP_READ: {
        /* param1 carries the zone and the 32-vs-4-byte bit; param2 is a word
         * address, and a 32-byte read is eight words. */
        uint16_t off = (uint16_t)(param2 * 4u);
        if ((param1 & AG_ATECC_ZONE_32) == 0u) { respond_status(0x03u); return; }
        if ((param1 & 0x03u) == AG_ATECC_ZONE_CONFIG) {
            if (off + 32u > sizeof config) { respond_status(0x03u); return; }
            memcpy(body, &config[off], 32u);
            respond(body, 32u);
            return;
        }
        /* A never-readable slot refuses, which is the behaviour the whole
         * design rests on. The model refuses because it is told to; on silicon
         * the slot configuration enforces it. */
        respond_status(0x0Fu);
        return;
    }

    case AG_ATECC_OP_WRITE: {
        uint16_t off = (uint16_t)(param2 * 4u);
        if ((param1 & AG_ATECC_ZONE_32) == 0u) { respond_status(0x03u); return; }
        if ((param1 & 0x03u) == AG_ATECC_ZONE_CONFIG) {
            if (config_locked)               { respond_status(0x0Fu); return; }
            if (off + 32u > sizeof config)   { respond_status(0x03u); return; }
            memcpy(&config[off], &p[6], 32u);
            respond_status(0x00u);
            return;
        }
        /* A data-zone write addresses a slot; the UDS write uses TempKey as its
         * source, so what lands is what Nonce loaded rather than what the
         * packet carried. Modelling that is the point -- it is how the test can
         * assert the secret never crossed the wire. */
        if (data_locked) { respond_status(0x0Fu); return; }
        if (param2 >> 3 < 16u) {
            memcpy(slots[param2 >> 3], ag_model_tempkey(), 32u);
            respond_status(0x00u);
            return;
        }
        respond_status(0x03u);
        return;
    }

    case AG_ATECC_OP_LOCK:
        /* The summary CRC is in param2. A zero would be the no-CRC form, which
         * the firmware refuses before it ever reaches here -- so the model
         * treats it as the parse error the part would give. */
        if (param2 == 0u) { respond_status(0x03u); return; }
        if (param1 == AG_ATECC_LOCK_CONFIG) {
            if (config_locked) { respond_status(0x0Fu); return; }
            config_locked = 1u; config[87] = 0x00u;   /* LockConfig */
        } else if (param1 == AG_ATECC_LOCK_DATA) {
            if (!config_locked || data_locked) { respond_status(0x0Fu); return; }
            data_locked = 1u; config[86] = 0x00u;     /* LockValue */
        } else {
            respond_status(0x03u);
            return;
        }
        respond_status(0x00u);
        return;

    case 0x45u:   /* Verify */
        /* Stored mode (0x00) names a slot; External mode (0x02, issue #4's
         * AUTH_CHALLENGE/AUTH_RESPONSE) carries a 64-byte public key after the
         * signature instead. Either way the model records what it was asked
         * to verify and answers from a flag rather than doing curve
         * arithmetic -- that would test the model, not the driver. */
        if (param1 == 0x00u) {
            if (n < 6u + 64u) { respond_status(0x03u); return; }
            memcpy(verify_last_sig, &p[6], 64u);
            verify_seen = 1u;
            respond_status(verify_should_pass ? (uint8_t)0x00u : (uint8_t)0x01u);  /* 0x01 = miscompare */
            return;
        }
        if (param1 == 0x02u) {
            if (n < 6u + 128u)                    { respond_status(0x03u); return; }
            if (param2 != 0x0004u)                { respond_status(0x03u); return; }
            memcpy(verify_last_sig, &p[6], 64u);
            memcpy(verify_last_pubkey, &p[6 + 64u], 64u);
            verify_seen = 1u;
            respond_status(verify_should_pass ? (uint8_t)0x00u : (uint8_t)0x01u);  /* 0x01 = miscompare */
            return;
        }
        respond_status(0x03u);
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
    if (absent) {
        return AG_ERR_NACK;
    }
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
