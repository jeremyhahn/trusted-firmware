# AVR Device Backend — Engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Feature: `xkms-adapter` · Moved from go-xkms `v0.51.0-alpha` (trusted-firmware#28) · Last verified: 2026-10-20
> Conflicts with this document are defects. Fix them here first.

Module: `github.com/jeremyhahn/trusted-firmware/xkms`, in `xkms/`, tagged
`xkms/v0.2.0-alpha`. Package: `xkms/avr` · Import path
`github.com/jeremyhahn/trusted-firmware/xkms/avr`. The root package `xkms`
registers the backend with go-xkms and re-exports the `avr` surface (see below).

The module sits above both of its dependencies. It imports go-xkms (Layer 3),
pinned in `xkms/go.mod` to `github.com/jeremyhahn/go-xkms v0.57.0-alpha`, and
`github.com/jeremyhahn/trusted-firmware/sdk/go` (Layer 0), required at
`v0.4.0-alpha`. Neither is replaced; the file's one `replace` pins the
third-party `github.com/armon/go-metrics`. `sdk/go` is a separate module on purpose:
go-xkms and go-trusted-platform import it, so it cannot depend on this one.
`make sdk-verify` keeps it free of in-house dependencies.

Release order: tag `sdk/go`, bump the `sdk/go` requirement in `xkms/go.mod` to
that tag, then tag `xkms/`. The module never builds against the working-tree
`sdk/go`; a wire change reaches the adapter only through a new `sdk/go` tag.
Raising the go-xkms requirement is likewise an edit to `xkms/go.mod`.

## Exported surface

| Symbol | What it is |
|---|---|
| `NewBackend(ctx, *Config, *tfw.Device) (*Backend, error)` | Reads `CAPS` and caches it. Fails when the device is absent, silent, or speaks a protocol major this host does not. |
| `Backend.Type() types.BackendType` | `"tfw"` (`avr.BackendType`), the type `xkms/register.go` registers through go-xkms's `RegisterExternalBackend`. Its keys' store type is `tfw` too, so their Key IDs read `tfw:...`. |
| `NewKeyProvider(map[string]interface{}) (types.KeyProvider, error)` | The factory the registry calls. Reads `ConfigKeyDevice` (required `*tfw.Device`) and `ConfigKeyConfig` (optional `*Config`) from the map; a wrong type is `ErrInvalidFactoryConfig`. |
| `Backend.PossessionSigner(*Attestation) (crypto.Signer, error)` | The signer that proves possession of the attested key during finalize. It asks the device for `Device.AttestCSR` and returns only a signature that verifies over the digest it was asked to sign, under the attested key; anything else is `ErrPossessionMismatch`. |
| `Backend.Capabilities() types.Capabilities` | Answers from the cached `CAPS`. No I/O, no allocation. `Attestation` is true only when `CAPS` sets `FeatureAttest`, `FeatureAliasCert` and the ECDSA P-256 bit on a tier with a key. |
| `Backend.Caps() tfw.Caps` | The device's `CAPS` reply verbatim. |
| `Backend.AttestLevel() (types.AttestLevel, error)` | Derived from `Caps.CryptoTier` and nothing else. |
| `Backend.Algorithms() Algorithms` | Derived from `Caps.Algorithms` and nothing else. |
| `Backend.AttestKey(ctx, attrs, nonce) (interface{}, error)` | `*Attestation`. The nonce is 32 bytes from `deviceattest01.DICENonce`. Gates on the cached `CAPS` before any frame, agreeing with `Capabilities().Attestation`: no `FeatureAttest` is `ErrCommandUnavailable`; `FeatureAttest` without `FeatureAliasCert` and the P-256 bit is `ErrUnsupportedByDevice`. A full `CAPS` goes to the wire, and the device's refusal maps as for any command. |
| `Backend.Identity(ctx) (*Identity, error)` | `PROV_INFO`: element serial, die revision, both zone lock states. Provisioning images, serial only. |
| `Backend.Health(ctx) (*Health, error)` | `SYS_INFO`: uptime, free RAM, reset cause. Not a clock. Serial only: the Ethernet allow-list refuses `SYS_INFO` (`ErrCommandUnavailable`). |
| `Backend.GetKey(ctx, attrs) (crypto.PrivateKey, error)` | `*DeviceKey` carrying the element's public point, read with `PROV_PUBKEY`: provisioning images, serial only. A fielded device's key is the alias certificate's (`AttestKey`). |
| `Backend.ListKeys(ctx)` | The one key the device holds, or none. No I/O. |
| `DeviceKey` | `Public()`, `Slot()`, `CN()`, `Equal()`. Deliberately not a `crypto.Signer`. |
| `Algorithms` | `Can`, `Named`, `Unnamed`, `Empty`, `Set`, `String`. |
| `Attestation` | Format, parsed leaf, DER, `dice.ClaimsSet`, UEID, nonce, attest level, crypto tier, `Anchored`. |
| `AttestationConfig` | `TrustStore *truststore.Config`, `Intermediates [][]byte`. |
| `Identity` | `Serial`, `SerialBytes`, `Revision`, `ConfigLocked`, `DataLocked`, `Provisioned()`. |
| `FormatDICE` | `"dice"`, the same string `device-attest-01` uses for this evidence. |
| `Config`, `DefaultConfig()`, `Config.Validate()` | `KeyCN`, `Slot`, `RequestTimeout`, `Attestation`, `Logger`. |

