# ADR: proving possession of the DeviceID key at ACME finalize

> **Status: ACCEPTED (2026-10-09)** · Issue: [#6](../../../../issues/6)
> · Scope: trusted-firmware (device side), go-xkms (challenge and finalize side),
> go-trusted-platform (enrolment) · Date: 2026-10-09
>
> The owner accepted option (c) on 2026-10-09; see the sign-off block at the end.
> Nothing here is implemented yet; the follow-up issues carry the work.
>
> **Amended 2026-10-09** after Codex's review of PR #32: `K` is now checked
> against the provisioning record before the challenge is valid, and carried to
> finalize as a persisted attested key. The 32U4 figures were re-measured. Option
> (c) and every owner sub-decision are unchanged; see "Amendment 1" at the end.
>
> **Amended again 2026-10-10** after Codex's second review: on the raw-key path
> go-xkms verifies the alias leaf's signature under the provisioned key; the
> identifier is the hex of the full UEID; and the CSR is assembled by go-xkms,
> with `sdk/go` as transport only. See "Amendment 2" at the end.

## The question

An AVR with an ATECC608A holds its DeviceID key in element slot 0, and that key
never leaves the part. To get a certificate for that key, go-trusted-platform
runs an ACME order against go-xkms. The order uses `device-attest-01`
(draft-acme-device-attest) with an EAB credential. The order ends at
**finalize** (RFC 8555 §7.4), which carries a PKCS#10 CSR (RFC 2986). A CSR's
signature is how its requester proves it holds the private key for the public
key it names.

So at finalize something has to produce a signature with the DeviceID key over a
CertificationRequestInfo. The firmware deliberately has no command that signs
whatever it is handed. This ADR is about how to close that gap.

## Context

### What the device signs today

Every signature the device makes is over a digest of bytes **the device
assembled itself**. The host chooses a nonce of fixed width, and nothing else:

| Command | Preimage | Host-chosen bytes | Where |
|---|---|---|---|
| `ATTEST` (0x60) | the first 73 bytes of the evidence frame: version, nonce, DeviceID, FWHash, BootCount, UptimeMs | 16-byte nonce at offset 1 | `src/caps/attest.c` `build()` |
| `ATTEST_CERT` (0x63) | a 383-byte TBSCertificate: a 232-byte template with three holes (serial, subject point, UCCS claims set) | 32-byte nonce, which becomes `eat_nonce` and sets the serial | `src/caps/alias.c` `emit_tbs()` / `mint()` |

The header comment of `src/caps/alias.c` explains why the device builds the
whole TBS: *"a signature over 32 opaque bytes is a signature over anything the
holder of those bytes says it is."* Two other places hold the same line:
`docs/security/threat-model.md` says a gateway relays device evidence and never
re-signs it, and that boundary only holds while the device decides what it
signs.

`ATTEST_CERT` is already the device side of `device-attest-01`.
`include/tfw/protocol.h` explains the 32-byte width: the host passes
`SHA-256(keyAuthorization)`, with `keyAuthorization = token "." thumbprint`,
and the device places it verbatim. The alias certificate's subject key **is**
the DeviceID point (`architecture.md`, "The DeviceID/Alias split"), and the
DeviceID key signs it. So the certificate is in effect self-signed by the
attested key over this order's key authorization. **Possession of that key at
challenge time is already proven.** Two things are missing:

- a CSR that standard finalize will accept, which is what this ADR is about;
- proof that the key is *this device's* DeviceID key. A self-signed certificate
  cannot give that; see "Who vouches for `K`" below.

### Constraints

1. **No signing oracle.** No command may let a party on the serial line obtain
   a DeviceID signature over a digest or a byte string it chose. One such
   command would let a host forge `ATTEST` evidence with any `FWHash` it liked
   (hash a 73-byte prefix and ask for a signature). It could also forge an alias
   certificate carrying any claims. That collapses every attestation the device
   makes, not just enrolment.
2. **Standard finalize.** go-xkms should accept the CSR as RFC 8555 §7.4 and RFC
   2986 describe it: a valid self-signature by the key to be certified, and the
   identifiers of the order.
3. **Flash.** Each board has a size gate (`make size-check`, `TFW_FLASH_BUDGET`
   in `boards/*/board_profile.h`):

   | Board, element tier | Budget | Today | `ATTEST_CERT` |
   |---|---:|---|---|
   | ATmega32U4 | 14 336 | 14 322: **14 bytes free** | **off**: with it on, the image is 16 930, **2 594 over** (see "The 32U4, honestly") |
   | ATmega328P | 16 384 | 16 054: **330 bytes free**, with the auth session off (`boards/atmega328p/board_profile.h`) | on |
   | ATmega2560 | 65 536 | far more headroom (`boards/atmega2560/board_profile.h`) | on |

   "Today" is the element-tier image (`make firmware BOARD=<board>
   CRYPTO=atecc608a`, which runs `size-check`), measured on 2026-10-09 at the
   commit this amendment starts from. The compiler was gcc-avr 14.2.0 from
   Debian trixie, the CI image's toolchain (`docker/Dockerfile.ci`).

4. **Ownership** (owner ruling 2026-10-09):
   - go-xkms owns the standards and profiles: DICE, EAT/UCCS, draft-acme-device-attest, RFC 9334, and the certificate and CSR profiles.
   - trusted-firmware owns the wire protocol, the firmware, `sdk/go` and its xkms adapter ([#28](../../../../issues/28)).
   - go-trusted-platform owns enrolment.

   The challenge and finalize checks of any option land in go-xkms. The device
   command and the host SDK land here. The CSR's *content* is a profile, so
   go-xkms specifies it and this repo implements it as a template, exactly as
   `alias.c` implements the alias-certificate profile. The device emits those
   bytes, so the template and its literal vector are wire and live here. Every
   host-side use of the profile (rebuilding the CRI, verifying the signature,
   assembling the DER CSR) is go-xkms's; `sdk/go` only carries the frames.

### What the ATECC608A can enforce

The element signs ECDSA P-256 only. For an ECC private-key slot, the slot and
key configuration can:

- allow or forbid **external** signing, over a message the MCU loads into
  TempKey (the firmware uses `Nonce` pass-through, then `Sign` external);
- allow or forbid **internal** signing, over a message the element composes
  from its own state after `GenKey` or `GenDig`. Microchip uses this to attest
  key generation, and its layout is fixed by the part;
- require prior authorisation by another key (`ReqAuth`/`AuthKey`), or require
  that TempKey hold element randomness (`ReqRandom`);
- make the key non-exportable (`Private`). Slot 0 already is
  (`docs/device-provisioning/api.md`).

None of these can say *"sign only digests of a structure built from this
template."* The element sees 32 bytes in TempKey and cannot know what was
hashed. Internal signing can sign only Microchip's own message format, which is
not a CertificationRequestInfo. Requiring element randomness would also forbid
the deterministic digests that `ATTEST` and `ATTEST_CERT` already depend on.
Every policy is also fixed when the config zone is locked at provisioning, so a
policy change would reach only parts provisioned afterwards.

**So an element-enforced template is not available.** The constraint must be
enforced by the firmware, the only code that calls `tfw_crypto_sign`. That is
the same basis `ATTEST` and `ATTEST_CERT` rest on today. The firmware's own
integrity rests on tfw-boot's signature check of every image, and on lock bits.

*Source note:* the slot and key policy bits are described in the public
ATECC508A datasheet. The full ATECC608A datasheet is under NDA. Before any
follow-up changes a provisioning template, check these against it. The
recommendation below changes no element policy.

### Threat model for this decision

The attacker is a compromised host or gateway on the serial line, as in
`docs/security/threat-model.md`:

- it can send any frame;
- it holds the ACME account key and the EAB credential the enrolment uses;
- it can relay, drop or replay;
- it can generate its own keys, and sign anything it likes with them.

It cannot reflash the application without a signed image, and it cannot read
the element. Physical attackers who reach the element's I2C bus directly can
make it sign anything; they are out of scope here, as they already are for
`ATTEST`.

What no option can prevent: a host that commands a device can enrol it into an
order the host started ("A host that can command a device can command it"). The
properties an option *can* provide are:

- **P1, no oracle:** the host never obtains a DeviceID signature over content it chose.
- **P2, key binding:** the issued certificate certifies the attested DeviceID key and no other.
- **P3, order binding:** the possession proof is fresh for *this* order and cannot be replayed into another.
- **P4, standard finalize:** an RFC 8555 client and server do nothing non-standard at finalize.

### Who vouches for `K`

Verifying the alias certificate's signature under the point it carries, here
called `K`, proves that whoever answered holds the private key for `K`, and that
the answer is fresh. It
does not prove that `K` is this device's DeviceID key. `ATTEST_CERT` is signed
by the key it carries, so the certificate vouches only for itself. A compromised
host can skip the device entirely:

1. generate its own P-256 key `K'`;
2. fill the alias template with `K'`, a victim's UEID in `ueid`, any
   measurement it likes, and the order's `b`;
3. sign it with `K'`, answer the challenge with it, and later finalize with a
   CSR for `K'` signed by `K'`.

Every check that compares the CSR with the alias certificate passes, because
the host produced both. The CA would issue a certificate naming the victim's
identifier and the host's key. Without a further check, P2 fails for every
option below, not only (c).

Something the host cannot write must say which key belongs to which device. That
is the provisioning record.

**What the provisioning record holds today.** `tfwprov` writes no structured
record. The operator captures its stdout, and that capture is the record
(`docs/device-provisioning/engineering.md`, "Observability"):

- `tfwprov --commit` prints the element's revision and 9-byte element serial,
  the zone lock states, and the DeviceID public key as `deviceid <hex X‖Y>`.
  That is the 64-byte point `PROV_PUBKEY` returns (`sdk/go/cmd/tfwprov/steps.go`).
- `tfwprov --bake-eeprom` is a separate run with no serial port. It writes the
  platform block, including the 16-byte `--serial`. That serial is the DeviceID
  that `ATTEST` puts in its frame (`architecture.md`, "Data flow status").
  `ATTEST_CERT` puts the RFC 9711 UEID in the `ueid` claim: the type byte
  `0x01` followed by the DeviceID, 17 bytes.

So the two halves the verifier needs, the UEID and the DeviceID key, are both
in the record. But no tool joins them, nothing signs them, and go-trusted-platform
holds no copy yet. What makes the record trustworthy against a field host is
where it is written: at the provisioning bench, under the physical-access
assumption in `docs/device-provisioning/architecture.md`, "Trust boundaries".

**The identifier is the full UEID.** Everywhere this ADR names a device
identity (the `permanent-identifier` of the order and the authorization, the
identity `ChallengeHandler` matches against the alias certificate, the CSR's
`identifierValue`, and the registry key) it is the lowercase hex of the
complete UEID, `0x01 ‖ DeviceID`: 34 hex digits, for example
`01a1b2c3d4e5f60718293a4b5c6d7e8f90`. It is never the hex of the 16-byte
DeviceID alone. This is what the firmware emits (trusted-firmware#30) and what
go-xkms's DICE profile reads from `ueid`, so the name and the evidence cannot
disagree. A registry built from the `--serial` prefixes `01`.

**The binding.** At challenge validation, *before* the challenge is marked
valid, go-xkms:

1. looks up the claimed identifier in go-trusted-platform's device registry. The
   claimed identifier is the 34-digit hex UEID that go-xkms reads from the
   alias certificate's `ueid` claim. Its `ChallengeHandler` already requires it
   to equal the authorization's `permanent-identifier`;
2. on the **raw-key path**, where the registry returns a provisioned DeviceID
   key and no certificate:
   - requires the alias certificate's SubjectPublicKeyInfo to equal the
     provisioned key, byte for byte;
   - **verifies the leaf's `Signature` over its `RawTBSCertificate` with the
     provisioned key** (ECDSA P-256 with SHA-256, the leaf's declared
     algorithm). This is the check that makes the record the chain's anchor.
     The SPKI comparison alone proves nothing: a host can put the registered
     public key in a certificate it cannot sign, beside forged measurements;
3. on the **endorsement path**, where the registry returns a DeviceID
   certificate issued at provisioning, anchors the chain `[alias, DeviceID
   certificate]` to its configured DICE roots, `TrustAnchors.DICERoots`, and
   checks that the DeviceID certificate names the same UEID. A nil or empty
   root pool is a failure, not a skipped anchor;
4. fails closed. No record, a record with neither a key nor a certificate, a
   lookup error, a mismatched key, a leaf signature that does not verify, or a
   chain that does not anchor all make the challenge `invalid` with
   `unauthorized`. Nothing defaults to valid.

Either way the anchor was written at provisioning, not by the host on the line.

**Why the raw-key path needs its own signature check.** go-xkms v0.51.0-alpha
leaves the registry route open: `dice.VerifyOptions.TrustedRoots` is documented
as "Leaving it nil skips anchoring, which is only correct when the caller has
already anchored the chain by another route". But with `TrustedRoots` nil,
`dice.Verify` skips `x509.Verify` altogether, and that is the only place the
leaf's signature is checked. `RequireDeviceSignedLeaf` checks only the leaf's
signature *algorithm*. So a leaf carrying the registered key, a victim's UEID,
forged measurements and an invalid signature would pass `dice.Verify`, then
pass the SPKI comparison. "Another route" has to include the signature check,
and the registry-anchored verifier has to make it itself, before `dice.Verify`
is trusted for the nonce, the UEID or the measurement.

**Carrying `K` to finalize.** Once validated, `K` has to reach finalize from
somewhere the host cannot write. In go-xkms v0.51.0-alpha:

- `common.ChallengeVerifierFunc` returns only an error. A verifier registered
  through it cannot hand `K` on, so this flow must not use it.
- `common.AttestingVerifier` returns an `AttestedIdentity`. `ChallengeHandler`
  persists its `Attestation` (`*entities.DeviceAttestation`) on
  `entities.ACMEChallenge.DeviceAttestation`. At finalize, `findDeviceAttestation`
  finds that record through the order's authorizations and the identity the CSR
  names. `bindDeviceAttestationKey` refuses a missing record as `badCSR`.
- The record has no attested-key field, though. For `dice`, `BindCSRKey` takes
  the key from the persisted `LeafDER`, which is the self-signed alias
  certificate. It compares the CSR with a key the leaf asserts, not with a key
  anything vouched for.

So go-xkms must add an **attested SPKI** to the persisted record, with these
properties:

- **Set only by the verifier**, after step 2 or step 3 above has passed, from
  the provisioned key or the endorsed DeviceID certificate's key. Never from
  the client's payload.
- **Authorization-scoped.** It lives on the `ACMEChallenge`, which carries
  `AuthorizationID` and `AccountID`. Finalize reads it only from a validated
  challenge of an authorization that this order references, under this order's
  account.
- **Read at finalize and compared byte for byte** with the CSR's
  `RawSubjectPublicKeyInfo`.

The failure rule: if no attested SPKI is present at finalize, the result is
`badCSR`. That covers a missing record, a record without the field, or a record
written by a verifier that predates the field. The check is never skipped.
Finalize should also query the registry again and refuse with `unauthorized`
if the entry has gone or its key has changed. That closes the window between a
decommission and an order that was already authorized.

## Options

Common to all three: go-trusted-platform creates the order with identifier
type `permanent-identifier` for the device, with EAB. go-xkms offers the
challenge. The host reaches the device through the #28 adapter and `sdk/go`.
All three need the binding in "Who vouches for `K`". None of them works
without it.

### (a) The device signs SHA-256 of a host-built CertificationRequestInfo

**Steps**

1. The challenge runs as today (`device-attest-01` with `ATTEST_CERT`).
2. The host takes the DeviceID point from the alias certificate. It builds a
   CertificationRequestInfo with that point and the order's identifiers.
3. a1: the host sends `SHA-256(CRI)` to a new command. a2: it streams the CRI
   bytes in, and the device hashes them.
4. The device signs, and returns 64 bytes. The host wraps the CSR and finalizes.
5. go-xkms checks the CSR's signature, its identifiers, and that the CSR key
   equals the attested key.

**Cost**

- a1: about 50–80 bytes of firmware, one command.
- a2: about 200–300 bytes, plus about 110 bytes of static RAM to keep a SHA-256
  context across frames. A 64-byte payload cannot carry a ~400-byte CRI in one
  frame.
- go-xkms: the finalize key-equality check only.

**Security**

- **P1 fails.** a1 is the oracle itself. a2 without checks is the oracle with
  extra steps: the "CRI" can be a forged evidence prefix or alias TBS.
- Adding checks to a2 *is* the structure-checked `SIGN_CSR` the owner rejected.
- P2 holds, given the registry binding, and P4 holds. P3 holds only if the host
  chooses to put an order value in the CRI.

**Conformance:** fully standard.

**Verdict:** not acceptable. It meets the standard by giving up the property
this firmware exists to keep.

### (b) A custom challenge type (e.g. `element-01`) that binds the CSR key to the attested key

**Steps**

1. go-xkms offers `element-01`. The device answers with attestation over the
   token: `ATTEST_CERT` with `SHA-256(keyAuthorization)`. On the 32U4 it could
   answer with the `ATTEST` frame, with nonce = the first 16 bytes of that
   digest, verified against the DeviceID key from the provisioning record.
2. At finalize the device cannot sign a CSR. The host submits one of two things:
   - b1: a CSR signed by a throwaway host key, and go-xkms **issues to the
     attested key instead of the CSR key**;
   - b2: a CSR with no valid signature.
3. go-trusted-platform's ACME client has to know to do either.

**Cost**

- **0 firmware bytes** on every element board, the 32U4 included, if go-xkms
  accepts the `ATTEST` frame there.
- A new challenge validator in go-xkms, a finalize path that substitutes the
  key, and client changes in go-trusted-platform.

**Security**

- P1 holds: no new signing surface.
- P2 holds, given the registry binding. P3 holds through the challenge token.
- Possession is proven at *challenge* time; the CSR proves nothing. In b1 it
  proves possession of the wrong key.

**Conformance**

- **P4 fails.** In b1 the issued certificate's key differs from the CSR's key,
  which every RFC 8555 client and CA assumes cannot happen. In b2 the CSR is
  not valid PKCS#10.
- `element-01` is private or unregistered, and it duplicates `device-attest-01`,
  which draft-acme-device-attest already defines for exactly this attestation.

**Verdict:** the cheapest option, and the only one that reaches the 32U4 today.
But it is non-standard at the one step this issue is about. go-xkms would carry
a challenge type and a finalize exception for a single device family, which is a
profile fork an L1 owner should not have to keep.

### (c) Hybrid: `device-attest-01` for the challenge, and a device-templated PKCS#10 at finalize — **recommended**

The CSR is built the way the alias certificate already is. The device holds the
CertificationRequestInfo as a fixed template, fills its holes from its own state
plus one 32-byte value from the host, hashes the result itself, and signs.

**Steps**

1. **Challenge.** Standard `device-attest-01`. The wire protocol is unchanged,
   and the server adds one check. The host computes
   `b = SHA-256(keyAuthorization)` and calls `ATTEST_CERT(b)`. go-xkms:
   - checks that the alias certificate's key `K` equals the DeviceID key the
     provisioning record holds for its UEID, and verifies the certificate's
     signature under that provisioned key (or anchors `[alias, DeviceID
     certificate]` in `TrustAnchors.DICERoots`), failing closed with
     `unauthorized` ("Who vouches for `K`");
   - only then trusts its UCCS claims: `nonce == b`, the UEID and the
     measurement;
   - only then marks the challenge valid, and persists `K` as the attested SPKI
     of this authorization.
2. **CSR.** The host calls a new command, `ATTEST_CSR(b)`, with the **same**
   `b`. The device:
   - fetches its own point (`tfw_crypto_pubkey`);
   - encodes the same claims set `ATTEST_CERT` carries (`tfw_uccs_encode`, nonce
     = `b`);
   - walks the template below, streaming every byte into SHA-256;
   - signs the digest with slot 0 and returns the 64-byte `r‖s`.
3. **Assembly.** `sdk/go` is transport only: `Device.AttestCSR(b)` sends `b`
   and returns the 64-byte signature, and the inputs to the CRI come from the
   calls that already exist (the alias certificate from `ATTEST_CERT(b)` carries
   the point, the UEID and the claims set). go-xkms's CSR builder, which owns
   the profile, rebuilds the same 365 CRI bytes from those inputs, checks the
   signature against `K`, and DER-wraps `CertificationRequest { CRI,
   ecdsa-with-SHA256, signature }`. The ACME client runs that builder. If the
   builder produces a different CRI, the signature does not verify. The host
   cannot alter what was signed, only fail to reproduce it.
4. **Finalize.** Standard RFC 8555 §7.4. go-xkms checks:
   - the PKCS#10 signature;
   - CSR SPKI `==` the attested SPKI persisted at step 1, byte for byte. If
     there is none, the result is `badCSR`, never a skipped check;
   - CSR UCCS `nonce == b` for this order's challenge;
   - CSR identifiers `==` order identifiers.

   It then issues to `K`.
5. **Enrolment.** go-trusted-platform records that the certified key equals the
   DeviceID key its provisioning record holds. This is a consistency check
   after issuance. The gate is the check at step 1.

**The template, in outline.** go-xkms must specify the exact profile, and the
implementation pins it with literal vectors:

```
CertificationRequestInfo ::= SEQUENCE {
  version        INTEGER 0
  subject        Name                       -- empty; identity is in the SAN and ueid
  subjectPKInfo  id-ecPublicKey, prime256v1, 04 ‖ POINT          <- hole 1: device
  attributes [0] { extensionRequest (1.2.840.113549.1.9.14) {
    subjectAltName  otherName id-on-permanentIdentifier
                    { identifierValue = hex(01 ‖ DeviceID) }     <- hole 2: device, 34 hex
    tcg-dice-UCCS-evidence (2.23.133.5.4.6)
                    { UccsEvidence { claims set, nonce = b } }   <- hole 3: device + b
  } }
}
```

Every byte is device-determined except the 32 bytes of `b`, which sit inside
the `nonce` claim at a fixed offset. The preimage cannot be confused with the
other two signed structures. The evidence frame starts `01`. The TBS starts
`30 82 01 7B A0 03` and the CRI starts `30 82 01 69 02 01 00`. A DER parser reads
each as only what it is.

**Cost (estimates; `size-check` must measure them)**

| Piece | 328P / 2560 (alias code already linked) |
|---|---:|
| CRI template constants: header, version, subject, SPKI header, attribute, SAN and UCCS headers | ~105 B (~60 B if the SPKI and UCCS headers are read from `TBS_PREFIX`) |
| Emitter: generalise `emit_tbs` into a hole-table walk that both templates share | ~60–120 B net |
| Hex encoding of the 17-byte UEID (34 digits) for `identifierValue` | ~20–30 B |
| Handler (read 32, digest, sign, write 64), `commands.def` entry, CAPS feature bit | ~60–90 B |
| **Total** | **~200–350 B**, no new static RAM; same stack as `mint()` |

- **Wire:** one new command, `ATTEST_CSR` = **0x64**, the next free id in the
  trust range. Request `[b:32]`, response `[sig:64]`. That fits one payload,
  as `AUTH_RESPONSE`'s 64-byte signature does in the other direction.
- No paging: the device does not return the CSR, because go-xkms's builder
  can reconstruct it.
- **`sdk/go`:** transport only. `Device.AttestCSR` frames the request and
  returns the 64 bytes; it holds no CRI template and builds no DER.
- **go-xkms:**
  - the CSR profile, and the builder that rebuilds the CRI, verifies the
    device's signature against `K` and assembles the DER CSR;
  - the three finalize checks;
  - a rule never to copy the requested UCCS extension into the issued
    certificate. It is evidence for the CA, not an attribute of the
    certificate.
  - the challenge-side binding from "Who vouches for `K`": a verifier that
    checks `K` against go-trusted-platform's registry, and verifies the leaf's
    signature under the provisioned key, before the challenge is valid, and an
    attested SPKI persisted for finalize. The challenge type and
    its wire format are unchanged.
- **go-trusted-platform:** the registry lookup that verifier consults.

**Security**

- **P1 holds.** The host chooses 32 bytes inside a claim of a structure that
  certifies only the device's own key, identifier and measurement. The worst a
  compromised host gets is a CSR for this device, bound to a key authorization
  of its choosing. That is the enrolment it could already start, and nothing
  it can present as anything else.
- **P2 holds, because of the registry check.** The CSR SPKI is filled from
  `tfw_crypto_pubkey`, so a genuine device cannot produce a CSR for another key.
  But that does not stop a host that never asks a device at all. P2 rests on
  go-xkms checking `K` against the provisioning record at the challenge, and
  checking the CSR against the persisted `K` at finalize.
- **P3 holds twice:** once in the challenge, and again in the CSR itself, whose
  signed `nonce` is this order's key authorization (which also covers the
  account thumbprint).
- **P4 holds.**
- The trust it adds is the trust `ATTEST_CERT` already asks for: the firmware
  is the only caller of the signer, and the firmware is verified at boot.

**Conformance**

- RFC 8555 §7.4: standard finalize, and the CSR names the order's identifiers.
- RFC 2986: valid PKCS#10, self-signed by the subject key.
- RFC 2985: `extensionRequest`. RFC 4043: `PermanentIdentifier`.
- draft-acme-device-attest: the challenge is unchanged. The draft's model is
  that the attestation speaks for a key and the certificate is issued to it,
  and the finalize key-equality check implements that. Whether the draft
  version go-xkms pins states the check normatively is for go-xkms to confirm.
- TCG DICE: r23 §6.1.4 describes `tcg-dice-UCCS-evidence` in certificates.
  Carrying it in a CSR's `extensionRequest` is an extension of that use, which
  go-xkms owns.

### Comparison

| | (a) host-built CRI | (b) custom challenge | (c) device-templated CSR |
|---|---|---|---|
| P1 no oracle | **no** | yes | yes |
| P2 key binding | yes | yes (key substitution) | yes |
| P3 order binding | only if the host adds it | challenge only | challenge **and** CSR |
| P4 standard finalize | yes | **no** | yes |
| Firmware, 328P / 2560 | 50–300 B | 0 B | ~200–350 B |
| Firmware, 32U4 | does not fit, and has no challenge side | 0 B (with the `ATTEST` frame) | does not fit (see below) |
| go-xkms | key-equality check | new challenge type and finalize exception | CSR profile and builder, three finalize checks |
| Registry binding of `K` (go-xkms and go-trusted-platform) | needed | needed | needed |

### The coordinator's earlier proposal, and what this changes

The coordinator proposed:

- a device-built, element-signed PKCS#10;
- the ACME nonce in `challengePassword`;
- standard RFC 8555 finalize;
- a server-side check that the CSR key equals the attested key.

That is option (c) in substance. This ADR keeps it and makes three refinements,
each an open question below:

1. **The binding goes in the UCCS `nonce` claim inside `extensionRequest`, not
   in `challengePassword`.**
   - `challengePassword` is a PKCS#9 `DirectoryString`, so 32 binary bytes
     would need text encoding on the device.
   - Its defined meaning is a revocation password, and SCEP repurposes it as a
     shared secret.
   - The UCCS claim is the encoding go-xkms already parses in the alias
     certificate, with the same value and the same meaning. One fact, one
     encoding, one parser.
2. **The binding is named: `SHA-256(keyAuthorization)`, the same 32 bytes as
   the challenge.** "The ACME nonce" could otherwise be read as the RFC 8555
   `Replay-Nonce`, which is a JWS anti-replay token the device has no use for.
3. **The device returns only the signature.** go-xkms's CSR builder, run by the
   ACME client, reconstructs the CRI. This saves the paging and DER-wrapping
   code a full on-device CSR would need.

### Options already rejected by the owner

These are recorded briefly so they are not proposed again. The owner's own
reasons govern; these are the reasons as they bear on this design.

- **A structure-checked `SIGN_CSR` command.** The firmware would have to parse
  host-supplied DER and decide whether it is "a CSR". Every subject, extension
  and identifier that grammar admits becomes signable, and a parser bug becomes
  an oracle. A DER parser is also the class of code `alias.c` chose a template
  to avoid.
- **A separate enrolment key.** It is not the attested key. Certifying it would
  need a second chain to show it lives on the same device. It would double the
  key lifecycle in a config zone that is locked forever. The DICE evidence
  would then speak for a key other than the one in the certificate.
- **A gateway-held key.** The certificate would prove possession by the
  gateway, not by the device. That is the re-signing the containment boundary
  forbids.

Not recommended either: **gating `ATTEST_CSR` behind the operator auth
session.** It brings back a gateway-held credential (the operator key) by
another name, and the auth session itself does not fit on the 328P.

## Recommendation

**Adopt option (c).**

- The challenge stays standard `device-attest-01`, answered by the existing
  `ATTEST_CERT`.
- Finalize uses a standard PKCS#10 CSR whose CertificationRequestInfo the device
  assembles from a fixed template. The host supplies only the 32-byte
  `SHA-256(keyAuthorization)` it already supplies to the challenge.
- go-xkms checks that the CSR key is the attested key and that the CSR's
  `nonce` is this order's.
- At the challenge, go-xkms checks that the attested key is the DeviceID key
  in go-trusted-platform's registry and that the alias certificate's signature
  verifies under it (or that the chain anchors in its DICE roots), failing
  closed. It persists that key for
  finalize.

It is the only option that meets P1 and P4 together. It extends a pattern the
firmware already uses, so it adds no new kind of trust. An element-enforced
constraint would be better than a firmware-enforced one, but the ATECC608A
cannot express it.

### The 32U4, honestly

The 32U4 cannot afford this, and option (c)'s CSR is not the reason. The 32U4
already cannot answer `device-attest-01`, because `ATTEST_CERT` is compiled out
of it. Both images below were built from the same commit, the one this
amendment starts from. Only the board profile's `TFW_FEATURE_ALIAS_CERT` was
flipped, in a scratch copy. Each was built with `make firmware
BOARD=atmega32u4 CRYPTO=atecc608a` on the CI toolchain:

| 32U4 element image | Flash | Against the 14 336 budget |
|---|---:|---|
| alias certificate off (as shipped) | 14 322 | 14 bytes free |
| alias certificate on | 16 930 | **2 594 over**; `size-check` fails |

On this baseline the alias certificate costs **2 608 B**. The CSR command would
add about 200–350 B, more without the alias emitter to share, and the board has
14 bytes free. Even the CSR alone would have no challenge to pair with.
Enrolling a 32U4 through this flow needs about **2.8–2.96 KB** of code, about
**2.8–2.95 KB over budget**.

The figures the accepted ADR quoted (14 306 today, 2 540 for the certificate,
"370 over") came from different builds and should not be combined. The comment
in `boards/atmega32u4/board_profile.h` still carries the old 14 706 / 370-over
figures. Its claim that dropping the measurement claim (388 B) would fit no
longer holds: that would still be about 2.2 KB over. trusted-firmware#30
replaces those figures. The conclusion is the same, and the gap is larger. The
ways to close it:

1. **Exclude the 32U4 from ACME enrolment (recommended for now).** CAPS already
   reports the alias-certificate bit clear, so go-trusted-platform can refuse
   cleanly. The 32U4 keeps `ATTEST` for runtime freshness.
2. **Drop capabilities from the 32U4 element build.**
   - Measured on the 328P (`docs/boards/attiny85.md`): 1-Wire ~942 B, I2C and
     SPI commands ~770 B, PWM ~298 B. Tone is already off.
   - All of them together are about 2 010 B, still short of the 2.8–2.95 KB
     needed.
   - Dropping them also breaks this firmware's "every Arduino capability is on
     the wire" promise on that board.
3. **Spend the 32U4's unspent half.** The budget is a project gate at half the
   28 672 usable bytes, not the silicon's limit. Raising it is a policy ruling
   against the reserve the budget keeps on purpose, which the board's own
   comment argues against.
4. **Use option (b) on the 32U4 only:** 0 bytes, with the `ATTEST` frame as the
   challenge evidence. This buys the 32U4 at the price of the non-standard
   finalize this ADR rejects, kept in go-xkms for one board.

## Consequences

- The device gains a third signed structure. `docs/security/threat-model.md`
  and `docs/dice-attestation/architecture.md` must list it beside the evidence
  frame and the alias TBS, together with the rule that only fixed templates
  with fixed-width host fields are ever signed.
- The CSR template exists twice: in C, because the device emits it, and in
  go-xkms, which owns the profile and assembles the CSR. `sdk/go` holds no copy.
  The literal CRI vector in `testdata/vectors.h` is a wire vector and stays
  here; go-xkms pins its builder to the same bytes (go-xkms#1441 att1 already
  reproduces the 365-byte CRI). A mismatch between the two is a wire break,
  resolved by the two owners, never by regenerating either vector. The Go
  mirror in `sdk/go` pins only the `ATTEST_CSR` frame.
- Enrolment through ACME needs an element tier with `ATTEST_CERT`: the 328P and
  the 2560. The 32U4 is excluded unless the owner picks another path above.
- go-xkms owns one more profile (the possession CSR), the builder that
  assembles it, and three finalize checks. It also owns a registry-anchored
  DICE verifier that checks the leaf's signature itself and an attested-SPKI field on
  the persisted challenge record. The challenge type and its wire format do not
  change.
- A device can be enrolled only after go-trusted-platform holds its
  provisioning record. Devices provisioned before then must be backfilled from
  the captured `tfwprov` output. Until that happens they fail closed.
- Decommissioning, which deletes the registry entry, stops new enrolments at
  the challenge and in-flight ones at finalize.

## Follow-up issues (1–5 filed)

1. **go-xkms#1441:** the possession-CSR profile (CRI layout, identifier type and
   encoding, where the binding sits, criticality of the requested UCCS
   extension) and the finalize checks: PKCS#10 signature, SPKI == attested
   point, `nonce` == `SHA-256(keyAuthorization)`, identifiers == the order's.
   The requested UCCS extension is never copied into the issued certificate.
   Added by Amendment 1:
   - **A registry-anchored DICE verifier.** go-xkms declares the lookup
     interface it consults, for example a resolver from the 34-digit hex UEID
     to the provisioned DeviceID SPKI or a DeviceID certificate. go-trusted-platform
     implements it and injects it, so the dependency points from Layer 5 to
     Layer 3, never back.
   - That verifier runs inside the device-attest-01 `common.AttestingVerifier`,
     never a `common.ChallengeVerifierFunc`. It runs before the
     `AttestedIdentity` is returned, which means before the challenge is valid.
   - It requires the alias leaf's SPKI to equal the provisioned key and the
     leaf's signature to verify under it, or the chain `[alias, DeviceID
     certificate]` to anchor in `TrustAnchors.DICERoots` with a matching UEID
     (see Amendment 2's requirements below). It then runs `dice.Verify` for the
     nonce, the UEID and the measurement.
   - No record, a lookup error or a mismatch makes the challenge invalid with
     `unauthorized`.
   - **An attested SPKI on `entities.DeviceAttestation`.** Only that verifier
     sets it, after the registry check, from the provisioned key, never from
     the payload. It is persisted on the `ACMEChallenge`, so it is scoped to
     the authorization and the account.
   - `bindDeviceAttestationKey` compares the CSR's `RawSubjectPublicKeyInfo`
     with it byte for byte. If no attested SPKI is present, the result is
     `badCSR`, never a skipped check.
   - Finalize queries the registry again and refuses with `unauthorized` if the
     entry has gone or its key has changed.

   Added by Amendment 2:
   - **Raw-key path: verify the leaf signature.** When the resolver returns a
     provisioned DeviceID key and no DeviceID certificate, the verifier
     requires the alias leaf's `RawSubjectPublicKeyInfo` to equal the
     provisioned SPKI byte for byte, **and** verifies the leaf's `Signature`
     over its `RawTBSCertificate` with the provisioned key, under the leaf's
     declared algorithm (ECDSA with SHA-256). Neither check alone is enough. The
     verifier makes this check itself: with `TrustedRoots` nil, `dice.Verify`
     skips `x509.Verify`, and `RequireDeviceSignedLeaf` checks only the
     algorithm. A signature that does not verify, or a key the verifier cannot
     parse, makes the challenge invalid with `unauthorized`. No claim of the
     leaf is trusted before the check passes.
   - **Endorsement path: anchor to `TrustAnchors.DICERoots`.** When the
     resolver returns a DeviceID certificate, `dice.Verify` runs over `[alias,
     DeviceID certificate]` with `TrustedRoots` set from
     `TrustAnchors.DICERoots`, and the DeviceID certificate must name the same
     UEID. A nil or empty root pool fails closed; it never falls back to the
     raw-key path or to no anchor.
   - **CSR assembly.** go-xkms provides the builder that takes the alias
     certificate from `ATTEST_CERT(b)` (the point, the UEID and the claims
     set), `b` and the device's 64-byte `r‖s` from `ATTEST_CSR(b)`. It rebuilds
     the 365-byte CRI byte for byte, verifies the signature against `K`, and
     DER-wraps the `CertificationRequest`. It is pinned to trusted-firmware's
     literal CRI vector in `testdata/vectors.h`. `sdk/go` holds no copy of the
     template.
2. **trusted-firmware#30:** firmware `ATTEST_CSR` (0x64): the template, a
   shared emitter with `alias.c`, a CAPS bit, literal vectors, `size-check`
   figures, docs and the threat-model update. It also records the 32U4 ruling
   in `boards/atmega32u4/board_profile.h` and the board docs. That includes
   replacing that file's stale 14 706 / 370-over figures with the measurements
   in "The 32U4, honestly". On the 328P, with 330 bytes free, its `size-check`
   decides resolved question 5.
3. **trusted-firmware#31:** `sdk/go` `Device.AttestCSR`, **transport only**
   (Amendment 2): it sends `b` and returns the 64-byte signature. The point, the
   UEID and the claims set come from the existing calls. It has no CRI
   template, builds no DER and verifies no signature; those are go-xkms#1441's.
   Also the Go mirror of the `ATTEST_CSR` frame vector, `tfwsim` coverage and
   `tfwctl` exposure.
4. **trusted-firmware#28:** the xkms adapter, phase C: serve the finalize-time
   CSR interface by calling `Device.AttestCSR` with the same `b` as the
   challenge, and handing the signature and the alias certificate to
   go-xkms's CSR builder.
5. **go-trusted-platform#772:** end-to-end enrolment: challenge, CSR with the
   same `b`, finalize; refuse devices whose CAPS lacks the bit; record that the
   certified key equals the provisioned DeviceID key.
   Added by Amendment 1:
   - **A device registry entry** mapping the UEID (`01` followed by the 16-byte
     platform serial, as 34 lowercase hex digits) to the provisioned DeviceID
     public key, as a DER SPKI of the
     `tfwprov` point. Optionally it also holds the element serial and the
     provisioning template's hash.
   - The entry is written from the provisioning record, which comes from the
     bench and outside any field host. It is never created or changed from the
     enrolment path.
   - **The lookup**, implementing go-xkms#1441's resolver interface: from a
     claimed UEID to the provisioned key, or to a DeviceID certificate once
     one is issued. It is read-only, and it says "no record" distinctly so
     that go-xkms can fail closed.
   - Decommissioning deletes the entry. Devices provisioned before the
     registry existed are backfilled from captured `tfwprov` output.
6. **To be filed (trusted-firmware), not yet filed:** `tfwprov` emits a
   structured provisioning record that joins the `--bake-eeprom` serial (the
   DeviceID; the UEID is `01` followed by it) to the `--commit` DeviceID point. go-trusted-platform could then ingest
   it instead of hand-copied stdout.

## Resolved questions

1. **Option (c).** Accepted by the owner, 2026-10-09.
2. **The 32U4.** Excluded from ACME enrolment; it keeps `ATTEST` for runtime
   freshness.
3. **Where the binding sits.** The UCCS `nonce` in `extensionRequest`, not
   `challengePassword`.
4. **CSR output.** The device returns only the signature; the CRI is
   reconstructed on the host by go-xkms's CSR builder, not by `sdk/go`
   (Amendment 2).
5. **The 328P.** Excluded if `size-check` shows it over budget.
6. **The identifier.** `permanent-identifier` whose value is the lowercase hex
   of the full UEID, `0x01 ‖ DeviceID`, 34 digits, as go-xkms#1441 profiles it
   and the firmware emits (Amendment 2).

## Sign-off

```
Decision: accepted
Option chosen: (c) device-templated PKCS#10 at finalize, device-attest-01 challenge unchanged
32U4 path: excluded from ACME enrolment (keeps ATTEST for runtime freshness)
Signed off by: owner (Jeremy Hahn)   Date: 2026-10-09
Notes:
- Binding: the UCCS nonce in extensionRequest, not challengePassword.
- The device returns only the signature; the host reconstructs the CRI.
- The 328P is excluded if size-check shows it over budget.
- Identifier: permanent-identifier; go-xkms has the final word (go-xkms#1441).
```

## Amendment 1 (2026-10-09): Codex's review of PR #32

Option (c) and every owner sub-decision above stand unchanged:

- the 32U4 is excluded;
- the binding is the UCCS `nonce` in `extensionRequest`;
- the device returns only the signature;
- the identifier is `permanent-identifier`.

The amendment closes two gaps in how the accepted flow authenticates the key,
and corrects one set of figures.

1. **[P1] `K` is bound to the provisioning record before the challenge is
   valid.** A self-signed alias certificate does not authenticate its own key.
   A host could mint one for a key it generated, with a victim's UEID. go-xkms
   now checks `K` against go-trusted-platform's registry entry for the claimed
   UEID at challenge validation, and fails closed. go-trusted-platform's
   check after issuance stays, as a consistency check rather than the gate.
   See "Who vouches for `K`".
2. **[P1] `K` is persisted for finalize.** The verifier sets an attested SPKI
   on the persisted, authorization-scoped `DeviceAttestation` record, and
   finalize compares the CSR with it. If it is absent, the result is `badCSR`.
   Codex's premise is half right for go-xkms v0.51.0-alpha:
   - right: `ChallengeVerifierFunc` returns only an error;
   - wrong: `ACMEChallenge` does carry a verifier result, `DeviceAttestation`,
     through `AttestingVerifier`.

   That record has no attested key, though. Its `dice` binding reads the key
   from the self-signed leaf, so the requirement stands.
3. **[P2] 32U4 re-measured from one baseline.** The figures are 14 322 without
   the alias certificate and 16 930 with it, so the certificate costs 2 608 B
   and the image would be 2 594 over budget. The exclusion stands, with a
   larger gap than the accepted text stated. The 328P was measured at 16 054,
   which leaves 330 bytes free.

## Amendment 2 (2026-10-10): Codex's second review of PR #32

Option (c), every owner sub-decision, and Amendment 1's shape stand unchanged.
go-xkms has agreed that shape: `AttestedSPKI` on `entities.DeviceAttestation`
set through `AttestingVerifier`, a resolver that returns a provisioned key or a
DeviceID endorsement, and a registry re-query at finalize. This amendment
tightens the first, corrects the identifier, and moves the host side of the CSR
to its owner.

1. **[P1] The raw-key path verifies the leaf's signature.** Matching the alias
   leaf's SPKI against the provisioned key is not enough on its own. In go-xkms
   v0.51.0-alpha, `dice.Verify` skips `x509.Verify` when `TrustedRoots` is nil,
   and `RequireDeviceSignedLeaf` checks only the algorithm. So a leaf that
   carries the registered key and forged measurements, with an invalid
   signature, would pass. The go-xkms verifier now verifies the leaf's
   `Signature` over `RawTBSCertificate` with the provisioned key and fails
   closed. On the endorsement path the chain must anchor in
   `TrustAnchors.DICERoots`, with no fallback. Both are explicit go-xkms#1441
   requirements. See "Who vouches for `K`".
2. **[P1] The identifier is the full UEID.** The `permanent-identifier`, and
   the identity used for order, challenge, CSR and registry matching, is the
   lowercase hex of the complete RFC 9711 UEID, `0x01 ‖ DeviceID`: 34 hex
   digits, not the 32-digit hex of the DeviceID. That is what the firmware
   emits (trusted-firmware#30) and what go-xkms's DICE profile reads from
   `ueid`. The CRI is accordingly 365 bytes and starts `30 82 01 69`.
3. **[P2] The CSR profile's host-side mirror lives in go-xkms.** This repo owns
   the wire protocol; go-xkms owns DICE ASN.1 and certificate profiles.
   `sdk/go` `AttestCSR` is transport only: it sends `b` and returns the
   64-byte signature, and the claims and alias data come from the existing
   calls. Rebuilding the CRI, verifying the signature and assembling the DER
   CSR are go-xkms's, which already reproduces the 365-byte CRI byte for byte
   (go-xkms#1441 att1). The C vector stays here as a wire vector, because the
   device emits it. trusted-firmware#31 becomes transport only, and
   go-xkms#1441 gains the CSR assembly.
