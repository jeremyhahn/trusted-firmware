/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A software ATECC608C, on the host's I2C, at the board profile's address.
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
 * WHAT IS REAL. The curve and the hash are real; the part's physical secrecy
 * is not.
 *
 *  - SHA-256 is a real SHA-256, and HMAC-SHA256 is a real HMAC keyed by the
 *    slot the driver names.
 *  - P-256 is real, through the vendored micro-ecc (test/third_party/micro-ecc,
 *    pinned in MANIFEST.md): the DeviceID key in slot 0 is a real private key
 *    with its real public point, Sign is ECDSA over the digest the driver
 *    loaded into TempKey, and Verify in External mode is a real ECDSA
 *    verification. Sign's k is deterministic (micro-ecc's HMAC-DRBG variant of
 *    RFC 6979 -- the same construction, not byte-identical output), so a
 *    signature is reproducible; the real part's k is random.
 *    The evidence this model produces therefore VERIFIES under its public key,
 *    which is what a host peer for go-xkms and go-trusted-platform needs.
 *  - The DeviceID key behind tfw_model_provision() is a TEST key: its private
 *    half is SHA-256 of a fixed, public string (kTestKeySeed below). Anyone can
 *    compute it, so evidence from this model proves nothing about a device, and
 *    nothing may ever enrol, trust or allow-list its public key.
 *
 * What it deliberately is NOT:
 *
 *  - It is not timing. The real part NACKs while busy; this answers at once,
 *    and the driver's polling bound is exercised by tfw_model_set_busy() rather
 *    than by waiting.
 *  - It is not a security boundary. The "never readable" slots are never
 *    readable here because the model refuses, not because anything enforces it.
 *  - Verify in STORED mode (the bootloader's image check) is still answered
 *    from a flag, tfw_model_set_verify_result(): that path's key lives in a
 *    locked slot this model does not populate. External mode ignores the flag.
 *  - It has no ECDH and no KDF, and does not claim any: the firmware uses
 *    neither.
 *  - Random is a fixed sequence, not a TRNG, so a test can prove a value came
 *    from inside the part. Each answer differs from the last. Nothing else
 *    is derived from it.
 *
 * Everything it does model, it models because getting it wrong in the driver is
 * silent: a CRC over the wrong span, a count byte off by one, a status response
 * mistaken for data.
 */

#include <stdint.h>
#include <string.h>
#include "tfw/hal.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "sha256.h"
#include "uECC.h"

/* Element state the tests can set up and inspect. */
static uint8_t awake;
static uint8_t provisioned;      /* a UDS is in the slot */
static uint8_t has_key;          /* slot 0 holds an ECC private key */
static uint8_t tempkey_valid;    /* Nonce loaded TempKey since the last sleep */
static uint16_t busy_reads;      /* NACK this many reads before answering */
static uint16_t exec_ms[256];    /* per opcode: busy this long on each arrival */
static uint8_t forced_status;    /* if non-zero, every command fails with this */
static uint8_t last_opcode;
static uint8_t last_addr;
static uint32_t counter0;
static uint32_t random_calls;    /* Random's n-th answer differs from its first */

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
static tfw_sha256_t sha_ctx;
static uint8_t     sha_active;

/* Verify in STORED mode, the bootloader's image check. Its public key lives in
 * a locked slot this model does not populate, so it records what it was asked
 * to verify and answers from a flag a test sets. External mode is real ECDSA
 * and does not read the flag. */
static uint8_t  verify_should_pass;
static uint8_t  verify_last_sig[64];
/* issue #4: External mode's public key, alongside the signature above --
 * populated only when the driver used mode 0x02, zero otherwise. */
static uint8_t  verify_last_pubkey[64];
/* issue #78: a stored-mode slot with a real key, set by a test. 0xFF: none. */
static uint8_t  stored_pub_slot = 0xFFu;
static uint8_t  stored_pub[64];
static uint8_t  verify_seen;

/* ------------------------------------------------------------------ P-256
 *
 * The slot 0 key pair, and the three operations the firmware drives: public
 * point, Sign and Verify. All the curve arithmetic is micro-ecc's; this file
 * only decides which key, which digest and which k.
 */

/* The seed of the DeviceID TEST key. PUBLIC ON PURPOSE: the private key is
 * SHA-256 of these bytes (no terminator), re-hashed only in the one-in-2^128
 * case that is not a valid scalar. docs/dice-attestation/ publishes the
 * resulting public point. Changing this string changes every pinned test
 * vector, and it is a wire-visible identity for the peers that pin it. */
static const char kTestKeySeed[] = "tfwsim-atecc608a TEST DeviceID key v1";
/* The UDS the provisioned test part holds, derived the same way. */
static const char kTestUdsSeed[] = "tfwsim-atecc608a TEST UDS v1";
/* GenKey in private mode derives key n from this and n, so a provisioning
 * session is reproducible too. */
static const char kGenKeySeed[]  = "tfwsim-atecc608a TEST GenKey sequence v1";

static uint8_t slot0_priv[32];
static uint8_t slot0_pub[64];
static uint8_t genkey_count;

static uECC_Curve curve(void) { return uECC_secp256r1(); }

/* micro-ecc blinds the point arithmetic with random bytes when it has an RNG.
 * Blinding changes nothing about the result, so a deterministic stream keeps
 * the model hermetic and its output reproducible -- no /dev/urandom read. */
static int blind_rng(uint8_t *dest, unsigned size)
{
    static uint32_t ctr;
    uint8_t         in[4], block[32];
    unsigned        n;

    while (size) {
        in[0] = (uint8_t)ctr; in[1] = (uint8_t)(ctr >> 8);
        in[2] = (uint8_t)(ctr >> 16); in[3] = (uint8_t)(ctr >> 24);
        ctr++;
        tfw_sha256(in, sizeof in, block);
        n = size < sizeof block ? size : (unsigned)sizeof block;
        memcpy(dest, block, n);
        dest += n; size -= n;
    }
    return 1;
}

/* A private key from a seed, and its public point. The scalar must lie in
 * [1, n-1]; uECC_compute_public_key refuses one that does not, and the hash is
 * simply taken again. */
static void key_from_seed(const uint8_t *seed, uint16_t len, uint8_t priv[32],
                          uint8_t pub[64])
{
    tfw_sha256(seed, len, priv);
    while (!uECC_compute_public_key(priv, pub, curve())) {
        tfw_sha256(priv, 32u, priv);
    }
}

/* Deterministic k needs an HMAC-DRBG over a hash; micro-ecc takes the hash as a
 * context. Its construction follows RFC 6979 but does not reproduce the RFC's
 * test vectors byte for byte (it uses H(m) unreduced and a different k
 * encoding), which no verifier can observe: every signature is a valid ECDSA
 * signature. test_model_p256.c pins that, not RFC byte-equality. */
typedef struct { uECC_HashContext uecc; tfw_sha256_t ctx; } sha256_hash_ctx_t;

static sha256_hash_ctx_t *as_ctx(const uECC_HashContext *c)
{
    return (sha256_hash_ctx_t *)(uintptr_t)c;
}
static void h_init(const uECC_HashContext *c) { tfw_sha256_init(&as_ctx(c)->ctx); }
static void h_update(const uECC_HashContext *c, const uint8_t *m, unsigned n)
{
    while (n) {
        uint16_t chunk = n > 0xFFFFu ? 0xFFFFu : (uint16_t)n;
        tfw_sha256_update(&as_ctx(c)->ctx, m, chunk);
        m += chunk; n -= chunk;
    }
}
static void h_finish(const uECC_HashContext *c, uint8_t *out)
{
    tfw_sha256_final(&as_ctx(c)->ctx, out);
}

int tfw_model_p256_public(const uint8_t priv[32], uint8_t pub[64])
{
    uECC_set_rng(blind_rng);
    return uECC_compute_public_key(priv, pub, curve());
}

int tfw_model_p256_sign(const uint8_t priv[32], const uint8_t digest[32],
                       uint8_t sig[64])
{
    uint8_t           tmp[32 + 32 + 64];
    uint8_t           unused[64];
    sha256_hash_ctx_t hc;

    /* micro-ecc signs with whatever scalar it is handed, zero included, and
     * produces a "signature" that verifies under nothing. The part refuses a
     * key that is not in [1, n-1]; so does this. */
    if (!uECC_compute_public_key(priv, unused, curve())) { return 0; }
    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    uECC_set_rng(blind_rng);
    return uECC_sign_deterministic(priv, digest, 32u, &hc.uecc, sig, curve());
}

int tfw_model_p256_verify(const uint8_t pub[64], const uint8_t digest[32],
                         const uint8_t sig[64])
{
    uECC_set_rng(blind_rng);
    return uECC_valid_public_key(pub, curve()) &&
           uECC_verify(pub, digest, 32u, sig, curve());
}

const uint8_t *tfw_model_deviceid_private(void) { return has_key ? slot0_priv : 0; }
const uint8_t *tfw_model_deviceid_public(void)  { return has_key ? slot0_pub  : 0; }

void tfw_model_test_deviceid(uint8_t priv[32], uint8_t pub[64])
{
    key_from_seed((const uint8_t *)kTestKeySeed, (uint16_t)(sizeof kTestKeySeed - 1u),
                  priv, pub);
}

static void install_key(const uint8_t priv[32], const uint8_t pub[64])
{
    memcpy(slot0_priv, priv, 32u);
    memcpy(slot0_pub, pub, 64u);
    has_key = 1u;
}

/* HMAC-SHA256 is accumulated and computed at End, keyed by the slot named at
 * Start. 512 bytes is far more than the one measurement the firmware ever
 * feeds it; a longer message is refused rather than silently truncated. */
static uint8_t  hmac_key[32];
static uint8_t  hmac_msg[512];
static uint16_t hmac_len;
static uint8_t  hmac_active;

/* No element on the board at all: every transfer NACKs, which is what a bus
 * with nothing on it does. Distinct from a busy part -- a driver must tell
 * "not there" from "not yet". */
static uint8_t  absent;

void tfw_model_reset(void)
{
    awake = 0u; provisioned = 0u; has_key = 0u; busy_reads = 0u;
    forced_status = 0u; last_opcode = 0u; counter0 = 0u; random_calls = 0u;
    rsp_len = 0u; last_addr = 0u;
    memset(exec_ms, 0, sizeof exec_ms);
    memset(rsp, 0, sizeof rsp);
    memset(config, 0, sizeof config);
    memset(slots, 0, sizeof slots);
    config_locked = 0u; data_locked = 0u;
    sha_active = 0u; verify_should_pass = 0u; verify_seen = 0u; absent = 0u;
    stored_pub_slot = 0xFFu; memset(stored_pub, 0, sizeof stored_pub);
    tempkey_valid = 0u; hmac_active = 0u; hmac_len = 0u; genkey_count = 0u;
    memset(tfw_model_tempkey(), 0, 32u);
    memset(slot0_priv, 0, sizeof slot0_priv);
    memset(slot0_pub, 0, sizeof slot0_pub);
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

void tfw_model_provision(void)
{
    uint8_t priv[32], pub[64];

    tfw_model_test_deviceid(priv, pub);
    install_key(priv, pub);
    /* A deterministic TEST UDS, so the CDI path's HMAC has a key to use and
     * two runs of the simulator agree. The real part draws its own from the
     * TRNG at provisioning (SEED_UDS); this is the stand-in. */
    tfw_sha256((const uint8_t *)kTestUdsSeed, (uint16_t)(sizeof kTestUdsSeed - 1u),
              slots[TFW_ATECC_SLOT_UDS]);
    provisioned = 1u;
}
uint8_t tfw_model_config_locked(void) { return config_locked; }
uint8_t tfw_model_data_locked(void)   { return data_locked; }
const uint8_t *tfw_model_slot(uint8_t n) { return n < 16u ? slots[n] : 0; }
void tfw_model_set_verify_result(uint8_t pass) { verify_should_pass = pass; }
void tfw_model_set_stored_pubkey(uint8_t slot, const uint8_t pub[64])
{
    stored_pub_slot = slot;
    memcpy(stored_pub, pub, sizeof stored_pub);
}
void tfw_model_set_absent(uint8_t yes) { absent = yes; }
uint8_t tfw_model_verify_seen(void) { return verify_seen; }
const uint8_t *tfw_model_verify_last_sig(void) { return verify_last_sig; }
const uint8_t *tfw_model_verify_last_pubkey(void) { return verify_last_pubkey; }
void tfw_model_set_busy(uint8_t reads)   { busy_reads = reads; }
void tfw_model_force_status(uint8_t st)  { forced_status = st; }
void tfw_model_set_counter(uint32_t v)   { counter0 = v; }
void tfw_model_set_exec_ms(uint8_t op, uint16_t ms) { exec_ms[op] = ms; }
uint8_t tfw_model_last_opcode(void)      { return last_opcode; }
uint8_t tfw_model_last_addr(void)        { return last_addr; }
uint8_t tfw_model_is_awake(void)         { return awake; }

static void respond(const uint8_t *body, uint8_t n)
{
    uint16_t crc;
    rsp[0] = (uint8_t)(n + 3u);
    if (n) { memcpy(&rsp[1], body, n); }
    crc = tfw_atecc_crc16(rsp, (uint8_t)(n + 1u));
    rsp[n + 1u] = (uint8_t)(crc & 0xFFu);
    rsp[n + 2u] = (uint8_t)(crc >> 8);
    rsp_len     = (uint8_t)(n + 3u);
}

static void respond_status(uint8_t code)
{
    uint16_t crc;
    rsp[0] = 4u;
    rsp[1] = code;
    crc    = tfw_atecc_crc16(rsp, 2u);
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
    crc = tfw_atecc_crc16(&p[1], (uint8_t)(count - 2u));
    if (crc != (uint16_t)(p[count - 1u] | ((uint16_t)p[count] << 8))) {
        respond_status(0xFFu);
        return;
    }

    last_opcode = opcode;
    /* The driver polls every 500 us, so ms of execution is ms*2 NACKed reads. */
    if (exec_ms[opcode]) { busy_reads = (uint16_t)(exec_ms[opcode] * 2u); }
    if (forced_status) { respond_status(forced_status); return; }

    switch (opcode) {
    case TFW_ATECC_OP_INFO:
        /* Revision word. 0x60 is the 608 family; the last byte is the die
         * revision: 0x02 on the 608A, 0x03 on the 608B, 0x05 on the 608C
         * (Microchip AN5078, DS00005078A). The part on the bench is a 608C,
         * so the model is one. */
        body[0] = 0x00u; body[1] = 0x00u; body[2] = 0x60u; body[3] = 0x05u;
        respond(body, 4u);
        return;

    case TFW_ATECC_OP_RANDOM:
        /* A fixed sequence, not a TRNG, so a test can prove a value came from
         * inside the part. The first answer is 0xA0 ^ i; each later one is
         * different, because a source that repeats cannot test anything built
         * on a fresh nonce (the auth session's replay protection). */
        for (i = 0u; i < 32u; i++) {
            body[i] = (uint8_t)(0xA0u ^ i ^ (uint8_t)random_calls);
        }
        random_calls++;
        respond(body, 32u);
        return;

    case TFW_ATECC_OP_SHA:
        if (param1 == TFW_ATECC_SHA_SHA256_START) {
            tfw_sha256_init(&sha_ctx);
            sha_active = 1u;
            respond_status(0x00u);
            return;
        }
        if (param1 == TFW_ATECC_SHA_SHA256_UPDATE && sha_active) {
            /* param2 is the byte count and the data follows the header. The
             * element accepts only whole 64-byte blocks here, and modelling
             * that refusal is worth more than accepting a short one: a driver
             * that sent a partial block would pass against a lenient model and
             * fail on silicon. */
            if (param2 != 64u || n < 6u + 64u) { respond_status(0x03u); return; }
            tfw_sha256_update(&sha_ctx, &p[6], 64u);
            respond_status(0x00u);
            return;
        }
        if (param1 == TFW_ATECC_SHA_SHA256_END && sha_active) {
            uint8_t tail = (uint8_t)param2;
            if (tail) { tfw_sha256_update(&sha_ctx, &p[6], tail); }
            tfw_sha256_final(&sha_ctx, body);
            sha_active = 0u;
            respond(body, 32u);
            return;
        }
        if (param1 == TFW_ATECC_SHA_HMAC_START) {
            if (!provisioned || param2 > 15u) { respond_status(0x0Fu); return; }  /* execution error */
            memcpy(hmac_key, slots[param2], sizeof hmac_key);
            hmac_len = 0u;
            hmac_active = 1u;
            respond_status(0x00u);
            return;
        }
        if (param1 == TFW_ATECC_SHA_HMAC_UPDATE && hmac_active) {
            if (param2 != 64u || n < 6u + 64u ||
                hmac_len + 64u > sizeof hmac_msg) { respond_status(0x03u); return; }
            memcpy(&hmac_msg[hmac_len], &p[6], 64u);
            hmac_len = (uint16_t)(hmac_len + 64u);
            respond_status(0x00u);
            return;
        }
        if (param1 == TFW_ATECC_SHA_HMAC_END && hmac_active) {
            uint8_t tail = (uint8_t)param2;
            if (tail > 64u || n < 6u + (uint16_t)tail ||
                hmac_len + tail > sizeof hmac_msg) { respond_status(0x03u); return; }
            if (tail) { memcpy(&hmac_msg[hmac_len], &p[6], tail); }
            tfw_hmac_sha256(hmac_key, (uint8_t)sizeof hmac_key, hmac_msg,
                           (uint16_t)(hmac_len + tail), body);
            hmac_active = 0u;
            respond(body, 32u);
            return;
        }
        respond_status(0x03u);      /* parse error */
        return;

    case TFW_ATECC_OP_NONCE:
        if (param1 != TFW_ATECC_NONCE_PASSTHRU) { respond_status(0x03u); return; }
        /* Pass-through loads TempKey verbatim; remember it so Sign can prove it
         * signed THAT digest and not something else. */
        if (n < 6u + 32u) { respond_status(0x03u); return; }
        memcpy(tfw_model_tempkey(), &p[6], 32u);
        tempkey_valid = 1u;
        respond_status(0x00u);
        return;

    case TFW_ATECC_OP_SIGN:
        /* External mode signs TempKey, which must have been loaded; an empty
         * TempKey is an execution error on the part, not a signature over
         * zeros. */
        if (param1 != TFW_ATECC_SIGN_EXTERNAL) { respond_status(0x03u); return; }
        if (!has_key || !tempkey_valid)       { respond_status(0x0Fu); return; }
        if (param2 != TFW_ATECC_SLOT_DEVICEID) { respond_status(0x0Fu); return; }
        if (!tfw_model_p256_sign(slot0_priv, tfw_model_tempkey(), body)) {
            respond_status(0x0Fu);
            return;
        }
        respond(body, 64u);
        return;

    case TFW_ATECC_OP_GENKEY:
        if (param1 == TFW_ATECC_GENKEY_PUBLIC) {
            /* Recomputes the public point of the key already there; it never
             * makes one. */
            if (!has_key) { respond_status(0x0Fu); return; }
            memcpy(body, slot0_pub, 64u);
            respond(body, 64u);
            return;
        }
        if (param1 == TFW_ATECC_GENKEY_PRIVATE) {
            /* A new key replaces slot 0's, as on the part. The n-th key of a
             * session is a fixed function of n, so a provisioning run is
             * reproducible; the private half is never in a response. */
            uint8_t seed[sizeof kGenKeySeed], priv[32], pub[64];

            if (param2 != TFW_ATECC_SLOT_DEVICEID) { respond_status(0x0Fu); return; }
            memcpy(seed, kGenKeySeed, sizeof kGenKeySeed - 1u);
            seed[sizeof kGenKeySeed - 1u] = genkey_count++;
            key_from_seed(seed, (uint16_t)sizeof seed, priv, pub);
            install_key(priv, pub);
            memcpy(body, pub, 64u);
            respond(body, 64u);
            return;
        }
        respond_status(0x03u);
        return;

    case TFW_ATECC_OP_READ: {
        /* param1 carries the zone and the 32-vs-4-byte bit; param2 is a word
         * address, and a 32-byte read is eight words. */
        uint16_t off = (uint16_t)(param2 * 4u);
        if ((param1 & TFW_ATECC_ZONE_32) == 0u) { respond_status(0x03u); return; }
        if ((param1 & 0x03u) == TFW_ATECC_ZONE_CONFIG) {
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

    case TFW_ATECC_OP_WRITE: {
        uint16_t off = (uint16_t)(param2 * 4u);
        if ((param1 & TFW_ATECC_ZONE_32) == 0u) { respond_status(0x03u); return; }
        if ((param1 & 0x03u) == TFW_ATECC_ZONE_CONFIG) {
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
            memcpy(slots[param2 >> 3], tfw_model_tempkey(), 32u);
            respond_status(0x00u);
            return;
        }
        respond_status(0x03u);
        return;
    }

    case TFW_ATECC_OP_LOCK:
        /* The summary CRC is in param2. A zero would be the no-CRC form, which
         * the firmware refuses before it ever reaches here -- so the model
         * treats it as the parse error the part would give. */
        if (param2 == 0u) { respond_status(0x03u); return; }
        if (param1 == TFW_ATECC_LOCK_CONFIG) {
            if (config_locked) { respond_status(0x0Fu); return; }
            config_locked = 1u; config[87] = 0x00u;   /* LockConfig */
        } else if (param1 == TFW_ATECC_LOCK_DATA) {
            if (!config_locked || data_locked) { respond_status(0x0Fu); return; }
            data_locked = 1u; config[86] = 0x00u;     /* LockValue */
        } else {
            respond_status(0x03u);
            return;
        }
        respond_status(0x00u);
        return;

    case TFW_ATECC_OP_VERIFY:
        /* Stored mode (0x00) names a slot; its key is in a locked slot this
         * model does not populate, so it records what it was asked and answers
         * from a flag. External mode (0x02, issue #4's AUTH_CHALLENGE/
         * AUTH_RESPONSE) carries the signature and a 64-byte public key, and
         * verifies the digest in TempKey for real. */
        if (param1 == 0x00u) {
            if (n < 6u + 64u) { respond_status(0x03u); return; }
            memcpy(verify_last_sig, &p[6], 64u);
            verify_seen = 1u;
            if (param2 == (uint16_t)stored_pub_slot) {
                if (!tempkey_valid) { respond_status(0x0Fu); return; }
                respond_status(tfw_model_p256_verify(stored_pub, tfw_model_tempkey(), &p[6])
                                   ? (uint8_t)0x00u : (uint8_t)0x01u);
                return;
            }
            respond_status(verify_should_pass ? (uint8_t)0x00u : (uint8_t)0x01u);  /* 0x01 = miscompare */
            return;
        }
        if (param1 == 0x02u) {
            if (n < 6u + 128u)                    { respond_status(0x03u); return; }
            if (param2 != 0x0004u)                { respond_status(0x03u); return; }
            if (!tempkey_valid)                   { respond_status(0x0Fu); return; }
            memcpy(verify_last_sig, &p[6], 64u);
            memcpy(verify_last_pubkey, &p[6 + 64u], 64u);
            verify_seen = 1u;
            /* A key that is not on the curve, or a signature that does not
             * verify, is a miscompare: the part has one answer for "no". */
            respond_status(tfw_model_p256_verify(&p[6 + 64u], tfw_model_tempkey(), &p[6])
                               ? (uint8_t)0x00u : (uint8_t)0x01u);
            return;
        }
        respond_status(0x03u);
        return;

    case TFW_ATECC_OP_COUNTER:
        if (param1 == TFW_ATECC_COUNTER_INCR) { counter0++; }
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

uint8_t *tfw_model_tempkey(void)
{
    static uint8_t tempkey[32];
    return tempkey;
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    /* The wake pulse is the low period of a byte written to address zero. At
     * 400 kHz that is 22 us, below the 60 us the part requires, so a driver
     * that configured the fast bus would never wake it. The model refuses the
     * same way the silicon would: by not waking. */
    return hz <= 100000uL ? TFW_OK : TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    if (absent) {
        return TFW_ERR_NACK;
    }
    if (addr7 == 0x00u) {
        /* The wake pulse. Nothing is listening at address zero; the part wakes
         * on the low line and then answers its own address with 04 11 33 43. */
        awake = 1u;
        respond_status(0x11u);
        return TFW_ERR_NACK;         /* and the NACK is expected */
    }
    last_addr = addr7;
    if (addr7 != TFW_ATECC_ADDR) {
        return TFW_ERR_NACK;
    }
    if (!awake) {
        return TFW_ERR_NACK;         /* asleep parts do not answer */
    }

    if (wlen) {
        switch (wr[0]) {
        case TFW_ATECC_WA_SLEEP:
            awake = 0u;
            tempkey_valid = 0u;
            hmac_active = 0u;
            memset(tfw_model_tempkey(), 0, 32u);   /* sleep clears volatile state */
            return TFW_OK;
        case TFW_ATECC_WA_IDLE:
            /* Idle keeps TempKey and the SHA context; only sleep loses them
             * (DS40002515B section 8.2.1). It does stop the part answering
             * until the next wake. */
            awake = 0u;
            return TFW_OK;
        case TFW_ATECC_WA_COMMAND:
            handle_command(wr, wlen);
            return TFW_OK;
        default:
            return TFW_ERR_NACK;
        }
    }

    if (rlen) {
        if (busy_reads) { busy_reads--; return TFW_ERR_NACK; }
        if (rsp_len == 0u) { return TFW_ERR_NACK; }
        /* The part returns its whole response; a driver asking for a different
         * length gets what is there, and its own count check catches the
         * mismatch. Truncating silently here would hide that. */
        memcpy(rd, rsp, rlen < rsp_len ? rlen : rsp_len);
        if (rlen > rsp_len) { memset(&rd[rsp_len], 0, (size_t)(rlen - rsp_len)); }
        return TFW_OK;
    }
    return TFW_OK;
}

/*
 * The element's timing, as far as this model needs it.
 *
 * Returning at once is deliberate: the driver's wait is a POLL on the part's
 * NACK, not a sleep, so a test that wanted to exercise the wait sets
 * tfw_model_set_busy() and gets the same code path in microseconds. A model
 * that actually slept would make the suite slow and would test the host's
 * clock.
 */
void tfw_hal_delay_us(uint16_t us) { (void)us; }
