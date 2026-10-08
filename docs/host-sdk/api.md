# Host SDK — Go API surface

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

What a consumer imports. Full method list, invariants and error table:
`engineering.md`. Design rationale: `architecture.md`.

## Import paths

```go
import (
    "github.com/jeremyhahn/trusted-firmware/sdk/go" // package tfw
    "github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)
```

`ag` (package `tfw`) is stdlib-only. Import `serialport` only if you're
opening a physical port — it's the sole reason `go.bug.st/serial` enters your
build. A consumer that only decodes frames (verifying evidence, say) needs
just `ag`.

## Testing against the firmware: `sdk/go/tfwsim`

```go
import "github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
```

Starts the firmware, compiled for the host, as a peer for a test. Every function
takes a `testing.TB`, stops the process when the test ends, and **fails** the
test (never skips) if the binary is not built.

| Function | Runs |
|---|---|
| `tfwsim.Start(t, opts...)` | `tfwsim`, the software tier: no asymmetric key, `ATTEST_CERT` is `unsupported` |
| `tfwsim.StartElement(t, opts...)` | `tfwsim-atecc608a` provisioned with the test identity: real DICE `ATTEST` and `ATTEST_CERT` |
| `tfwsim.StartBlankElement(t, opts...)` | the same binary with nothing provisioned: attestation is `not provisioned` |
| `tfwsim.StartElementWith(t, args, opts...)` | the element tier with your own `--eeprom`, `--provision-element`, `--image-hash` arguments |
| `tfwsim.StartElementWithOperator(t, pub, opts...)` | `StartElement` with the P-256 public key `pub` baked in as the operator, so `AuthChallenge`/`AuthResponse` can be exercised with a key the test generated |

`opts` are ordinary `tfw.Option`s. `tfwsim.TestSerial`, `tfwsim.TestImage` and
`tfwsim.TestFWHash` are the identity `StartElement` provisions, for asserting
against, and `tfwsim.TestDeviceIDPublicKey()` is the `*ecdsa.PublicKey` its
evidence is signed with. The element model does real P-256, so the `ATTEST`
frame and the `ATTEST_CERT` certificate **verify** under that key with
`crypto/ecdsa` and `crypto/x509`. **It is still a test identity and must never be
trusted:** the private key is the SHA-256 of a published string, so anyone can
sign as this device. Accept the key only in a test that supplied it. See
`docs/dice-attestation/user.md` for what that does and does not let you test.

**Finding the binaries.** `$TFWSIM_DIR` if set, otherwise `build/host` in the
checkout the package was compiled from. A module that depends on this one
through a `replace` to a sibling checkout gets the right answer with no
configuration; one resolving it from the module cache, or built with
`-trimpath`, must set `TFWSIM_DIR`. Build both with
`make tfwsim tfwsim-atecc608a`. The package imports only this module and the
standard library, so `make sdk-verify` is unaffected.

## Connecting

`tfw.New` wraps any `io.ReadWriter` — a serial port via `serialport.Open`, a
pty, or an `tfwsim` subprocess pipe, which is how the SDK's own tests run
without hardware:

```go
conn, err := serialport.Open("/dev/ttyUSB0", serialport.DefaultBaud)
if err != nil {
    return err
}
defer conn.Close()

dev := tfw.New(conn, tfw.WithTimeout(10*time.Second))
ctx := context.Background()

caps, err := dev.Caps(ctx)
if err != nil {
    var ve *tfw.ProtocolVersionError
    if errors.As(err, &ve) {
        // caps is still populated; log what the device claimed before refusing it
    }
    return err
}
if !caps.Has(tfw.FeatureAttest) {
    return errors.New("device has no attestation support")
}
```

## Surface groups

Everything below is a method on `*tfw.Device` unless noted. Signatures and the
full constant list are in `engineering.md`; this is the shape a consumer
reaches for by task.

