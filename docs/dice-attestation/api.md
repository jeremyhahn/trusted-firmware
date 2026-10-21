# DICE attestation — API

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-18
> Conflicts with this document are defects. Fix them here first.

## One wire transport

This repo defines exactly one framing for the command protocol, used
regardless of the physical link (UART or USB-CDC): `include/tfw/protocol.h`.

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

`total` is always 164 (`TFW_ATTEST_LEN`); a page beyond it is `TFW_ERR_RANGE`.
Evidence is **regenerated** on each `ATTEST(nonce)` call, not cached across
nonces — a new nonce invalidates in-flight paging of the previous one.

### Evidence frame — exact layout, `src/caps/attest.c`

```
off  size  field
  0    1   Version     TFW_ATTEST_VERSION = 1
  1   16   Nonce       the verifier's 16-byte challenge, echoed
 17   16   DeviceID    TFW_ATTEST_ID_LEN; zero until the bootloader handoff lands (architecture.md)
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
Attestation Architecture r23 defines. Absent (`TFW_ERR_UNKNOWN_CMD`) on every
`CRYPTO=soft` and `CRYPTO=none` image, and on `boards/attiny85` and
`boards/atmega32u4` regardless of tier — see `engineering.md`. A
`CRYPTO=auto` image carries it: with an element present the element's key is
the subject; with no element the soft fallback certifies its P-256 DeviceID,
and answers `TFW_ERR_NOT_PROVISIONED` when tfw-boot handed off no DeviceID
seed. A build that carries the command under a provider with no asymmetric key
answers `TFW_ERR_UNSUPPORTED`.

```
request 1: ATTEST_CERT(nonce[32])   mint, return page 0
request 2: ATTEST_CERT(page[1])     return a later page
response:  [page:1][total:2 little-endian][bytes:0..60]
```

`total` is 471–473 (the DER encoding of the ECDSA signature varies by 1–2
bytes per INTEGER). Pages are regenerated per request, never cached — a 328P
does not have the RAM to hold the certificate whole. A page at or past the
end is `TFW_ERR_RANGE`.

### The certificate, `src/caps/alias.c`

| Field | Value |
|---|---|
| version | v3 |
| serialNumber | 8 bytes derived from the nonce; leading byte forced into `0x40`–`0x7F` for a minimal positive DER INTEGER |
| signature / signatureAlgorithm | `ecdsa-with-SHA256`, `1.2.840.10045.4.3.2` |
| issuer | `CN=tfw DeviceID` |
| validity | `20200101000000Z` .. `99991231235959Z` — RFC 5280 §4.1.2.5's "no well-defined expiration"; carries no information, no board here has a clock |
| subject | `CN=tfw Alias` |
| subjectPublicKeyInfo | `id-ecPublicKey` / `prime256v1`; **the element's DeviceID point** — there is no separate Alias key (`architecture.md`) |
| extensions | one: `tcg-dice-UCCS-evidence`, **critical** |

`TBSCertificate` is a 232-byte PROGMEM template (`TBS_PREFIX`) with two holes
patched in place (serial number, SPKI) plus the 151-byte claims set appended
as the tail — `_Static_assert(TBS_PREFIX_LEN == 232)`,
`_Static_assert(TBS_LEN == 383)`.

### The UCCS claims set — `tcg-dice-UCCS-evidence`, OID `2.23.133.5.4.6`

```
UccsEvidence ::= SEQUENCE { uccs OCTET STRING }
```

151 bytes of core-deterministic CBOR (RFC 8949 §4.2.1), `src/caps/uccs.c`:

```
{ 10:  h'<32-byte nonce>',                          -- CWT claim "nonce", RFC 9711 §10.2
  256: h'01 <16-byte device id>',                    -- CWT claim "ueid"; leading byte 0x01 = RAND, §4.2.1.1
  273: [[258, <84-byte CoSWID>]] }                    -- CWT claim "measurements"; 258 = application/swid+cbor, RFC 9393 §6.4
