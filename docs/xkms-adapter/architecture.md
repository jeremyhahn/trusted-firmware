# AVR Device Backend — Architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Feature: `xkms-adapter` · Last verified: 2026-10-15
> Conflicts with this document are defects. Fix them here first.

## The problem

A classic AVR part with a secure element on its I2C bus has a real identity: a
P-256 keypair generated inside the element on a provisioning jig, whose private
half has no path out. That identity is worth the same things every other
hardware identity in this library is worth — an enrolment record, an attestation
level a policy can act on, a device a CA can issue to.

What it is not is a general key store. The part has 2 KB of RAM, no clock, and a
wire protocol with 64-byte payloads. Presenting it as an ordinary backend that
"just happens to be slow" would have callers ask it for an RSA key, a decryption
or a signature over a digest they chose, and get a failure at the point where
they had already committed to it.

## Shape chosen: a read-only key provider that reports from the device's CAPS reply

`xkms/avr` (module `github.com/jeremyhahn/trusted-firmware/xkms`, tagged
`xkms/v0.2.0-alpha`) implements `types.KeyProvider` and nothing more. Every answer
it gives about the device comes from the device's own `CAPS` reply, read once at
construction and cached; every operation the part cannot perform refuses with a
sentinel that says which permanent property of the part or the protocol refuses
it.

```
caller ──opens link──► io.ReadWriter
                          │
             tfw.Device (trusted-firmware/sdk/go)   ◄── owns the wire
                          │
                   avr.Backend  ──► types.KeyProvider
```

## Where it lives, and how go-xkms finds it

The owner ruled on 2026-10-09 that go-xkms defines the plug-in interfaces and
implements standards, and that a firmware we wrote ships its xkms integration as
an adapter module in its own repository. So this backend lives in
`trusted-firmware`, in the `xkms/` module, and sits above both of its
dependencies: it imports go-xkms (Layer 3, pinned in `xkms/go.mod`) and
`sdk/go` (Layer 0). Nothing imports it except through go-xkms's registry. It is a
module of its own, not part of `sdk/go`, because go-xkms and go-trusted-platform
import `sdk/go` and it must stay free of in-house dependencies.

Package `xkms` (`xkms/register.go`) registers the backend in an `init`, through
go-xkms's `RegisterExternalBackend`, under the type `tfw` with `avr.NewKeyProvider`
as the factory. A program that blank-imports the package gets `tfw` as a backend
type that go-xkms's `AutoInitialize` constructs from
`AutoConfig.BackendConfigs["tfw"]`, whose entries are the attached `*tfw.Device`
and an optional `*avr.Config`. The type is `tfw` because go-xkms still holds
`avr` for its built-in backend.

## The link

The backend does not own the link. A device is one physical thing with several
concerns on it, and the code that opened the port is the code that should close
it; `Close` marks the backend closed and leaves the port alone.

It does own the **conversation** over that link, and has to. `tfw.Device`
is a strict request/response client with no locking of its own — one sequence
number, one receive buffer, one event list — while `types.KeyProvider` requires
a thread-safe implementation, so the backend serialises every command on a
mutex and holds it across the whole of a multi-frame exchange. For the same
reason it applies `Config.RequestTimeout` to the device rather than only to each
call's context: `Do` derives its read deadline from the device's own timeout and
sees a context only between reads, so a deadline that never reached the device
would not be the deadline the read got.

## The boundary with trusted-firmware

`trusted-firmware` owns the AVR wire protocol, the firmware that speaks it and
the host SDK for it (`sdk/go`). This module sits in the same repository and still
owns none of those: it reaches the wire only through `sdk/go`, at a tagged
version. Every byte that reaches
the wire is put there by an SDK call — there is no frame built here, no command
id declared here, no reply layout parsed here.

That boundary is what decides what this feature contains. Where the SDK has no
call, the capability is absent and says so, rather than being reconstructed from
the firmware's headers. A second copy of a wire format is a copy that drifts,
and the half that drifts is always the one nobody is testing against real
firmware.

Where this backend is constructed and called during enrolment, and what
go-quicbaas and go-trusted-platform owe it, is
[`../device-onboarding/architecture.md`](../device-onboarding/architecture.md).

## Why `Capabilities.Keys` is false

`Keys` reads as "generates and stores keys". This backend does neither: the
identity is created on a provisioning jig, inside an element whose zones are
then locked irreversibly, by firmware that is replaced before the part ships.