| Task | Call |
|---|---|
| liveness / framing check | `Ping(ctx, msg []byte) ([]byte, error)` |
| discover the board | `Caps(ctx) (*Caps, error)`, `PinInfo(ctx, pin) (*PinInfo, error)` |
| runtime state | `SysInfo(ctx) (*SysInfo, error)` |
| GPIO | `SetPinMode`, `DigitalRead`, `DigitalWrite` |
| device-held timers | `TimerSet`, `TimerCancel`, `TimerQuery` |
| analog | `AnalogRead`, `SetADCReference` |
| PWM / tone | `PWMWrite`, `Tone`, `NoTone` |
| timing-critical sensors | `PulseIn`, `PulseCapture` |
| EEPROM | `EEPROMRead`, `EEPROMWrite` |
| buses | `I2CTransfer`, `SetI2CClock`, `SPITransfer`, `ConfigureSPI`, `OneWireReset/Write/Read` |
| attestation | `Attest(ctx, nonce []byte) ([]byte, error)`, `AttestCert(ctx, nonce []byte) ([]byte, error)` |
| auth session (element tier only) | `AuthChallenge(ctx) ([]byte, error)`, `AuthResponse(ctx, signer crypto.Signer, nonce, serial []byte) error` |
| provisioning (jig image only) | `ProvInfo`, `ProvWriteConfig`, `ProvLock`, `ProvGenKey`, `ProvPubKey`, `ProvSeedUDS` |
| raw codec | `tfw.EncodeRequest`, `tfw.EncodeResponse`, `tfw.Decode`, `tfw.CRC16` |

## Error handling

Every method returns a typed error from `sdk/go/errors.go`. Match with
`errors.As` for the fields, `errors.Is` against the five published sentinels
(`tfw.ErrShortFrame`, `tfw.ErrBadCRC`, `tfw.ErrBadFormat`, `tfw.ErrPayloadTooLarge`,
`tfw.ErrTimeout`) for the category. Full table: `engineering.md#error-semantics`.

```go
_, err := dev.EEPROMWrite(ctx, addr, data)
var rangeErr *tfw.ArgRangeError
if errors.As(err, &rangeErr) {
    // rangeErr.Arg, .Value, .Min, .Max, .Unit — nothing was sent to the device
}
```

## Attestation consumers

`go-xkms` is the intended importer of `Attest`/`AttestCert`. Neither call
holds or returns a private key — `AttestCert` returns DER bytes the device
signed itself; the nonce widths (`AttestNonceLen` 16, `AliasNonceLen` 32) are
deliberately different and must not be conflated (see `attest.go` and
`docs/dice-attestation/architecture.md`).

## The auth session

`AuthChallenge`/`AuthResponse` (issue #4) authenticate the HOST to an
element-tier device — the opposite direction from attestation, which
authenticates the device to a verifier. Custody of the operator private key
belongs to whoever calls this SDK — typically `go-trusted-platform`, per L2
ownership — never to `sdk/go` itself, which is why `AuthResponse` takes a
`crypto.Signer` rather than a key. Typical use:

```go
nonce, err := dev.AuthChallenge(ctx)
// ...
err = dev.AuthResponse(ctx, operatorSigner, nonce, deviceSerial)
// EEPROMRead below EEPROMReservedEnd now succeeds on this device, until it
// next resets. Reset(ResetBootloader) needs this session too.
```

`deviceSerial` is the device's own platform identity (`SerialLen` bytes) —
obtainable from wherever it was recorded at provisioning, or from a fresh
`Attest` call's evidence (`DeviceID`). Not enforced on the soft or
`CRYPTO=none` tier: see `docs/security/threat-model.md`.

## What is not exported

No transport type beyond `io.ReadWriter`. No logging or metrics hooks — see
`engineering.md#observability`. No YAML or config-file parser — `tfwprov`'s
template format is internal to `cmd/tfwprov`, not part of this API.
