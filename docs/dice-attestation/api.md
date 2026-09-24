# DICE attestation — API

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## One wire transport

This repo defines exactly one framing for the command protocol, used
regardless of the physical link (UART or USB-CDC): `include/ag/protocol.h`.

```
off  size  field
 0    1    SOF      0xA5
 1    1    LEN      2 + payload_len
 2    1    SEQ      request token, echoed verbatim
 3    1    OP       request 0x00..0x7F, response OP|0x80, event 0xFF
 4    1    STATUS   responses and events only
 5+   N    PAYLOAD
 ..   2    CRC16    little-endian, over bytes [1 .. end of payload]
```

Everything below is the `PAYLOAD` of that frame. There is no second wire
shape for this feature.

## `ATTEST` — 0x60

Nonce-bound evidence, paged because 164 bytes does not fit one payload.

```
request 1: ATTEST(nonce[16])   compute fresh evidence, return page 0
request 2: ATTEST(page[1])     return a later page of the same evidence
response:  [page:1][total:1][bytes:1..48]
```

`total` is always 164 (`AG_ATTEST_LEN`); a page beyond it is `AG_ERR_RANGE`.
Evidence is **regenerated** on each `ATTEST(nonce)` call, not cached across
nonces — a new nonce invalidates in-flight paging of the previous one.

### Evidence frame — exact layout, `src/caps/attest.c`

```
off  size  field
  0    1   Version     AG_ATTEST_VERSION = 1
  1   16   Nonce       the verifier's 16-byte challenge, echoed
 17   16   DeviceID    AG_ATTEST_ID_LEN; zero until the bootloader handoff lands (architecture.md)
 33   32   FWHash      SHA-256 of the measured firmware; zero, same reason
 65    4   BootCount   little-endian; 0 if the provider has no counter
 69    4   UptimeMs    little-endian; elapsed time since boot, NOT a timestamp
 73    1   AlgID       0x00 none, 0x01 ECDSA-P256-SHA256, 0x02 Ed25519, 0x10 HMAC-SHA256
 74   64   Signature   raw r‖s (ECDSA) or R‖S (Ed25519), or a 32-byte HMAC padded with 32 zero bytes
138   26   Reserved    zero
```

164 bytes total. The signature covers bytes `[0, 74)` — everything before
the signature field, which includes the nonce; that inclusion is what binds
this evidence to this challenge. `AlgID` lets a verifier interpret
`Signature` without knowing the device's tier out of band.

## `ATTEST_CERT` — 0x63

An alias certificate carrying the same challenge, in the shape TCG DICE
Attestation Architecture r23 defines. Absent (`AG_ERR_UNSUPPORTED`) on any
tier with no asymmetric key, and on `boards/attiny85` and
`boards/atmega32u4` regardless of tier — see `engineering.md`.

```
request 1: ATTEST_CERT(nonce[32])   mint, return page 0
request 2: ATTEST_CERT(page[1])     return a later page
response:  [page:1][total:2 little-endian][bytes:0..60]
```

`total` is 470–472 (the DER encoding of the ECDSA signature varies by 1–2
bytes per INTEGER). Pages are regenerated per request, never cached — a 328P
does not have the RAM to hold the certificate whole. A page at or past the
end is `AG_ERR_RANGE`.

### The certificate, `src/caps/alias.c`

| Field | Value |
|---|---|
| version | v3 |
| serialNumber | 8 bytes derived from the nonce; leading byte forced into `0x40`–`0x7F` for a minimal positive DER INTEGER |
| signature / signatureAlgorithm | `ecdsa-with-SHA256`, `1.2.840.10045.4.3.2` |
| issuer | `CN=ag DeviceID` |
| validity | `20200101000000Z` .. `99991231235959Z` — RFC 5280 §4.1.2.5's "no well-defined expiration"; carries no information, no board here has a clock |
| subject | `CN=ag Alias` |
| subjectPublicKeyInfo | `id-ecPublicKey` / `prime256v1`; **the element's DeviceID point** — there is no separate Alias key (`architecture.md`) |
| extensions | one: `tcg-dice-UCCS-evidence`, **critical** |

`TBSCertificate` is a 230-byte PROGMEM template (`TBS_PREFIX`) with two holes
patched in place (serial number, SPKI) plus the 152-byte claims set appended
as the tail — `_Static_assert(TBS_PREFIX_LEN == 230)`,
`_Static_assert(TBS_LEN == 382)`.

### The UCCS claims set — `tcg-dice-UCCS-evidence`, OID `2.23.133.5.4.6`

```
UccsEvidence ::= SEQUENCE { uccs OCTET STRING }
```

152 bytes of core-deterministic CBOR (RFC 8949 §4.2.1), `src/caps/uccs.c`:

```
{ 10:  h'<32-byte nonce>',                          -- CWT claim "nonce", RFC 9711 §10.2
  256: h'01 <16-byte device id>',                    -- CWT claim "ueid"; leading byte 0x01 = RAND, §4.2.1.1
  273: [[258, <85-byte CoSWID>]] }                    -- CWT claim "measurements"; 258 = application/swid+cbor, RFC 9393 §6.4
```

The nonce here is `AG_ALIAS_NONCE_LEN` (32 bytes, the enrolment challenge —
**not** the evidence frame's 16-byte freshness nonce; the two are different
values and the dispatcher refuses each command the other's width). The host
computes it as `SHA-256(keyAuthorization)` for an ACME `device-attest-01`
flow; the device never sees the key authorization and never hashes anything —
it places the 32 bytes it is given verbatim.

CoSWID (RFC 9393), the smallest tag the spec permits carrying one measurement:

```
{ 0:  h'<16-byte tag-id>',        -- tag-id, a frozen product constant
  1:  "ag-fw",                    -- software-name
  2:  {31: "ag", 33: 1},          -- entity: entity-name, role=tag-creator
  3:  {17: {7: [1, h'<sha-256 fw hash>'], 24: "app"}},  -- evidence -> file -> hash-entry(hash-alg-id=1 sha-256), fs-name
  12: 0 }                         -- tag-version
```

Label `3` (evidence) is used rather than `6` (payload): RFC 9711 §4.2.16
requires an evidence CoSWID, and a payload tag would claim this is what was
*installed* rather than what was *measured*.

## `CAPS` — 0x00, the fields this feature depends on

```
off  size  field
  9    1   CryptoTier     ag_crypto_tier_t: 0 none, 1 software, 2 secure element, 3 tpm2 (reserved, unimplemented)
 10    4   Features       little-endian bitmap; bit 10 = AG_FEATURE_ATTEST, bit 13 = AG_FEATURE_ALIAS_CERT
 14    2   Algorithms     little-endian bitmap; bit 0 ECDSA-P256-SHA256, bit 1 Ed25519, bit 2 HMAC-SHA256; absent on firmware built before it existed
```

A host reads `CryptoTier` and `Algorithms` before it decides what any
evidence from `ATTEST`/`ATTEST_CERT` is worth — never after.
