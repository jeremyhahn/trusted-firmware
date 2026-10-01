# Host SDK — Go API surface

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

What a consumer imports. Full method list, invariants and error table:
`engineering.md`. Design rationale: `architecture.md`.

## Import paths

```go
import (
    ag "github.com/jeremyhahn/arduino-generic/sdk/go"
    "github.com/jeremyhahn/arduino-generic/sdk/go/serialport"
)
```

`ag` (package `aggeneric`) is stdlib-only. Import `serialport` only if you're
opening a physical port — it's the sole reason `go.bug.st/serial` enters your
build. A consumer that only decodes frames (verifying evidence, say) needs
just `ag`.

## Connecting

`ag.New` wraps any `io.ReadWriter` — a serial port via `serialport.Open`, a
pty, or an `agsim` subprocess pipe, which is how the SDK's own tests run
without hardware:

```go
conn, err := serialport.Open("/dev/ttyUSB0", serialport.DefaultBaud)
if err != nil {
    return err
}
defer conn.Close()

dev := ag.New(conn, ag.WithTimeout(10*time.Second))
ctx := context.Background()

caps, err := dev.Caps(ctx)
if err != nil {
    var ve *ag.ProtocolVersionError
    if errors.As(err, &ve) {
        // caps is still populated; log what the device claimed before refusing it
    }
    return err
}
if !caps.Has(ag.FeatureAttest) {
    return errors.New("device has no attestation support")
}
```

## Surface groups

Everything below is a method on `*ag.Device` unless noted. Signatures and the
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
| raw codec | `ag.EncodeRequest`, `ag.EncodeResponse`, `ag.Decode`, `ag.CRC16` |

## Error handling

Every method returns a typed error from `sdk/go/errors.go`. Match with
`errors.As` for the fields, `errors.Is` against the five published sentinels
(`ag.ErrShortFrame`, `ag.ErrBadCRC`, `ag.ErrBadFormat`, `ag.ErrPayloadTooLarge`,
`ag.ErrTimeout`) for the category. Full table: `engineering.md#error-semantics`.

```go
_, err := dev.EEPROMWrite(ctx, addr, data)
var rangeErr *ag.ArgRangeError
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
// next resets. Reset(ResetBootloader) is not gated (issue #28).
```

`deviceSerial` is the device's own platform identity (`SerialLen` bytes) —
obtainable from wherever it was recorded at provisioning, or from a fresh
`Attest` call's evidence (`DeviceID`). Not enforced on the soft or
`CRYPTO=none` tier: see `docs/security/threat-model.md`.

## What is not exported

No transport type beyond `io.ReadWriter`. No logging or metrics hooks — see
`engineering.md#observability`. No YAML or config-file parser — `agprov`'s
template format is internal to `cmd/agprov`, not part of this API.