```

The nonce here is `TFW_ALIAS_NONCE_LEN` (32 bytes, the enrolment challenge —
**not** the evidence frame's 16-byte freshness nonce; the two are different
values and the dispatcher refuses each command the other's width). The host
computes it as `SHA-256(keyAuthorization)` for an ACME `device-attest-01`
flow; the device never sees the key authorization and never hashes anything —
it places the 32 bytes it is given verbatim.

CoSWID (RFC 9393), the smallest tag the spec permits carrying one measurement:

```
{ 0:  h'<16-byte tag-id>',        -- tag-id, a frozen product constant
  1:  "tfw",                      -- software-name
  2:  {31: "tfw", 33: 1},         -- entity: entity-name, role=tag-creator
  3:  {17: {7: [1, h'<32 bytes>'], 24: "app"}},  -- evidence -> file -> hash-entry(hash-alg-id=1 sha-256), fs-name
  12: 0 }                         -- tag-version
```

The hash-entry's 32 bytes are the running application's evidence, what its
`tfw_app_evidence` hook wrote ([application-sdk/api.md](../application-sdk/api.md)).
Where the image has no application, the application is not running, or its hook
returned anything but `TFW_OK`, they are the firmware hash `tfw-boot` measured
(`tfw_attest_measurement`, `src/caps/attest.c`).
The claim is never absent: go-xkms's possession template requires `273`. The
host reads the 32 bytes with `tfw.ApplicationEvidence`.

Label `3` (evidence) is used rather than `6` (payload): RFC 9711 §4.2.16
requires an evidence CoSWID, and a payload tag would claim this is what was
*installed* rather than what was *measured*.

## `ATTEST_CSR` — 0x64

Proof of possession of the DeviceID key at ACME finalize
([adr-possession-at-acme-finalize.md](adr-possession-at-acme-finalize.md)).
Built only on the ATmega2560's element tier. It needs `ATTEST_CERT`, so
`boards/attiny85` has no handler; the `boards/atmega32u4` and
`boards/atmega328p` profiles turn it off by name (#6) — the 328P because it puts the element image over its flash budget;
see `engineering.md`. Each answers `TFW_ERR_UNKNOWN_CMD` and reports `CAPS`
bit 15 clear. On a build that has it, a tier with no asymmetric key answers
`TFW_ERR_UNSUPPORTED` and an unprovisioned element `TFW_ERR_NOT_PROVISIONED`,
exactly as `ATTEST_CERT`'s mint does.

```
request:  ATTEST_CSR(b[32])
response: [sig:64]   r‖s, P-256 over SHA-256(CertificationRequestInfo)
```

One exchange: nothing is paged and nothing is kept, so it does not disturb an
`ATTEST_CERT` paging in progress or leave a certificate for one to read. Any
request length but 32 is `TFW_ERR_BAD_ARGC`.

**`b` must be the nonce of the certificate last minted.** The request carries
the claims set that certificate carries, and its measurements claim (273) is
the 32 bytes the mint placed, kept in RAM beside the nonce for the pages that
follow (`tfw_alias_measurement_for`, `src/caps/alias.c`). `ATTEST_CSR` signs
those bytes and does not call the application's `tfw_app_evidence` again. An
application may report its current state, so its evidence may have changed
since the mint, and go-xkms rebuilds this request from the certificate: a
fresh value would sign a request it cannot match. The enrollment flow
satisfies this already, since the possession signer asks with the nonce of
the attestation it just made.

`b` with no certificate behind it is `TFW_ERR_BAD_ARGC`: nothing minted since
reset, a mint for another nonce since, or a mint that failed (a mint clears
what the last one kept before it starts). It is the code an `ATTEST_CERT` page
request with no certificate behind it already answers, for the same reason:
the argument names a certificate that does not exist. `TFW_ERR_BAD_STATE` is
not used, because the OTA ADR defines it for the OTA state and an older host
names it so. Nothing is kept across a reset, and no EEPROM or flash is
written.

### What the device signs, `src/caps/csr.c`

A PKCS#10 `CertificationRequestInfo` (RFC 2986 §4.1), 365 bytes, every one of
them fixed except four holes the device fills from its own state:

```
off  size  field
  0    9   30 82 01 69  02 01 00  30 00         SEQUENCE, version 0, subject = empty Name
  9   27   SubjectPublicKeyInfo header          id-ecPublicKey / prime256v1, BIT STRING 00 04  -- from TBS_PREFIX
 36   64   HOLE: the DeviceID point X‖Y
