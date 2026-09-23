/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Unprotected CWT Claims Set that travels in tcg-dice-UCCS-evidence.
 *
 * WHY THE NONCE LIVES HERE AND NOT IN DiceTcbInfo.vendorInfo. TCG DICE
 * Attestation Architecture r23 section 6.1.4 defines this extension for exactly
 * this: a CWT claims set carried in a certificate, unsigned on its own because
 * "the certificate signature authenticates, and integrity protects UCCS
 * contents". vendorInfo is an OCTET STRING whose meaning is the vendor's, so a
 * nonce placed there is a private convention every verifier has to be told
 * about. A nonce placed here is a registered EAT claim in a registered
 * extension, and a verifier that has never heard of this product can still read
 * it.
 *
 * THIS IS NOT A CBOR LIBRARY AND MUST NOT BECOME ONE. The plan rejected CBOR on
 * the wire because a general encoder does not fit; that judgement has not
 * changed. What is here is four emit routines over one head-encoding primitive,
 * which is all a claims set of fixed shape needs. Everything it emits is either
 * a constant from flash or one of three fixed-width values.
 *
 * THE CLAIM KEYS, and where each was verified:
 *
 *   nonce         10   RFC 9711 (EAT) section 10.2, "Claim Key: 10", value
 *                      bstr 8..64. Ours is 32: the enrolment profile carries
 *                      SHA-256 of an ACME key authorization, because the raw
 *                      "<token>.<RFC 7638 thumbprint>" is 87 bytes and does not
 *                      fit the claim at all. The HOST hashes it; the device
 *                      receives 32 bytes and places them verbatim.
 *   ueid         256   RFC 9711 section 10.2, "CWT Claim Key: 256", value
 *                      bstr 7..33. Section 4.2.1.1 requires a leading TYPE
 *                      BYTE; 0x01 (RAND) is the type for a stored random
 *                      identifier of at least 128 bits, which is what a
 *                      16-byte DeviceID is.
 *   measurements 273   RFC 9711 section 10.2, value an array of
 *                      [content-type, content]. Section 4.2.16 requires an
 *                      EVIDENCE CoSWID when the CoSWID format is used, and
 *                      names the format by CoAP Content-Format id -- 258,
 *                      application/swid+cbor, registered by RFC 9393 section
 *                      6.4.
 *
 * EAT has NO scalar "firmware hash" claim. Carrying the measurement therefore
 * costs a CoSWID, and the alternative -- a private claim key -- would be the
 * same private convention this extension was chosen to avoid. The CoSWID here
 * is the smallest RFC 9393 permits: the four mandatory fields plus one file
 * entry holding one hash-entry. hash-alg-id 1 is sha-256 in the IANA Named
 * Information Hash Algorithm registry, which RFC 9393 section 2.9.1 points at.
 *
 * CORE-DETERMINISTIC CBOR, RFC 8949 section 4.2.1, and it is a requirement
 * rather than tidiness. DICE r23 section 6.1.4 makes the issuer responsible for
 * the extension's contribution to the CDI, so one claims set must have exactly
 * one encoding -- otherwise the verifier re-encodes what it parsed, gets
 * different bytes, and the contribution no longer means what the spec says.
 * Three rules produce it and all three are structural here: definite lengths
 * only (head() has no indefinite form), the smallest head that fits the
 * argument (head() picks it), and map keys in ascending order (written that way
 * throughout, including inside the CoSWID).
 */

#include <string.h>
#include "uccs.h"
#include "ag/crypto.h"
#include "ag/progmem.h"
#include "ag/protocol.h"

/* CBOR major types, RFC 8949 section 3.1. */
#define MT_UINT  0u
#define MT_BSTR  2u
#define MT_TSTR  3u
#define MT_ARRAY 4u
#define MT_MAP   5u

/* CWT claim keys. */
#define CLAIM_NONCE        10u
#define CLAIM_UEID        256u
#define CLAIM_MEASUREMENTS 273u

/* RFC 9711 section 4.2.1.1: a stored random identifier of at least 128 bits. */
#define UEID_TYPE_RAND 0x01u