It is also load-bearing elsewhere. go-xkms's `pkg/xkms/algorithms.go`'s
`BuildAlgorithmsResponse` advertises ECDSA at four curve sizes, RSA at three key
sizes and Ed25519 for **every** backend whose `Keys` is true. For this device
that would claim eight algorithms it cannot perform, one of which — Ed25519 —
the ATECC608A has no hardware for at all. A host that negotiated a peer down
onto it would fail at the moment it mattered most.

Callers that want to know what the device can perform call `Backend.Algorithms`,
which answers from the device's own capability bitmap.

## Attestation

`AttestKey` asks the device to mint an alias certificate for a challenge —
`ATTEST_CERT`, which the SDK pages internally — and checks what came back.

```
verifier nonce = SHA-256(ACME key authorization)       32 bytes
        │        deviceattest01.DICENonce
        ▼
   device mints and signs an alias certificate, carrying the nonce in
   the eat_nonce claim of tcg-dice-UCCS-evidence
        │
        ▼
   parse → dice.Verify (P-256 device-signed leaf, UCCS extension,
   constant-time nonce compare, anchor when roots are configured)
```

The nonce check is **unconditional**. The device has no clock, so that nonce is
the only thing separating a live answer from a replayed recording, and a
mismatch refuses. Anchoring is conditional, because roots belong to whoever
configured them and a device on a bench has none; `Attestation.Anchored` reports
which happened and never conflates "no roots configured" with "anchored".

This host never hashes the key authorization. `deviceattest01.DICENonce` is the
one place that hashing happens, so a device, a provisioning tool and a verifier
cannot each hash something slightly different — a trailing newline, the token
rather than the whole authorization — and produce a mismatch whose cause is
invisible from either end. A caller that passes a raw key authorization is
refused for its width.

### How `AttestKey` verifies

`checkAliasCertificate` parses the certificate and makes one `dice.Verify` call
with the issued nonce, the configured roots and `RequireDeviceSignedLeaf`. The
decoded `dice.ClaimsSet` and UEID it returns are handed to the caller in
`Attestation`, the UEID and the firmware measurement being CWT claims 256 and 273
inside the `tcg-dice-UCCS-evidence` extension. Nothing about the DICE formats is
reimplemented here.

`RequireDeviceSignedLeaf` is needed because the alias certificate is signed by
the DeviceID key in the same element and that key's certificate is not in the
chain, so the generic device-residency rule has no certificate to check the
signature algorithm on. Without it an alias signed with ECDSA-SHA384 would pass a
backend documented to accept the one algorithm the element emits.

This was not always one call. `dice.Verify` once demanded a `DiceTcbInfo`
extension that the AVR alias profile does not emit (it carries only the critical
`tcg-dice-UCCS-evidence`, because the certificate is assembled on an 8-bit part
from a 230-byte PROGMEM template). That was the verifier being wrong: §6.1.4
permits UCCS "in place of" `DiceTcbInfo`. It was fixed in go-xkms's `dice`, and
the open-coded sequence here was removed.

### Possession and finalize

Finalize needs a signature by the attested key over a request the device builds.
The protocol has no command that signs a host-supplied digest, so
`Backend.PossessionSigner(att)` returns a `crypto.Signer` that asks the device for
`Device.AttestCSR` with the attestation's nonce and returns the signature only if
it verifies, under the attested key, over the SHA-256 digest it was asked to sign.
Any other digest — another document, another nonce, a request for another
identity — is refused with `ErrPossessionMismatch`. A device without the
`FeatureAttestCSR` bit refuses with the device's unsupported status.

## Rejected alternatives

**A `crypto.Signer` whose `Sign` always fails.** It would put the discovery that
this device cannot sign at the end of a protocol exchange the caller had already
committed to. `GetKey` returns a `*DeviceKey` that fails the `crypto.Signer`
assertion instead, which puts the discovery where a negotiation can still act on
it.

**Returning an unverified certificate when no roots are configured.** The
certificate is returned, but only after every check that does not need a root
has passed — the challenge above all. What is optional is anchoring, not
freshness. A `nil` trust store never means "skip the nonce".

**Inferring hardware backing from the attestation feature bit.** The bit says a
command is compiled in. The crypto tier says what the root of trust is. Only the
second is an attestation claim, and a device can report the first with the
second set to software.

**Treating an unrecognised crypto tier as at least as strong as the last one
this host knows.** Tiers extend and never renumber, so a larger number carries
no promise. An unknown tier reports `AttestLevelUnattested` and an error.

**Refusing to attach to a device whose tier or protocol minor is newer than this
host.** That would take a fleet down on a host upgrade alone. A newer minor is
tolerated; an unknown tier attaches and degrades. Only a protocol **major**
mismatch refuses, because past that the frames themselves may not parse.
