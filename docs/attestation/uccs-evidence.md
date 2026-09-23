# UCCS evidence and the alias certificate

> **Status: NORMATIVE** · Scope: `ATTEST_CERT`, the claims set and the
> certificate that carries it · Last verified: 2026-09-23

A verifier's nonce travels in **`tcg-dice-UCCS-evidence`**, the extension TCG
DICE Attestation Architecture r23 §6.1.4 defines for exactly this, rather than
in a repurposed `DiceTcbInfo.vendorInfo`. `vendorInfo` is an OCTET STRING whose
meaning is the vendor's, so a nonce there is a private convention every verifier
must be told about. A nonce here is a registered EAT claim in a registered
extension, and a verifier that has never heard of this product can read it.

```
tcg-dice-UCCS-evidence OBJECT IDENTIFIER ::= {tcg-dice 6}   -- 2.23.133.5.4.6
UccsEvidence ::= SEQUENCE { uccs OCTET STRING }
```

The extension carries no signature of its own — §6.1.4: *"the certificate
signature authenticates, and integrity protects UCCS contents"*. So there has to
be a certificate, and the DeviceID key in the element has to sign it. That is
the alias certificate.

## The claims set

152 bytes of CBOR, a fixed shape with three values in it. Every key was read out
of the registry rather than assumed.

| Claim | Key | Value | Source |
|---|---|---|---|
| `nonce` | 10 | `bstr`, **32 bytes** | RFC 9711 §10.2 (`bstr` 8–64); the enrolment challenge — see below |
| `ueid` | 256 | `bstr`, 17 bytes | RFC 9711 §10.2 (`bstr` 7–33); §4.2.1.1 requires a leading **type byte**, `0x01` RAND for a stored ≥128-bit identifier |
| `measurements` | 273 | `[[258, <CoSWID>]]` | RFC 9711 §10.2 and §4.2.16; content named by CoAP Content-Format id, 258 = `application/swid+cbor` (RFC 9393 §6.4) |

### Deterministic encoding is a requirement, not tidiness

The output is core-deterministic CBOR (RFC 8949 §4.2.1). §6.1.4 makes the issuer
responsible for the extension's contribution to the CDI, so one claims set must
have exactly one encoding — otherwise the verifier re-encodes what it parsed,
gets different bytes, and the contribution no longer means what the spec says.
The certificate signature would still verify; the CDI claim would not hold.

Three rules produce it, and all three are structural rather than remembered:
definite lengths only (`head()` has no indefinite form), the smallest head that
fits the argument (`head()` picks it), and map keys in ascending order — written
that way throughout, including inside the CoSWID. go-xkms encodes the same way
on the verifying side.

### Why the nonce is 32 bytes and the evidence frame's is 16

These are two different nonces and they must not be merged.

**`AG_ATTEST_NONCE_LEN` (16)** is runtime freshness. It rides in the 164-byte
evidence frame and answers *is this device reporting now*. Sixteen bytes is a
wire contract with every deployed verifier.

**`AG_ALIAS_NONCE_LEN` (32)** is enrolment. It rides in `eat_nonce` and answers
*is this key on this device*. ACME `device-attest-01` compares the attestation
nonce against the key authorization — `"<token>.<RFC 7638 thumbprint>"`, which
is 87 bytes, because the thumbprint alone is 43 base64url characters. RFC 9711
bounds `eat_nonce` at 8–64, so **the raw key authorization cannot be carried by
a conforming claims set at all.** The profile therefore carries
`SHA-256(keyAuthorization)`, which is 32 bytes and is what Apple's
`device-attest-01` profile does.

The host does that hashing. **The device never sees a key authorization and
never hashes one** — it receives 32 bytes in the command and places them
verbatim in the claim. Merging the two widths would either truncate an enrolment
challenge to 16 bytes or widen a frame every deployed host parses at a fixed
offset, so the dispatcher refuses each command the other's width with
`AG_ERR_BAD_ARGC`.

### Why the firmware hash costs a CoSWID

**EAT has no scalar "firmware hash" claim.** `measurements` is the claim for
it, and RFC 9711 §4.2.16 requires an *evidence* CoSWID — not a payload CoSWID —
when the CoSWID format is used. So `FWHash` travels inside the smallest tag
RFC 9393 permits:

```
{0: h'5eec…b4',                     tag-id, a product constant
 1: "ag-fw",                        software-name
 2: {31: "ag", 33: 1},              entity, role tag-creator
 3: {17: {7: [1, h'<sha-256>'],     evidence → file → hash-entry
          24: "app"}},                              fs-name
 12: 0}                             tag-version
```

`hash-alg-id` 1 is `sha-256` in the IANA Named Information Hash Algorithm
registry, which RFC 9393 §2.9.1 points at. The four identity fields say nothing
interesting; they are there because a tag without them is not a CoSWID.

The alternative was a private claim key, which would have been the same private
convention this extension was chosen to avoid.

## The certificate

470–472 bytes. The spread is the DER encoding of the ECDSA signature: its two
INTEGERs are 32 or 33 bytes each depending on leading zeros and the high bit.

Most of it is constant for a given product, so the `TBSCertificate` is a
**230-byte PROGMEM template** with three holes — serial number, subject public
key, and the claims set, which is the tail and so is appended rather than
patched. A general DER writer would be a length stack, a two-pass size
computation and a class of bug where the lengths and the contents disagree; on a
part with 1 446 bytes of budget left that is not a close call.