The root package re-exports these from `xkms/backends_avr.go` under an `AVR`
prefix — `AVRBackend`, `AVRConfig`, `NewAVRBackend`, `DefaultAVRConfig`,
`ErrAVR<Name>` — and `xkms/register.go` adds `AVRBackendType`,
`AVRConfigKeyDevice`, `AVRConfigKeyConfig` and `RegisterAVRBackend`. They are
not in `sdk/go`: that module carries no AVR backend symbols, because it must not
depend on go-xkms. The device itself comes from `trusted-firmware/sdk/go`, which
a consumer imports directly.

## Registration

Importing package `xkms` (blank is enough) runs an `init` that calls
`RegisterAVRBackend`, which registers `avr.BackendType` (`"tfw"`) and
`avr.NewKeyProvider` with go-xkms's `RegisterExternalBackend`. A refused
registration panics: another module already holds the name, and a process that
started without the backend would fail later, on a device, far from the cause. A
second call to `RegisterAVRBackend` returns an error satisfying
`errors.Is(err, types.ErrBackendTypeRegistered)`.

The registry type is `tfw`, not `avr`: go-xkms keeps `avr` for its own built-in
backend until go-xkms#1439 step 3, so a Key ID written `avr:...` does not resolve
to this backend.

`GenerateKey`, `Signer`, `Decrypter`, `DeleteKey` and `RotateKey` exist to
satisfy `types.KeyProvider` and always refuse. Each refusal names a permanent
property of the part or the protocol.

## Invariants

- **The backend holds no private key** and needs no locked memory. Nothing here
  reaches `go-enclave`, and if that ever changes the layering changes with it.
- **Attestation level comes from the crypto tier.** Not from the attestation
  feature bit, not from the algorithms on offer, not from whether a chain
  happened to verify.
- **Algorithms come from the capability bitmap.** The bitmap is not the
  algorithm identifiers that travel in an attestation frame: `HMAC-SHA256` is
  bit `0x0004` and identifier `0x10`. They are separate types upstream for
  exactly this reason.
- **A tier or an algorithm bit this host cannot name never negotiates up.**
  Unknown tier → `AttestLevelUnattested` plus `ErrUnknownCryptoTier`; unknown
  algorithm bit → reported by `Unnamed`, `Can` returns false.
- **`CAPS` is read once.** `Capabilities` returns no error, so a backend that
  re-read it would have to invent an answer — about the strength of a root of
  trust — whenever the device did not reply.
- **Nothing here may reach `PROV_GENKEY`.** It replaces the identity every
  other answer is about. `GetKey` uses `PROV_PUBKEY`, which recomputes the
  public point and generates nothing.
- **A refusal costs no round trip.** Every unsupported operation and every
  argument error answers before a frame is built.
- **This package never hashes a key authorization.** The challenge arrives
  already hashed, from `deviceattest01.DICENonce`. One place hashes; everything
  else compares.
- **The attestation nonce check is unconditional.** Anchoring is configurable;
  freshness is not. `Anchored` false means no roots were configured, never that
  anchoring ran and did not hold.
- **The two nonce widths never mix.** 32 bytes is enrolment (`eat_nonce` in the
  alias certificate); 16 is runtime freshness (the evidence frame). `AttestKey`
  refuses 16 before a frame is built, because a device handed the shorter one
  cannot tell it was truncated.