/* CoAP Content-Format for application/swid+cbor, RFC 9393 section 6.4. */
#define CF_SWID_CBOR 258u

/* CoSWID map keys, RFC 9393 section 2.3 and the index table in its appendix. */
#define CO_TAG_ID       0u
#define CO_SOFTWARE_NAME 1u
#define CO_ENTITY        2u
#define CO_EVIDENCE      3u
#define CO_HASH          7u
#define CO_TAG_VERSION  12u
#define CO_FILE         17u
#define CO_FS_NAME      24u
#define CO_ENTITY_NAME  31u
#define CO_ROLE         33u
#define ROLE_TAG_CREATOR 1u

/* IANA Named Information Hash Algorithm registry: 1 is sha-256. */
#define NI_SHA256 1u

/* Everything outside the CoSWID: the three map keys, the nonce and ueid byte
 * strings and the measurements wrapper. Stated as an equation so a change to
 * either constant that does not hold it is a compile error rather than a
 * claims set whose inner length field disagrees with its contents. */
#define UCCS_ENVELOPE_LEN 67u
_Static_assert(AG_UCCS_LEN == UCCS_ENVELOPE_LEN + AG_UCCS_COSWID_LEN,
               "the claims set length and its CoSWID length disagree");

/*
 * Product constants. They identify the SOFTWARE, not the device -- the device
 * is the ueid claim -- so they are the same in every image of a given firmware
 * and contribute to the CDI by being part of what is measured.
 *
 * TAG_ID is uuid5(NAMESPACE_URL,
 * "https://github.com/jeremyhahn/arduino-generic/coswid/firmware"), computed
 * once and frozen. It is written out rather than derived because a tag id that
 * a device computes is a tag id that changes when the derivation changes.
 */
static const uint8_t TAG_ID[16] AG_PROGMEM = {
    0x5E, 0xEC, 0x78, 0x74, 0xAA, 0xA5, 0x57, 0xFD,
    0xAA, 0xAE, 0xF3, 0xF9, 0xFE, 0x0C, 0xCF, 0xB4
};
static const uint8_t SW_NAME[5]  AG_PROGMEM = { 'a', 'g', '-', 'f', 'w' };
static const uint8_t ENT_NAME[2] AG_PROGMEM = { 'a', 'g' };
static const uint8_t FS_NAME[3]  AG_PROGMEM = { 'a', 'p', 'p' };

/*
 * One head, four shapes.
 *
 * CBOR heads are BIG-endian, which is why this shifts rather than calling
 * ag_write_u16 -- that one is little-endian because the wire is, and using it
 * here would produce a claims set no decoder accepts.
 *
 * Only the three shortest argument forms exist. Nothing in this claims set is
 * longer than 65535, and an encoder that could emit a form it never emits is
 * flash spent on a branch no test can reach.
 */
static void head(ag_writer_t *w, uint8_t major, uint16_t arg)
{
    uint8_t mt = (uint8_t)(major << 5);

    if (arg < 24u) {
        ag_write_u8(w, (uint8_t)(mt | (uint8_t)arg));
    } else if (arg < 256u) {
        ag_write_u8(w, (uint8_t)(mt | 24u));
        ag_write_u8(w, (uint8_t)arg);
    } else {
        ag_write_u8(w, (uint8_t)(mt | 25u));
        ag_write_u8(w, (uint8_t)(arg >> 8));
        ag_write_u8(w, (uint8_t)arg);
    }
}

/* A byte or text string whose content is in flash. One routine for both,
 * because the only difference between them is the major type. */
static void str_pgm(ag_writer_t *w, uint8_t major, const uint8_t *p, uint8_t n)
{
    uint8_t i;

    head(w, major, n);
    for (i = 0u; i < n; i++) {
        ag_write_u8(w, ag_pgm_u8(&p[i]));
    }
}

static void bstr(ag_writer_t *w, const uint8_t *p, uint8_t n)
{
    head(w, MT_BSTR, n);
    ag_write_blob(w, p, n);
}

