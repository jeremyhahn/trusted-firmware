# AVR Device Backend — API

> **Status: NORMATIVE** · Scope: trusted-firmware · Feature: `xkms-adapter` · Last verified: 2026-10-20
> Conflicts with this document are defects. Fix them here first.

## Transports

| Transport | Reachable |
|---|---|
| Embedded Go API | **yes** |
| REST | no |
| gRPC | no |
| MCP | no |
| CLI | no |
| SDK (`sdk/go`) | no — `sdk/go` is Layer 0 and carries no AVR backend symbols; the wire client is `tfw.Device` |
| Go module `github.com/jeremyhahn/trusted-firmware/xkms` | **yes** — `avr.NewBackend`, `avr.NewKeyProvider`, and the `AVR`-prefixed re-exports in package `xkms` |
| go-xkms backend registry | **yes** — registered as type `tfw` by `xkms/register.go` |
| ACME `device-attest-01` | indirectly — the caller forwards `Attestation.Chain` (leaf first, the device's `CertificateDER` verbatim) as the `x5c` of `dice`-format evidence |

The backend is not served over a network transport by this repository. It
registers with go-xkms, so a daemon that links package `xkms` and puts an attached
`*tfw.Device` in `AutoConfig.BackendConfigs["tfw"]` can construct it; opening the
port is the host program's job. A daemon configuration that names a device's
serial port, and the lifetime of the link over it, is a decision for the daemon's
owner — which port belongs to which tenant, what happens when the device is
unplugged mid-request, whether one process may hold two devices.

## Registration

```go
import (
    tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
    _ "github.com/jeremyhahn/trusted-firmware/xkms" // registers "tfw" in init
    "github.com/jeremyhahn/trusted-firmware/xkms/avr"
    goxkms "github.com/jeremyhahn/go-xkms/pkg/xkms"
)

err := goxkms.AutoInitialize(&goxkms.AutoConfig{
    BackendConfigs: map[goxkms.BackendType]map[string]interface{}{
        goxkms.BackendType(avr.BackendType): {
            avr.ConfigKeyDevice: tfw.New(port), // required: *tfw.Device
            avr.ConfigKeyConfig: avr.DefaultConfig(), // optional: *avr.Config
        },
    },
    // ... the rest of go-xkms's configuration
})
```

The `init` panics if the name `tfw` is already registered. `RegisterAVRBackend`
is exported, but `init` has already called it; a second call returns an error
satisfying `errors.Is(err, types.ErrBackendTypeRegistered)`. A value of the wrong
type under either key fails with `ErrInvalidFactoryConfig`, and a missing device
with `ErrNilDevice`.

## Embedded Go API

```go
import (
    tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
    "github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
    "github.com/jeremyhahn/trusted-firmware/xkms/avr"
    "github.com/jeremyhahn/go-xkms/pkg/types"
)

port, err := serialport.Open("/dev/ttyUSB0", 115200)   // the caller's link
if err != nil {
    return err
}
defer port.Close()

backend, err := avr.NewBackend(ctx, avr.DefaultConfig(), tfw.New(port))
if err != nil {
    return err
}
defer backend.Close()

level, err := backend.AttestLevel()          // what the device's tier establishes
algs := backend.Algorithms()                 // what the element can actually perform
key, err := backend.GetKey(ctx, &types.KeyAttributes{CN: avr.DefaultKeyCN})
```

### Attestation

```go
// The challenge is SHA-256 of the ACME key authorization. Hash it HERE and
// nowhere else -- one function so a device, a provisioning tool and a verifier
// cannot each hash something slightly different.
nonce := deviceattest01.DICENonce(keyAuth)

result, err := backend.AttestKey(ctx, &types.KeyAttributes{CN: avr.DefaultKeyCN}, nonce)
if err != nil {
    return err                                // a device that cannot answer is not a pass
}
att := result.(*avr.Attestation)

att.CertificateDER        // forward these bytes verbatim; never re-encode them
att.Chain                 // x5c, leaf first: CertificateDER, then the configured Intermediates
att.Claims.Nonce          // the challenge, already matched in constant time
att.Claims.Other[256]     // the ueid, as the raw CBOR the device encoded
att.Claims.Other[273]     // the measurement (application evidence, else the firmware's), likewise
att.AttestLevel           // from the device's crypto tier
att.Anchored              // false means no roots were configured -- not that anchoring failed
```

Anchoring is configured, not passed per call:

```go
cfg := avr.DefaultConfig()
cfg.Attestation = &avr.AttestationConfig{
    TrustStore:    &truststore.Config{ExternalRootPaths: []string{"/etc/xkms/avr-roots.pem"}},
    Intermediates: [][]byte{deviceIDCertDER},   // the device has nowhere to keep these
}
```

A trust store that will not load fails `NewBackend`, not the first attestation:
discovering it later would mean discovering it in front of a device waiting to
enrol.

`xkms/avr` reaches the wire only through the SDK's root package. The `serialport` subpackage above is the
caller's choice and brings `go.bug.st/serial` into the caller's graph, not into
this one — any `io.ReadWriter` will do, including a pty or a subprocess pipe.

`backend` satisfies both `types.KeyProvider` and `types.AttestingKeyProvider`,
so it composes anywhere either interface is accepted. It is safe to use from
several goroutines: every command is serialised on the link, and a multi-frame
exchange such as `AttestKey` stays contiguous.

### Finalize

`backend.PossessionSigner(att)` returns the `crypto.Signer` that proves possession
of the attested key. It asks the device for `Device.AttestCSR` and returns a
signature only when it verifies over the digest it was asked to sign; any other
digest returns `ErrPossessionMismatch`, and an `att` this backend did not produce
returns `ErrInvalidAttestation`.

### Through package `xkms`

A consumer that wants one import for the backend uses the root package, which
aliases the `avr` declarations rather than copying them:

```go
import (
    tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
    xkms "github.com/jeremyhahn/trusted-firmware/xkms"
)

backend, err := xkms.NewAVRBackend(ctx, xkms.DefaultAVRConfig(), tfw.New(port))
if errors.Is(err, xkms.ErrAVRNoDeviceKey) {
    // a part with no crypto provider to hold an identity
}
```

The device comes from `trusted-firmware/sdk/go` directly, which this module does
not re-export.

`Config.RequestTimeout` is applied to the device as well as to each call's
context, so a 60-second timeout waits 60 seconds on a device that is slow rather
than the SDK's own 10-second default.

## Backend type

The backend's type, and its keys' store type, is `"tfw"` (`avr.BackendType`,
re-exported as `xkms.AVRBackendType`). go-xkms's own `types.BackendTypeAVR`
(`"avr"`) names its built-in backend, not this one, until go-xkms#1439 step 3; a
Key ID beginning `avr:` does not resolve to this backend, and a Key ID reads
`tfw:...`.