| Field | Value |
|---|---|
| version | v3 |
| serialNumber | 8 bytes from the nonce, leading byte forced into `0x40`–`0x7F` so it is positive and minimally encoded at a fixed width |
| signature | `ecdsa-with-SHA256` |
| issuer | `CN=ag DeviceID` — the DeviceID certificate is issued off-device at provisioning, so the device knows its issuer's **name** and not its key |
| validity | `20200101000000Z` to `99991231235959Z`, RFC 5280 §4.1.2.5's "no well-defined expiration". **These dates carry no information**: no part here has a clock |
| subject | `CN=ag Alias` — the per-device identity is the `ueid` claim, not a name |
| SPKI | `id-ecPublicKey` / `prime256v1`, the element's DeviceID point |
| extensions | `tcg-dice-UCCS-evidence`, **critical** |

**Critical on purpose.** This is not a general-purpose identity certificate. A
relying party that cannot read the evidence has no business accepting the key it
certifies, and a non-critical extension is one a parser may silently drop.

### The device builds the whole TBS

A gateway could assemble a `TBSCertificate` and hand the device a digest to
sign. It would be less code. It would also mean the device cannot know what it
attested to — a signature over 32 opaque bytes is a signature over whatever the
holder of those bytes says it is. The threat model's containment boundary is
"the gateway relays device-signed evidence verbatim and never re-signs it", and
that boundary is only real if the device determines the content. So the TBS is
built here, byte for byte, and the only thing that crosses the bus into the
element is a digest of bytes this firmware produced.

## Where this deviates from the spec, and why

§6.1.4: *"the issuer MUST ensure that non-constant elements of this field
contributed to the CDI that generated the subject key"*.

The `ueid` and the firmware measurement do. They are DICE inputs, fixed before
the CDI exists.

**The nonce does not, and on this class of part it cannot.** The CDI is computed
at boot; the challenge arrives afterwards. Satisfying the letter needs a subject
key derived per challenge, and an ATECC608A will not do that: there is no
command that turns an arbitrary 32-byte secret into a P-256 private key inside
the part, and a key derived outside it is a key that left it — which is the one
thing this tier exists to prevent. `src/crypto/atecc608a/provider.c` records the
same constraint for the DeviceID key.

What binds the nonce instead is the certificate signature over a TBS that
contains it, which is the protection §6.1.4 itself names for UCCS contents. The
freshness property a verifier needs holds; the key-derivation property the
sentence asks for does not. Stated here rather than buried.

## Delivery

A certificate is seven times a frame payload and longer than a byte can count,
so it is paged like the attestation evidence — and for the same reason:
enlarging every frame buffer charges the cost to every command on every board to
serve the one command that needs it.

```
request   ATTEST_CERT(nonce[32])   mint, and return page 0
          ATTEST_CERT(page[1])     return a later page of the same document
response  [page:1][total:2 little-endian][bytes:1..60]
```

The total is **two bytes** where the evidence frame's is one. A host reads it
from page 0 and asks for `ceil(total / 60)` pages; a page at or past the end is
`AG_ERR_RANGE` rather than an empty success, because an empty success reads as
"the document ended here".

**Pages are regenerated, not cached.** The device holds the nonce, the public
key and the signature — 161 bytes — and emits the certificate afresh for each
page. It is never held whole, because a 328P has 239 bytes of RAM budget left
and the document is 471. Consequently the pages are consistent only until the
next nonce arrives, which is the contract `ATTEST` already has.

## Where it is compiled in

| Board | `soft` | `atecc608a` |
|---|---|---|
| ATmega328P | absent | present — 14 938 / 16 384 |
| ATmega2560 | absent | present — 15 354 / 65 536 |
| ATmega32U4 | absent | **absent** — see below |

**Absent on the software tier** because a certificate names a subject *public*
key and that tier's identity is an HMAC. There is nothing to certify. The
command is still compiled into the host test build so the refusal is pinned:
`AG_ERR_UNSUPPORTED`, which says the firmware knows what a certificate is and
this root of trust cannot make one — distinct from `AG_ERR_UNKNOWN_CMD`, which
says it has never heard of them.

**Absent on the ATmega32U4** because it does not fit. The builder is 2 540
bytes; an element-backed image measures 14 706 against a 14 336 budget, and that
budget is half of 28 672 usable bytes rather than half of 32 768 because 4 096
bytes of that part are a DFU bootloader. `ATTEST` still works there and its
evidence still carries the measurement and the nonce. See the note beside
`AG_FEATURE_ALIAS_CERT` in `boards/atmega32u4/board_profile.h`.

## The vectors

`testdata/uccs_vectors.h`. Literals, frozen after an independent encoder
produced them and tools that did not write them accepted them: the claims set
decodes under a real CBOR decoder with no trailing bytes, and the certificate is
accepted by OpenSSL and by Python `cryptography` with a signature that verifies
over the parser's own `tbs_certificate_bytes`.

Two malformed vectors go with them. `BAD_OUTER` moves the TBS length by one and
every X.509 parser rejects it. `BAD_INNER` moves one length byte *inside* the
extension value — and **OpenSSL accepts it, as does `cryptography`**, because
neither descends into an extension it does not recognise. That is the failure
this project has to fear: a certificate that parses somewhere, is relayed by a
gateway that checked it, and means nothing. `test/unit/test_alias.c` walks the
extension's own DER, which is what finds it.