/*
 * The evidence CoSWID: the smallest tag RFC 9393 permits that carries a
 * measurement.
 *
 * tag-id, software-name, entity and tag-version are mandatory and say nothing
 * interesting; they are here because a tag without them is not a CoSWID. The
 * payload is the one file entry, and label 3 (evidence) rather than 6 (payload)
 * because RFC 9711 section 4.2.16 requires evidence: a payload tag would claim
 * this is what was INSTALLED, where what we can honestly say is what was
 * MEASURED.
 */
static void coswid(ag_writer_t *w, const uint8_t *fw_hash)
{
    head(w, MT_MAP, 5u);

    head(w, MT_UINT, CO_TAG_ID);
    str_pgm(w, MT_BSTR, TAG_ID, (uint8_t)sizeof TAG_ID);

    head(w, MT_UINT, CO_SOFTWARE_NAME);
    str_pgm(w, MT_TSTR, SW_NAME, (uint8_t)sizeof SW_NAME);

    head(w, MT_UINT, CO_ENTITY);
    head(w, MT_MAP, 2u);
    head(w, MT_UINT, CO_ENTITY_NAME);
    str_pgm(w, MT_TSTR, ENT_NAME, (uint8_t)sizeof ENT_NAME);
    head(w, MT_UINT, CO_ROLE);
    head(w, MT_UINT, ROLE_TAG_CREATOR);

    head(w, MT_UINT, CO_EVIDENCE);
    head(w, MT_MAP, 1u);
    head(w, MT_UINT, CO_FILE);
    head(w, MT_MAP, 2u);
    head(w, MT_UINT, CO_HASH);
    head(w, MT_ARRAY, 2u);          /* hash-entry = [hash-alg-id, hash-value] */
    head(w, MT_UINT, NI_SHA256);
    bstr(w, fw_hash, AG_SHA256_LEN);
    head(w, MT_UINT, CO_FS_NAME);
    str_pgm(w, MT_TSTR, FS_NAME, (uint8_t)sizeof FS_NAME);

    head(w, MT_UINT, CO_TAG_VERSION);
    head(w, MT_UINT, 0u);
}

ag_status_t ag_uccs_encode(ag_writer_t *w,
                           const uint8_t *nonce, uint8_t nonce_len,
                           const uint8_t *ueid,
                           const uint8_t *fw_hash)
{
    /* The width is the contract, so it is checked rather than trusted. A short
     * nonce padded to length would be a claims set that says the verifier
     * challenged with bytes it never sent. */
    if (nonce_len != AG_ALIAS_NONCE_LEN) {
        return AG_ERR_RANGE;
    }

    head(w, MT_MAP, 3u);

    head(w, MT_UINT, CLAIM_NONCE);
    bstr(w, nonce, AG_ALIAS_NONCE_LEN);

    /* The type byte is part of the UEID, not a wrapper around it: RFC 9711
     * gives it as the first byte of the value, which is why this emits a
     * 17-byte string and not a 16-byte one inside something. */
    head(w, MT_UINT, CLAIM_UEID);
    head(w, MT_BSTR, (uint16_t)(AG_ATTEST_ID_LEN + 1u));
    ag_write_u8(w, UEID_TYPE_RAND);
    ag_write_blob(w, ueid, AG_ATTEST_ID_LEN);

    head(w, MT_UINT, CLAIM_MEASUREMENTS);
    head(w, MT_ARRAY, 1u);          /* measurements-type = [+ format] */
    head(w, MT_ARRAY, 2u);          /* [content-type, content] */
    head(w, MT_UINT, CF_SWID_CBOR);
    head(w, MT_BSTR, AG_UCCS_COSWID_LEN);
    coswid(w, fw_hash);

    /* Asked once, at the end. The writer refuses an overflowing write outright
     * rather than truncating it, so what is in the buffer is either all of the
     * claims set or not a claims set at all. */
    if (!ag_writer_ok(w)) {
        return AG_ERR_TOO_LONG;
    }
    return AG_OK;
}
