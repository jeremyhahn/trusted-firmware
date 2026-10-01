/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Attestation: prove, to a verifier's challenge, what this device is running.
 *
 * ATTEST(nonce[16])  computes fresh evidence and returns page 0
 * ATTEST(page)       returns a later page of the evidence just computed
 *
 * PAGED, because the evidence is 164 bytes and a frame payload on this class of
 * part is 64. The alternative -- enlarging every frame buffer -- would charge
 * the cost to every command on every board to serve the one command that needs
 * it, and would put the payload out of reach of the smaller parts entirely.
 *
 * FRESHNESS IS THE VERIFIER'S NONCE, always. No part in this family has a
 * secure clock, so the device cannot contribute freshness and does not pretend
 * to: UptimeMs says how long it has been up, which is useful context and is not
 * a timestamp. A verifier that accepted evidence without having supplied the
 * nonce in it would be accepting a replay.
 */

#include <string.h>
#include "attest.h"
#include "ag/crypto.h"
#include "ag/config.h"
#include "ag/hal.h"
#include "ag/protocol.h"
#include "../core/handoff.h"
#include "../core/platform_id.h"

/* Offsets into the evidence, per include/ag/protocol.h. */
#define OFF_VERSION   0u
#define OFF_NONCE     1u
#define OFF_DEVICE_ID 17u
#define OFF_FW_HASH   33u
#define OFF_BOOTCOUNT 65u
#define OFF_UPTIME    69u
#define OFF_ALG       73u
#define OFF_SIG       74u
#define OFF_RESERVED  138u

static uint8_t evidence[AG_ATTEST_LEN];
static uint8_t have_evidence;

void ag_attest_reset_state(void)
{
    memset(evidence, 0, sizeof evidence);
    have_evidence = 0u;
}

/*
 * THESE READ THEIR STARTUP SOURCES DIRECTLY, NOT THE EVIDENCE BUFFER.
 *
 * `evidence` only holds a real DeviceID/FWHash after build() has run at
 * least once -- i.e. after ATTEST has been called with a nonce. A host that
 * sends ATTEST_CERT first (src/caps/alias.c's mint(), which calls these two
 * accessors) would otherwise get a certificate correctly signed over an
 * all-zero UEID and measurement, on a fully provisioned device, with nothing
 * about the exchange indicating anything had gone wrong. Reading the live
 * sources here means the two callers -- this file's own build() and
 * alias.c's gather() -- agree with each other regardless of which command
 * ran first.
 */
const uint8_t *ag_attest_device_id(void)
{
    return ag_platform_id_serial();
}

const uint8_t *ag_attest_fw_hash(void)
{
    static const uint8_t zero[AG_SHA256_LEN] = { 0 };
    const uint8_t *measurement = ag_boot_handoff_measurement();
    return measurement ? measurement : zero;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static ag_status_t build(const uint8_t *nonce)
{
    uint8_t     digest[AG_SHA256_LEN];
    uint8_t     alg = AG_ALG_NONE;
    uint32_t    boot = 0u;
    ag_status_t st;

    memset(evidence, 0, sizeof evidence);
    evidence[OFF_VERSION] = AG_ATTEST_VERSION;
    memcpy(&evidence[OFF_NONCE], nonce, AG_ATTEST_NONCE_LEN);

    /*
     * DeviceID: the serial agprov baked into the platform-reserved EEPROM
     * block before this part was ever flashed (src/core/platform_id.c).
     * FWHash: the measurement ag-boot verified and handed off
     * (src/core/handoff.c) -- not a value this file recomputes, so the
     * evidence reports what was actually checked rather than what this build
     * believes about itself.
     *
     * Both stay ZERO rather than fabricated wherever their source is absent
     * -- an unprovisioned platform block, a stock bootloader, or no
     * bootloader at all -- so a verifier can see an unprovisioned device for
     * what it is, where a plausible-looking made-up value would be believed.
     */
    memcpy(&evidence[OFF_DEVICE_ID], ag_platform_id_serial(), AG_ATTEST_ID_LEN);
    {
        const uint8_t *fw_hash = ag_boot_handoff_measurement();
        if (fw_hash) {
            memcpy(&evidence[OFF_FW_HASH], fw_hash, AG_SHA256_LEN);
        }
    }

    /* The boot counter needs a monotonic source. Absent one, it stays zero and
     * says so by the tier -- rather than using a RAM counter that resets with
     * the device and would make every boot look like the first. */
    if (ag_crypto_counter(0u, &boot) == AG_OK) {
        put_u32(&evidence[OFF_BOOTCOUNT], boot);
    }

    put_u32(&evidence[OFF_UPTIME], (uint32_t)ag_hal_millis());

    /* Sign everything before the signature field. The nonce is inside that
     * span, which is what binds this evidence to this challenge. */
    st = ag_crypto_sha256(evidence, OFF_ALG, digest);
    if (st != AG_OK) {
        return st;
    }
    st = ag_crypto_sign(digest, &evidence[OFF_SIG], &alg);
    if (st != AG_OK) {
        return st;
    }
    evidence[OFF_ALG] = alg;

    have_evidence = 1u;
    return AG_OK;
}

ag_status_t ag_attest_cmd(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t n = ag_reader_remaining(r);
    uint8_t page;
    uint8_t off, len;

    if (n == AG_ATTEST_NONCE_LEN) {
        const uint8_t *nonce = ag_read_blob(r, AG_ATTEST_NONCE_LEN);
        ag_status_t    st;

        if (!nonce) {
            return AG_ERR_BAD_ARGC;
        }
        st = build(nonce);
        if (st != AG_OK) {
            return st;
        }
        page = 0u;
    } else if (n == 1u) {
        page = ag_read_u8(r);
        /* A page request with no evidence behind it would return zeros that
         * look like a valid answer. Refused instead. */
        if (!have_evidence) {
            return AG_ERR_BAD_ARGC;
        }
    } else {
        return AG_ERR_BAD_ARGC;
    }

    /* Computed wide, then bounded, then narrowed. page * AG_ATTEST_PAGE
     * overflows a uint8_t at page 6 and wraps: page 99 lands on offset 144,
     * which is inside the evidence and would be served as though it had been
     * asked for. The bound has to be checked at the wide width or it is not a
     * bound at all. */
    {
        uint16_t off16 = (uint16_t)page * (uint16_t)AG_ATTEST_PAGE;
        if (off16 >= (uint16_t)AG_ATTEST_LEN) {
            return AG_ERR_RANGE;
        }
        off = (uint8_t)off16;
    }
    len = (uint8_t)(AG_ATTEST_LEN - off);
    if (len > AG_ATTEST_PAGE) {
        len = AG_ATTEST_PAGE;
    }

    ag_write_u8(w, page);
    ag_write_u8(w, (uint8_t)AG_ATTEST_LEN);
    ag_write_blob(w, &evidence[off], len);
    return AG_OK;
}