- **One device, one conversation.** Every command is issued under the backend's
  `wire` mutex, and `AttestKey` holds it across the whole of `AttestCert` —
  several `Do` calls that must stay contiguous, because the device regenerates
  its certificate pages for each nonce and keeps no copy. `tfw.Device` is
  a strict request/response client with no locking of its own (its
  documentation says so), and `types.KeyProvider` requires implementations to
  be thread-safe: two goroutines in `Do` at once consume each other's replies.
- **`Config.RequestTimeout` reaches the device.** `NewBackend` applies it to the
  attached device with `tfw.WithTimeout` as well as to each call's
  context. `Device.Do` computes its read deadline from the device's own timeout
  and notices a context only between reads, so without this a configured 30s
  still gave up at the SDK's 10s default on a blocking read. The device is the
  caller's, and this does change it: a backend that issues every command on that
  link owns the deadline those commands are given.
- **The alias certificate is ECDSA-SHA256 signed.** `dice.Verify` is called with
  `RequireDeviceSignedLeaf`, because the leaf's issuer is the DeviceID key in
  the same element and its certificate is not in the chain — so the generic
  device-residency rule has no certificate to check the algorithm on.
- **Element lock polarity is not re-inverted.** The element writes `0x55` for
  UNLOCKED; `ElementInfo` already decodes that, and `Identity` copies the fields
  through unchanged.

## Errors

All sentinels, all prefixed `avr backend:`. Grouped by what a caller should do:

- **Fix the call** — `ErrNilConfig`, `ErrNilDevice`, `ErrMissingKeyCN`,
  `ErrInvalidSlot`, `ErrInvalidKeyAttributes`, `ErrUnknownKey`,
  `ErrInvalidNonce`, `ErrTrustStoreInit`, `ErrBackendClosed`,
  `ErrInvalidFactoryConfig`, `ErrInvalidAttestation`.
- **Refuse the evidence** — `ErrMalformedCertificate`, plus the `dice` package's
  own errors returned unwrapped so there is one vocabulary and one place each
  check lives: `ErrUnsupportedKeyType`, `ErrUnsupportedSignatureAlgorithm`,
  `ErrNonceMismatch`, `ErrChainVerificationFailed`, `ErrUccsEvidenceNotFound`,
  `ErrNonceClaimMissing`, `ErrCriticalEvidenceUnreadable`.
- **Stop asking** — `ErrSigningUnsupported`, `ErrDecryptionUnsupported`,
  `ErrGenerationUnsupported`, `ErrDeletionUnsupported`,
  `ErrRotationUnsupported`.
- **Refuse the device** — `ErrProtocolVersion`, `ErrUnknownCryptoTier`,
  `ErrNoDeviceKey`, `ErrInvalidDevicePoint`, `ErrPossessionMismatch`.
- **Retry, reconnect or read the firmware** — `ErrDeviceUnreachable`,
  `ErrDeviceProtocol`, `ErrDeviceRefused`, `ErrCommandUnavailable`,
  `ErrNotProvisioned`, `ErrDeviceCrypto`, `ErrAuthRequired`, `ErrDeviceBusy`,
  `ErrProtectedRegion`, `ErrDeviceInternal`, `ErrUnsupportedByDevice`,
  `ErrElementUnreachable`, `ErrInvalidArgument`.

