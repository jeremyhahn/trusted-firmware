# AVR Device Backend — Engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Feature: `xkms-adapter` · Moved from go-xkms `v0.51.0-alpha` (trusted-firmware#28) · Last verified: 2026-10-20
> Conflicts with this document are defects. Fix them here first.

Module: `github.com/jeremyhahn/trusted-firmware/xkms`, in `xkms/`, tagged
`xkms/v0.2.0-alpha`. Package: `xkms/avr` · Import path
`github.com/jeremyhahn/trusted-firmware/xkms/avr`. The root package `xkms`
registers the backend with go-xkms and re-exports the `avr` surface (see below).

The module sits above both of its dependencies. It imports go-xkms (Layer 3),
pinned in `xkms/go.mod` to `github.com/jeremyhahn/go-xkms v0.64.2-alpha`, and
`github.com/jeremyhahn/trusted-firmware/sdk/go` (Layer 0), required at
`v0.4.2-alpha`. Neither is replaced; the file's one `replace` pins the
third-party `github.com/armon/go-metrics`. `sdk/go` is a separate module on purpose:
go-xkms and go-trusted-platform import it, so it cannot depend on this one.
`make sdk-verify` keeps it free of in-house dependencies.

Release order: tag `sdk/go`, bump the `sdk/go` requirement in `xkms/go.mod` to
that tag, then tag `xkms/`. The module never builds against the working-tree
`sdk/go`; a wire change reaches the adapter only through a new `sdk/go` tag.
Raising the go-xkms requirement is likewise an edit to `xkms/go.mod`.

## Requirements