100   57   attributes [0]: one extensionRequest (1.2.840.113549.1.9.14) holding
             subjectAltName (2.5.29.17), critical:
               otherName id-on-permanentIdentifier (1.3.6.1.5.5.7.8.3),
               PermanentIdentifier { identifierValue UTF8String (34) }
157   34   HOLE: the UEID, 17 bytes as 34 lowercase hex digits
191   23   tcg-dice-UCCS-evidence Extension header, critical                    -- from TBS_PREFIX
214  151   HOLE: the UCCS claims set above, with b as the nonce
```

The `identifierValue` is the lowercase hex of the full UEID — the type byte
then the DeviceID, 34 digits, e.g. `01a1b2c3d4e5f60718293a4b5c6d7e8f90` — as
go-xkms#1441 profiles it. The device hexes the very 17 bytes it places in the
`ueid` claim, so the name and the evidence cannot disagree.

The two headers marked `TBS_PREFIX` are read from the alias certificate's
template, so the request and the certificate cannot describe the key or the
evidence differently. The subject is empty, which is why the subjectAltName is
critical (RFC 5280 §4.2.1.6). `assigner` is omitted from the
PermanentIdentifier.

### Building the request is go-xkms's

This repo does not own certificate profiles. The host knows every hole — `b`
because it chose it, and the point, the DeviceID and the firmware hash from the
alias certificate it already verified — so go-xkms reproduces the 365 bytes
itself (go-xkms#1441), checks the device's signature over SHA-256 of them under
the DeviceID point, and wraps the `CertificationRequest`:

```
CertificationRequest ::= SEQUENCE {
  certificationRequestInfo  <the 365 bytes>,
  signatureAlgorithm        ecdsa-with-SHA256 (1.2.840.10045.4.3.2), no parameters,
  signature                 BIT STRING, DER ECDSA-Sig-Value { r, s } from the 64 bytes }
}
```

A rebuild that differs by one byte from the device's fails to verify; it never
yields a CSR for something else. The device never sees a digest, a subject or
an extension from the host — only `b`, a fixed-width field inside a CBOR byte
string inside an OCTET STRING, which changes no length and no tag.
`TFWV_CSR_CRI` and `TFWV_CSR_CRI_DIGEST` in `testdata/vectors.h` pin the bytes
for the model element; the C tests pin them, and `sdk/go` carries no copy.

### Host SDK — `Device.AttestCSR`

```go
sig, err := dev.AttestCSR(ctx, b) // b is [tfw.AliasNonceLen]byte; sig is [tfw.CSRSigLen]byte, r‖s
```

Transport only. In order:

1. `CAPS` bit 15 must be set. If not, `AttestCSR` returns
   `tfw.StatusErrUnsupported` (wrapped, so `errors.Is` matches) and nothing
   else is sent.
2. `ATTEST_CSR(b)` — the 64-byte `r‖s`, returned as it came; any other length is
   a `*tfw.ShortReplyError`. A refusal comes back as the status the device gave.

The SDK does not mint the alias certificate, build the CertificationRequestInfo,
or verify the signature: a signature that does not verify is the caller's to
find, with the profile code that knows what was signed. The result carries no
private key and the SDK holds none.

## `CAPS` — 0x00, the fields this feature depends on

```
off  size  field
  9    1   CryptoTier     tfw_crypto_tier_t: 0 none, 1 software, 2 secure element, 3 tpm2 (reserved, unimplemented)
 10    4   Features       little-endian bitmap; bit 10 = TFW_FEATURE_ATTEST, bit 13 = TFW_FEATURE_ALIAS_CERT, bit 15 = TFW_FEATURE_ATTEST_CSR
 14    2   Algorithms     little-endian bitmap; bit 0 ECDSA-P256-SHA256, bit 1 Ed25519, bit 2 HMAC-SHA256; absent on firmware built before it existed
```

A host reads `CryptoTier` and `Algorithms` before it decides what any
evidence from `ATTEST`/`ATTEST_CERT` is worth — never after.