`ErrUnsupportedByDevice` and `ErrCommandUnavailable` are deliberately separate.
A software-tier device built with the HMAC signer (the ATmega328P and
ATmega32U4) asked to mint an alias certificate answers the first: it knows what
a certificate is and has no public half to certify. The ATmega2560's soft root
has one, a P-256 DeviceID (#48, #130), and mints the certificate. The second says
the feature was compiled out. One is a part to change and the other is a build
to change.

Device failures are returned as `errs.Joined`, so both sides stay reachable:
`errors.Is` against a sentinel above says what to do, and `errors.As` against
the SDK's own type — `*tfw.CRCError`, `*tfw.ShortReplyError`,
`*tfw.TimeoutError` — reaches the values that say why. `io.EOF` stays
reachable underneath a link failure, which is how a closed port is told from a
stalled one.

A status code this build has no mapping for becomes `ErrDeviceRefused`, never
the nearest known one: statuses extend and never renumber, so an unrecognised
one means this host is older than the firmware.

Context cancellation passes through unchanged, so `errors.Is(err,
context.Canceled)` still works across this layer.

## Configuration

| Field | Default | Notes |
|---|---|---|
| `KeyCN` | `"avr-device-id"` | Refused when empty rather than defaulted: an empty name would match every `KeyAttributes` that forgot to set one. |
| `Slot` | `tfw.SlotDeviceID` (0) | Any other value is refused. Slot 1 is the UDS; reading it as a public key would hand out a derivation secret's slot as an identity. |
| `RequestTimeout` | 30s | Generous on purpose — a device deriving a key in software spends seconds inside one command on a 16 MHz part. Applied to the attached device as well as to each call's context, so it is the deadline the read actually gets. |
| `Attestation` | nil | `TrustStore` and `Intermediates`. Nil is the unanchored profile: every check but anchoring still runs. |
| `Logger` | stderr text handler | |

## Tests

`make xkms-test` and `make xkms-coverage` (floor `XKMS_COVERAGE_FLOOR`, 90%).
Both depend on `tfwsim` and `tfwsim-atecc608a` and run with `-tags codec_json`
from `xkms/`. They are not part of `make ci`: the module depends on the private
go-xkms module, so they run in the `xkms` CI job, which is part of the CI Gate.
Run `make ci xkms-test xkms-coverage` where go-xkms is reachable.

`xkms/e2e` drives a `device-attest-01` DICE enrolment against the real firmware
compiled for the host (`tfwsim`, `tfwsim-atecc608a`). `xkms/registration` tests
the registry hook.

`avr/fieldlink_test.go` builds the backend through the registry's factory over
the Ethernet link of the same firmware (`tfwsim.StartElementEthernet`), with its
serial link open beside it. It pins what the field link carries: `CAPS`,
`AttestKey` and `PossessionSigner` work; `Identity`, `Health` and `GetKey` answer
`ErrCommandUnavailable` and leave the connection in step; re-attesting makes the
earlier attestation's possession signer answer `ErrInvalidArgument`, because the
device keeps its latest certificate's nonce only; and `Close` leaves the
connection usable for the next backend.

There is no hardware and CI has none, so the tests drive a **fake link and the
real codec**: every byte a test sends is framed by the SDK's own
`EncodeResponse` and every byte the backend sends is parsed by the SDK's own
`Decode`. What is under test is this package against the protocol as
trusted-firmware implements it. Mocking `*tfw.Device` would replace the one
thing worth exercising — a reply's layout, a status code's meaning, a command's
length — with this package's own opinion of it.

A command with no entry in the fake link's table is answered
`StatusErrUnknownCmd`, which is what firmware built without that feature really
does: the absence of a reply models the absence of a handler.

The link's read returns `io.EOF` when its buffer is exhausted, so a negative
test fails in microseconds instead of spinning to the SDK's read deadline.

`concurrency_test.go` holds the two tests about the link rather than the
protocol. One drives `Health`, `Identity`, `GetKey` and `AttestKey` from many
goroutines at once and requires every caller to get its own reply — without the
`wire` mutex the SDK's shared receive buffer panics, and `-race` reports the
unsynchronised sequence counter directly. The other attaches a link that blocks
until the read deadline, as a serial port does, and measures the wait: a
100ms `RequestTimeout` that never reached the device waits 10 seconds.

## Test vectors

`vectors_test.go` mirrors `trusted-firmware`'s `testdata/uccs_vectors.h`: the
471-byte alias certificate a real device emits, frozen as a literal, with a real
ECDSA signature over its own TBS and a real point on P-256.
`TestAttestKeyVerifiesTheCertificateARealDeviceEmits` drives it through the
whole path.

**A failure there is a wire break, not a test to update.** Those are the bytes
every already-provisioned device puts on the wire. It is mirrored rather than
imported because the SDK does not export it — the vector lives beside the
firmware's contract file, not in the Go module — and that second copy is
acceptable only because neither copy is generated: both are frozen literals of
one fact, so drift is a failure rather than two implementations agreeing with
each other about something wrong.

## Resolved upstream gaps

Both blockers reported against `v0.2.0-alpha` are fixed in `v0.3.0-alpha`:
`ProvInfo` now receives the two lock bytes explicitly (a 38-byte reply; a
36-byte one from older firmware is refused as short rather than read past), and
`Attest`/`AttestCert` exist with internal paging and exported nonce widths.

The disagreement over `dice.Verify` requiring a `DiceTcbInfo` the alias profile
does not emit was resolved in go-xkms's `dice` rather than worked around here:
§6.1.4 permits UCCS "in place of" `DiceTcbInfo`. `checkAliasCertificate` now
makes the one `dice.Verify` call, with `RequireDeviceSignedLeaf`. See
`architecture.md`.