Audited 2026-11-16 (#456) against SPEC row `xkms-adapter` (S) and this
document (E). Code paths are under `xkms/`; a test is `file: TestName`, paths
relative to `xkms/`. A make target stands in where the requirement is about
what CI runs.

| # | Requirement | Status | Code | Test |
|---|---|---|---|---|
| 1 | S: registered through go-xkms's `RegisterExternalBackend` as type `tfw`; importing `xkms` is enough | done | `register.go` (`init`, `RegisterAVRBackend`) | `registration/registration_test.go: TestBlankImportMakesTFWABackendType` |
| 2 | E: a refused registration panics; a second `RegisterAVRBackend` is `types.ErrBackendTypeRegistered` | done | `register.go` | `register_internal_test.go: TestARefusedRegistrationPanics` |
| 3 | E: the registry's factory builds the backend from `ConfigKeyDevice` / `ConfigKeyConfig`; a wrong type is `ErrInvalidFactoryConfig` | done | `avr/factory.go` (`NewKeyProvider`) | `avr/factory_test.go: TestNewKeyProviderRefusesTheWrongTypes`; `registration/registration_test.go: TestAutoInitializeConstructsTheAVRBackend` |
| 4 | S: consumed by go-quicbaas's composition as one of the hardware crypto implementations it passes to go-xkms | partial | `avr/factory.go` (this side); the composition is go-quicbaas's | `registration/registration_test.go: TestAutoInitializeConstructsTheAVRBackend` (this side only) |
| 5 | S: go-trusted-platform and xkey never import this module | not started | none here: a property of those modules | none in this repository |
| 6 | S: a read-only key provider that answers from `CAPS`, read once | done | `avr/backend.go` (`NewBackend`, `Capabilities`) | `avr/backend_test.go: TestNewBackendReadsCapsOnce`; `avr/backend_test.go: TestCapabilitiesDoNotAllocate` |
| 7 | S: refuses what the part cannot do, before any frame | done | `avr/backend.go` (`GenerateKey`, `Signer`, `Decrypter`, `DeleteKey`, `RotateKey`) | `avr/backend_test.go: TestUnsupportedOperationsRefuseWithoutTouchingTheWire`; `avr/avr_mutate_test.go: TestEveryMutatedConfigFieldIsRefusedBeforeTheWire` |
| 8 | E: `AttestKey` gates on the cached `CAPS`, agreeing with `Capabilities().Attestation` | done | `avr/attestation.go` (`AttestKey`), `avr/backend.go` (`canAttest`) | `avr/attestation_test.go: TestAttestKeyRefusesWhereCAPSSaysTheDeviceCannotAttest`; `avr/backend_test.go: TestCapabilitiesClaimAttestationOnlyWhereCAPSSaysTheDeviceCan` |
| 9 | S: holds no private key; `DeviceKey` is not a `crypto.Signer` | done | `avr/key.go` | `avr/key_test.go: TestDeviceKeyIsNotASigner` |
| 10 | S: reaches the wire only through `sdk/go` | done | `avr/` calls `tfw.Device` only | `avr/boundary_test.go: TestPackageReachesTheWireOnlyThroughSDK` |
| 11 | S: finalizes through `Device.AttestCSR`; the possession signer returns only a signature over the asked digest under the attested key | done | `avr/possession.go` (`PossessionSigner`) | `avr/possession_test.go: TestPossessionSignerSignsTheDevicesRequest`; `avr/possession_test.go: TestPossessionSignerRefusesAnotherKeysSignature` |
| 12 | S: the device-attest-01 tests are unit tests in `xkms/avr` replaying recorded tfwsim frames | done | `avr/attestation.go`, `avr/possession.go` | `avr/device_attest_test.go: TestDeviceAttestAcceptsTheRecordedEnrolment` |
| 13 | S: `xkms-coverage` is unit-only; `xkms-test` deselects `xkms/e2e` | done | `../mk/go.mk` (`xkms-test`, `xkms-coverage`) | `make xkms-coverage` (floor 90%) |
| 14 | S: the soft root enrols through device-attest-01 on tfwsim in CI | done | `avr/attestation.go`, `avr/possession.go` | `e2e/soft_enroll_test.go: TestSoftRootEnrollsThroughDeviceAttest01` (`make xkms-e2e-soft`) |
| 15 | S: `TestHardwareEnrollment` runs on a real board through `make hw-enroll` (#7) | partial | `e2e/hw_enroll_test.go`, `../mk/go.mk` (`hw-enroll`) | `e2e/hw_enroll_test.go: TestHardwareEnrollment` exists; no board run is recorded |
| 16 | S: the enrollment and downstream migration work (#20, #19) | not started | not named in this repository | none; see slice 6 |
| 17 | E: attestation level comes from the crypto tier only | done | `avr/trust.go` (`trustFor`), `avr/backend.go` (`AttestLevel`) | `avr/backend_test.go: TestAttestLevelComesFromTheDeviceTier`; `avr/backend_test.go: TestAttestLevelIgnoresTheAttestationFeatureBit` |
| 18 | E: algorithms come from the capability bitmap, not wire identifiers | done | `avr/algorithms.go` | `avr/algorithms_test.go: TestAlgorithmsUsesCapabilityBitsAndNotWireIdentifiers` |
| 19 | E: an unknown tier or algorithm bit never negotiates up | done | `avr/trust.go`, `avr/algorithms.go` | `avr/trust_test.go: TestTrustForRefusesToGuessAtAnUnknownTier`; `avr/algorithms_test.go: TestAlgorithmsReportsBitsItCannotName` |
| 20 | E: nothing reaches `PROV_GENKEY`; `GetKey` reads `PROV_PUBKEY` | done | `avr/backend.go` (`GetKey`) | `avr/backend_test.go: TestGetKeyReadsThePublicPointFromTheElement` |
| 21 | E: the key authorization is never hashed here | done | `avr/attestation.go` | `avr/attestation_test.go: TestAttestKeyTakesTheNonceItIsGivenAndDoesNotHashIt` |
| 22 | E: the nonce check is unconditional | done | `avr/attestation.go` (`checkAliasCertificate`) | `avr/attestation_test.go: TestAttestKeyRefusesAReplayedCertificate` |
| 23 | E: the 16-byte runtime nonce is refused before a frame | done | `avr/attestation.go` | `avr/attestation_test.go: TestAttestKeyRefusesTheEvidenceFrameNonceWidth` |
| 24 | E: one device, one conversation (`wire` mutex) | done | `avr/backend.go` (`Backend.wire`) | `avr/concurrency_test.go: TestConcurrentCallsDoNotConsumeEachOthersReplies` |
| 25 | E: `Config.RequestTimeout` reaches the device | done | `avr/backend.go` (`NewBackend`) | `avr/concurrency_test.go: TestRequestTimeoutBoundsTheDeviceRead` |
| 26 | E: the alias certificate is ECDSA-SHA256 signed, verified with `RequireDeviceSignedLeaf` | done | `avr/attestation.go` (`checkAliasCertificate`) | `avr/attestation_test.go: TestAttestKeyRefusesAnAliasThatIsNotECDSASHA256Signed`; `avr/attestation_test.go: TestDiceVerifyAcceptsTheProfileTheDeviceEmits` |
| 27 | E: element lock polarity is not re-inverted | done | `avr/device.go` (`newIdentity`) | `avr/backend_test.go: TestIdentityDoesNotInvertTheLockPolarity` |
| 28 | E: an unmapped status is `ErrDeviceRefused`; SDK errors and context errors stay reachable | done | `avr/device.go` (`deviceError`, `classify`), `avr/errors.go` | `avr/device_test.go: TestDeviceErrorDegradesAnUnknownStatusToARefusal`; `avr/device_test.go: TestDeviceErrorKeepsTheSDKErrorReachable`; `avr/device_test.go: TestDeviceErrorPassesContextFailuresThrough` |
| 29 | E: the field (Ethernet) link carries `CAPS`, `AttestKey` and `PossessionSigner` and refuses the bench calls in step | done | `avr/backend.go`, `avr/attestation.go`, `avr/possession.go` | `avr/fieldlink_test.go: TestFieldLinkAttestsAndProvesPossessionOverEthernet`; `avr/fieldlink_test.go: TestFieldLinkRefusesBenchCallsAndStaysInStep` |
| 30 | E: the frozen real-device alias certificate verifies end to end | done | `avr/attestation.go` | `avr/attestation_test.go: TestAttestKeyVerifiesTheCertificateARealDeviceEmits` |
| 31 | E: `Config` defaults, and `Slot` other than the DeviceID refused | done | `avr/config.go` | `avr/config_test.go: TestDefaultConfigIsValid`; `avr/avr_mutate_test.go: TestEveryMutatedConfigFieldIsRefusedBeforeTheWire` |

### Remaining slices

1. **Owner decision (SPEC text).** Row `xkms-adapter` in `docs/SPEC.md` names
   `TestDeviceAttestDICE*`, which matches no test (the tests are
   `TestDeviceAttest*` in `xkms/avr/device_attest_test.go`), and pins go-xkms
   `v0.55.0-alpha` where `xkms/go.mod` requires `v0.64.2-alpha`; correcting
   both touches `docs/SPEC.md` only and is proved by `git grep` finding the
   names it states.
2. The comment on `enrollOnce` in `xkms/e2e/hw_enroll_test.go` points at
   `TestDeviceAttestDICEEnrollsTheRealFirmware`, which does not exist; renaming
   it to `TestSoftRootEnrollsThroughDeviceAttest01` touches that file only and
   is proved by `make xkms-e2e-soft` staying green. **Done:** the comment
   names `TestSoftRootEnrollsThroughDeviceAttest01`.
3. **Hardware (#7).** Run `make hw-enroll DEVICE=<port> DEVICEID=<hex>` on a
   provisioned board and record the run in `docs/xkms-adapter/user.md`, proved
   by `xkms/e2e/hw_enroll_test.go: TestHardwareEnrollment` passing on the board.
4. Pin "reaches the wire only through `sdk/go`" with a unit test in a new
   `xkms/avr/boundary_test.go` that parses the package's non-test files and
   fails on an import that opens a link of its own (`net`, a serial package),
   proved by `make xkms-test`. **Done:**
   `TestPackageReachesTheWireOnlyThroughSDK` (with
   `TestOpensALinkOfItsOwnNamesTheNetworkAndSerialPorts`) fails naming the file
   and import of any `net`, `net/*` or `*serial*` import.
5. **Another project.** go-quicbaas's composition passes this backend to
   go-xkms, and go-trusted-platform and xkey never import `xkms/`: each is a
   test or import check in that project (requirements 4 and 5), with nothing to
   change here.
6. **Owner decision.** The SPEC row's "enrollment and downstream migration
   work (#20, #19)" names no file in this repository, and #19 elsewhere in
   these docs is the `SPI_XFER` hang (`docs/wire-protocol/engineering.md`); the
   owner says whether that work is open and where it lives before a lane can
   be seated on it.

Slices 2 and 4 are done. Slices 1 and 6 wait on the owner, slice 3 on a
provisioned board (#7), and slice 5 on the other projects; none is a lane's
work in this repository until its owner, board or project acts. With slices
1 to 6 closed the row has nothing left in this repository; the SPEC status
change is the owner's.

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
A software-tier device built with the HMAC signer (the ATmega32U4; the
ATmega328P has no software tier since owner ruling 2026-10-17 (m522)) asked to mint an alias certificate answers the first: it knows what
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
489-byte alias certificate a real device emits, frozen as a literal, with a real
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
then did not emit was resolved in go-xkms's `dice` rather than worked around
here: §6.1.4 permits UCCS "in place of" `DiceTcbInfo`. Since #213 the alias
certificate carries a non-critical `DiceTcbInfo` (`flags` only) before the
critical `tcg-dice-UCCS-evidence`. `checkAliasCertificate` now
makes the one `dice.Verify` call, with `RequireDeviceSignedLeaf`. See
`architecture.md`.

## Requirements: device-security-event-evidence

Audited 2026-11-16 (#505) against SPEC row `device-security-event-evidence`
(`docs/SPEC.md` line 55, status planned; ROADMAP: #283, go-xkms #1765). The
row is owner ruling 2026-10-31, item 181 (#283), option (b), which carries
forward item 173 (CA m1656, ruling 251 item 4): "option (b) xkms evidence
through the tf xkms adapter, the new command id is allowed"
(`docs/hsm-self-defence/m7-design.md`, O9). Paths are from the repository
root; a test is `file: name`.

| # | Requirement | Status | Code | Test |
|---|---|---|---|---|
| 1 | The device keeps a security-event record (#50, A6): what the engine saw, decided and did | partial: the record runs over a RAM store that survives no reset; the durable store is row `hsm-self-defence`'s slice 11b-3 (`docs/hsm-self-defence/adr-a6-durable-store.md`) | `src/core/hsm_record.c`, `src/core/hsm_store.h`, `src/core/hsm_store_ram.c` | `test/unit/test_hsm_record.c: test_each_kind_reads_back_in_order`; `test/unit/test_hsm_record.c: test_a_full_record_keeps_the_first_and_counts_the_rest` |
| 2 | The new command id of item 173 reads the record over the wire | done: `HSM_RECORD_READ` (`0x7C`), `TFW_FEATURE_HSM` images only | `include/tfw/protocol.h` (`TFW_CMD_HSM_RECORD_READ`), `src/core/hsm_cmd.c` (`tfw_hsm_cmd_record_read`), `sdk/go/hsm.go` (`HSMRecordRead`, `HSMRecordReadAll`) | `test/unit/test_hsm_cmd.c: test_one_entry_reads_back_with_every_field`; `test/unit/test_hsm_cmd.c: test_an_empty_record_reads_range`; `sdk/go/hsm_test.go: TestHSMRecordReadAllStopsAtTheEndOfTheRecord`; `sdk/go/hsm_record_tfwsim_test.go: TestSimHSMRecordKeepsTheSignalTheDecisionAndTheActions` |
| 3 | Device-signed EAT evidence over the Verifier nonce | done (what the digest would ride in): the UCCS claims set with `eat_nonce` in the DeviceID-signed alias certificate and `ATTEST_CSR` | `src/caps/uccs.c` (`tfw_uccs_encode`), `src/caps/alias.c`, `src/caps/csr.c`; host check `xkms/avr/attestation.go` (`checkAliasCertificate`) | `test/unit/test_uccs.c: test_the_claims_set_is_the_bytes_the_vector_pins`; `test/unit/test_uccs.c: test_the_claim_keys_are_the_registered_ones`; `xkms/avr/attestation_test.go: TestAttestKeyRefusesAReplayedCertificate` |
| 4 | The record is bound as a Detached Submodule Digest inside that evidence | not started: the claims set is a fixed 151-byte template (`TFW_UCCS_LEN`, `src/caps/uccs.h`) with no submodule claim | none | none; see slice 2 |
| 5 | The xkms adapter's route for the new command id carries the digest | not started: nothing in `xkms/` calls `HSMRecordRead` | none (would be `xkms/avr/`) | none; see slice 3 |
| 6 | The record itself travels beside the envelope | not started on the adapter: the SDK reads it (requirement 2), the adapter does not return it | none (would be `xkms/avr/`) | none; see slice 3 |
| 7 | The evidence format and its verification are go-xkms's (`device-security-event-evidence`, go-xkms #1765); this project neither restates nor designs them | holds: no format is defined here | none: a property of what this repository leaves out | none in this repository |

### Remaining slices

1. **Another project, then owner decision.** go-xkms #1765 fixes the format:
   which claim carries the Detached Submodule Digest (RFC 9711 `submods`),
   its hash algorithm, and which bytes of the record it digests (the 10-byte
   `HSM_RECORD_READ` replies in index order, or the store's own image). Two
   choices are this project's and the owner's once that lands: whether the
   digest rides in the alias certificate's claims set (built at boot, so it
   would bind the record as it stood then) or in a per-request evidence
   (`ATTEST_CSR` over the Verifier nonce, so it binds the record at the
   request); and whether the 151-byte template, pinned by
   `testdata/uccs_vectors.h` and parsed at fixed offsets, may grow. Touches
   `docs/xkms-adapter/architecture.md` only; proved by the ruling being
   quoted there.
2. **Firmware: the digest claim.** After slice 1: the claims set takes the
   digest of the A6 record (`src/caps/uccs.c`, `src/caps/uccs.h`,
   `src/caps/csr.c` or `src/caps/alias.c` per slice 1, a digest helper beside
   `src/core/hsm_record.c`), new pinned vectors in
   `testdata/uccs_vectors.h` and their Go mirror in `sdk/go/`; proved by new
   cases in `test/unit/test_uccs.c` and `test/unit/test_uccs_mutate.c` (an
   empty record, one entry, a full record with its dropped count, and a
   changed entry changing the digest) under `make host-test`. A wire or
   vector change, so the owner's go-ahead per ruling 109.
3. **Adapter: the route.** After slice 2 and an `sdk/go` tag that carries
   `HSMRecordReadAll`: `xkms/avr/` reads the record through the SDK and
   returns it beside the evidence, and checks that its digest matches the
   claim (`xkms/avr/attestation.go` or a new `xkms/avr/record.go`, the
   `sdk/go` requirement in `xkms/go.mod`); proved by unit tests in a new
   `xkms/avr/record_test.go` (a record whose digest matches, one changed
   entry refused, an image without `TFW_FEATURE_HSM` refused before the
   wire) under `make xkms-test`, and the go-xkms verifier's tests in go-xkms.
4. **Hardware.** The record and its digest from a real board: needs the M7
   of row `imx95` running the engine (row `hsm-self-defence`, slice 3 on
   QEMU first) and the A6 durable store (slice 11b-3); recorded in
   `docs/development/bench/frdm-imx95.md`, proved by the slice 3 test run
   against the board.

None is a lane's work in this repository until go-xkms #1765 fixes the
format and the owner rules on slice 1; slices 2 and 3 can then be seated in
order, and slice 4 waits on the board. The SPEC status change is the owner's.
