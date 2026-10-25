# Host SDK — Go API surface

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-21
> Conflicts with this document are defects. Fix them here first.

What a consumer imports. Every exported symbol, with its signature and the
first sentence of its doc comment: [Exported symbols](#exported-symbols), kept
whole by `scripts/api-doc-check.py`. Invariants and error table:
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
| `tfwsim.StartProvisioningWith(t, args, opts...)` | `tfwsim-prov`, the provisioning jig image, with your own arguments: the `PROV_*` commands answer |
| `tfwsim.StartElementApp`, `tfwsim.StartElementVia`, `tfwsim.StartElementEthernet` | the element tier with `test/app/example` linked, through a wrapper you supply, or as an Ethernet device: see [Exported symbols](#exported-symbols) |

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
| runtime state | `SysInfo(ctx) (*SysInfo, error)`, `AppInfo(ctx) (*AppInfo, error)` |
| GPIO | `SetPinMode`, `DigitalRead`, `DigitalWrite` |
| device-held timers | `TimerSet`, `TimerCancel`, `TimerQuery` |
| analog | `AnalogRead`, `SetADCReference` |
| PWM / tone | `PWMWrite`, `Tone`, `NoTone` |
| timing-critical sensors | `PulseIn`, `PulseCapture` |
| EEPROM | `EEPROMRead`, `EEPROMWrite` |
| buses | `I2CTransfer`, `SetI2CClock`, `SPITransfer`, `ConfigureSPI`, `OneWireReset/Write/Read` |
| attestation | `Attest(ctx, nonce []byte) ([]byte, error)`, `AttestCert(ctx, nonce []byte) ([]byte, error)`, `AttestCSR(ctx, b [32]byte) ([64]byte, error)` |
| auth session (element tier only) | `AuthChallenge(ctx) ([]byte, error)`, `AuthResponse(ctx, signer crypto.Signer, nonce, serial []byte) error` |
| provisioning (jig image only) | `ProvInfo`, `ProvWriteConfig`, `ProvLock`, `ProvGenKey`, `ProvPubKey`, `ProvSeedUDS` |
| network block | `ReadNetConfig`, `WriteNetConfig`, `tfw.DerivedMAC` ([ethernet-transport](../ethernet-transport/api.md)) |
| OTA | `StageImage`, or `OTABegin`/`OTAData`/`OTAEnd`/`OTAStatus`; `OTAAuth`, `OTAInfo`, `OTAActivate`, `OTAConfirm` ([ota](../ota/api.md)) |
| accept Ethernet devices | `tfw.Listen(addr, opts...) (*Listener, error)`, `(*Listener) Accept() (*TCPDevice, error)`; a `*TCPDevice` is a `*Device` |
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

`go-xkms` is the intended importer of `Attest`/`AttestCert`/`AttestCSR`. None
of the calls holds or returns a private key — `AttestCert` returns DER bytes the
device signed itself, and `AttestCSR` returns the 64-byte signature the device
made, with no request built around it and none verified (that is go-xkms's; see
`docs/dice-attestation/api.md`); the nonce widths (`AttestNonceLen` 16, `AliasNonceLen` 32) are
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

No transport type beyond `io.ReadWriter` and the TCP `Listener` that hands
each accepted connection to `New`. No logging or metrics hooks — see
`engineering.md#observability`. No YAML or config-file parser — `tfwprov`'s
template format is internal to `cmd/tfwprov`, not part of this API.

## Exported symbols

Every exported function, type, method, constant and variable of the three
public packages, build-tag-gated files included, in `go doc` order. A row is
`pkg.Name`, or `pkg.Type.Method` for a method; a group of constants that share
a type and a doc comment is one row. The contract is the first sentence of the
doc comment, which is where to change it. `make scripts-test` fails when a
symbol is exported and has no row here, or a row names one that is gone.

### `tfw` (`sdk/go`)

| Symbol | Signature | Contract |
|---|---|---|
| `tfw.AttestNonceLen` | `const AttestNonceLen = 16` | RUNTIME FRESHNESS: the nonce carried in the 164-byte evidence frame, answering "is this device reporting now". |
| `tfw.AliasNonceLen` | `const AliasNonceLen = 32` | ENROLMENT: the nonce carried in the eat_nonce claim of the alias certificate, answering "is this key on this device". |
| `tfw.EvidenceLen` | `const EvidenceLen = 164` | The size of one complete attestation frame. |
| `tfw.SOF`, `tfw.MaxPayload` | `const SOF = 0xA5`, `const MaxPayload = 64` | The wire contract: the start-of-frame byte and the largest payload. |
| `tfw.SeqNone` | `const SeqNone Seq = 0` | The wire contract: the sequence number of an unsolicited event. |
| `tfw.ProtoVersionMajor`, `tfw.ProtoVersionMinor` | `const ProtoVersionMajor uint8 = 2`, `const ProtoVersionMinor uint8 = 0` | The version of the wire this host speaks, mirroring TFW_PROTO_VERSION_MAJOR and _MINOR. |
| `tfw.I2CClockMin`, `tfw.I2CClockMax` | `const I2CClockMin = 1_000`, `const I2CClockMax = 400_000` | Bound what SetI2CClock will send. |
| `tfw.EventBoot`, `tfw.EventDeadman`, `tfw.EventCounterWrap` | `const` 0x01, 0x02, 0x03 | Events the device may send unsolicited. |
| `tfw.FeatureGPIO`, `tfw.FeatureSched`, `tfw.FeatureAnalog`, `tfw.FeaturePWM`, `tfw.FeatureTone`, `tfw.FeaturePulse`, `tfw.FeatureEEPROM`, `tfw.FeatureI2C`, `tfw.FeatureSPI`, `tfw.FeatureOneWire`, `tfw.FeatureAttest`, `tfw.FeatureDeadman`, `tfw.FeatureProvision`, `tfw.FeatureAliasCert` | `const FeatureGPIO uint32 = 1 << iota`, … | Feature bits, mirroring TFW_FEATURE_BITMAP. |
| `tfw.FeatureAuthSession` | `const FeatureAuthSession` (bit 14) | Issue #4's AUTH_CHALLENGE/AUTH_RESPONSE session -- element tier only. |
| `tfw.FeatureAttestCSR` | `const FeatureAttestCSR` (bit 15) | Issue #30's ATTEST_CSR -- the possession signature over a device-templated CertificationRequestInfo. |
| `tfw.NetConfigAddr`, `tfw.NetConfigLen` | `const NetConfigAddr = EEPROMReservedEnd` (0x140), `const NetConfigLen = 32` | The network block, [0x140, 0x160) on the device, where the bootloader's reserved span ends (#231), mirroring include/tfw/eth.h's TFW_NETCFG_*. The tfwsim harness asks the binary (`tfwsim --netcfg-addr`) instead of using `NetConfigAddr`. |
| `tfw.OTAActivateAtReset` | `const OTAActivateAtReset byte = 0` | OTA_ACTIVATE mode: tried at the next reset. |
| `tfw.OTAActivateResetNow` | `const OTAActivateResetNow byte = 1` | OTA_ACTIVATE mode: the device resets once the reply is out. |
| `tfw.SlotFellBack` | `const SlotFellBack uint8 = 1 << 0` | SlotInfo flag: the last boot fell back. |
| `tfw.SlotRefused` | `const SlotRefused uint8 = 1 << 1` | SlotInfo flag: the fallback followed a refusal at evaluation. |
| `tfw.SlotTrial` | `const SlotTrial uint8 = 1 << 2` | SlotInfo flag: the device is in TRIAL. |
| `tfw.PlatformBlockLen`, `tfw.SerialLen`, `tfw.PubkeyHashLen`, `tfw.OperatorPubkeyLen` | `const` 192, 16, 32, 64 | The platform-reserved EEPROM block, [0x000, 0x0C0) on the device, mirroring include/tfw/boot.h. |
| `tfw.ZoneConfig`, `tfw.ZoneData` | `const uint8` 0x00, 0x01 | Element zones, for Lock. |
| `tfw.SlotDeviceID`, `tfw.SlotUDS` | `const uint8` 0, 1 | Slot assignments, mirroring TFW_ATECC_SLOT_* in src/crypto/atecc608a/atecc.h. |
| `tfw.ApplicationEvidenceLen` | `const ApplicationEvidenceLen = 32` | The width of the value in the measurements claim (273): one sha-256 hash-entry. |
| `tfw.AuthNonceLen` | `const AuthNonceLen = 16` | The challenge's width. |
| `tfw.CSRSigLen` | `const CSRSigLen = 64` | The length of the ATTEST_CSR response: a P-256 signature as raw r\|\|s (TFW_SIG_LEN). |
| `tfw.DefaultBootWait` | `const DefaultBootWait = 2 * time.Second` | How long New waits for the firmware's BOOT event before proceeding anyway. |
| `tfw.EEPROMChunk` | `const EEPROMChunk = 8` | The largest read or write the device accepts in one command. |
| `tfw.EEPROMReservedEnd` | `const EEPROMReservedEnd = 0xC0` | The first address past the platform block, [0x000, 0x0C0) on every board. |
| `tfw.HSMSignalIMAViolation`, `tfw.HSMSignalHotplug` | `const byte` 4, 5 | The signals the A55 may report (architecture.md section 1). |
| `tfw.HSMVerdictClear`, `tfw.HSMVerdictCompromised` | `const byte` 0, 1 | The verifier's verdict V. |
| `tfw.HSMStateLen` | `const HSMStateLen = 6` | The reply every HSM command carries when it is not refused. |
| `tfw.HSMLoadChunk` | `const HSMLoadChunk = 60` | The most envelope bytes one HSM_LOAD carries: the payload less its total and offset. |
| `tfw.HSMEnvelopeMax` | `const HSMEnvelopeMax = 1024` | The largest envelope the device takes (TFW_SUIT_INTEGRATED_MAX); a larger one is refused with StatusErrRange. |
| `tfw.HSMSetHeld`, `tfw.HSMLocked`, `tfw.HSMWiped` | `const uint8` 1<<0, 1<<1, 1<<2 | HSM state flags: a verified policy set is held; the device keys are locked; the device keys are wiped. |
| `tfw.I2CReservedAddr` | `const I2CReservedAddr = 0x60` | The address the device refuses. |
| `tfw.OTAChunk` | `const OTAChunk = 60` | The most image bytes one OTA_DATA carries: the payload less the offset. |
| `tfw.OTAInfoLen` | `const OTAInfoLen = 44` | OTA_INFO's reply length. |
| `tfw.OTAAuthSigLen` | `const OTAAuthSigLen = 64` | OTA_AUTH's payload, the ES256 r\|\|s (#137). |
| `tfw.ErrShortFrame` | `var ErrShortFrame error` | A buffer does not yet hold the frame its length field claims. |
| `tfw.ErrBadCRC` | `var ErrBadCRC error` | The category of a frame whose checksum does not match. |
| `tfw.ErrBadFormat` | `var ErrBadFormat error` | The category of a structurally impossible frame. |
| `tfw.ErrPayloadTooLarge` | `var ErrPayloadTooLarge error` | The category of a payload over MaxPayload. |
| `tfw.ErrTimeout` | `var ErrTimeout error` | The category of a device that did not answer in time. |
| `tfw.AliasUCCS` | `func AliasUCCS(der []byte) ([]byte, error)` | Returns the claims set the alias certificate der carries in its tcg-dice-UCCS-evidence extension. |
| `tfw.ApplicationEvidence` | `func ApplicationEvidence(uccs []byte) ([ApplicationEvidenceLen]byte, error)` | Returns the 32 bytes in the measurements claim (273) of the claims set uccs. |
| `tfw.BuildPlatformBlock` | `func BuildPlatformBlock(serial [SerialLen]byte, pubkeyHash [PubkeyHashLen]byte, operatorPubkey [OperatorPubkeyLen]byte) [PlatformBlockLen]byte` | Serialises a factory-fresh platform block: magic, layout version, pubkey hash, serial, operator key and a CRC. |
| `tfw.CRC16` | `func CRC16(b []byte) uint16` | Computes the CRC of a buffer. |
| `tfw.CRC16Update` | `func CRC16Update(crc uint16, b byte) uint16` | Folds one byte into a running CRC. |
| `tfw.DerivedMAC` | `func DerivedMAC(id []byte) net.HardwareAddr` | The MAC address a device uses until its shield's own is provisioned. |
| `tfw.EncodeRequest` | `func EncodeRequest(seq Seq, cmd Cmd, payload []byte) ([]byte, error)` | Builds a request frame. |
| `tfw.EncodeResponse` | `func EncodeResponse(seq Seq, cmd Cmd, st Status, payload []byte) ([]byte, error)` | Builds a response frame, as a device would. |
| `tfw.Decode` | `func Decode(b []byte) (Frame, int, error)` | Parses one frame from the front of b, returning it and the number of bytes consumed. |
| `tfw.New` | `func New(conn io.ReadWriter, opts ...Option) *Device` | Wraps a byte stream. |
| `tfw.Listen` | `func Listen(addr string, opts ...Option) (*Listener, error)` | Opens a TCP listener on addr ("host:port"; port 0 picks one) for devices to connect to. |
| `tfw.NewListener` | `func NewListener(l net.Listener, opts ...Option) *Listener` | Wraps a listener the caller already owns -- one with TLS in front of it, say. |
| `tfw.WithBootWait` | `func WithBootWait(d time.Duration) Option` | Overrides how long New waits for the BOOT event. |
| `tfw.WithTimeout` | `func WithTimeout(d time.Duration) Option` | Sets how long to wait for a reply. |
| `tfw.ADCReference` | `type ADCReference uint8` | Selects what the converter measures against. |
| `tfw.ADCRefVCC`, `tfw.ADCRefExternal`, `tfw.ADCRefInternal` | `const ADCReference` 0, 1, 2 | The supply rail, the AREF pin, the internal bandgap. |
| `tfw.AlgorithmSet` | `type AlgorithmSet uint16` | The set of signature algorithms a device's crypto provider can actually perform, as CAPS reports it. |
| `tfw.AlgECDSAP256SHA256`, `tfw.AlgEd25519`, `tfw.AlgHMACSHA256` | `const AlgorithmSet = 1 << iota`, … | Mirrors TFW_ALGBIT_* in include/tfw/protocol.h. |
| `tfw.AlgorithmSet.Can` | `func (s AlgorithmSet) Can(want AlgorithmSet) bool` | Reports whether the device can perform every algorithm in want. |
| `tfw.AlgorithmSet.String` | `func (s AlgorithmSet) String() string` | Names each algorithm in the set, unnamed bits as a number; "(none)" for the empty set. |
| `tfw.AppInfo` | `type AppInfo struct` | The application linked into the image and how its runner is doing -- APP_INFO, issue #51. |
| `tfw.AppState` | `type AppState uint8` | The application runner's state. |
| `tfw.AppIdle`, `tfw.AppRunning`, `tfw.AppFailed`, `tfw.AppUnsupported`, `tfw.AppStopped` | `const AppState` 0…4 | The runner states APP_INFO reports. |
| `tfw.AppState.String` | `func (s AppState) String() string` | Names the state; an unrecognised value is "unknown". |
| `tfw.ArgRangeError` | `type ArgRangeError struct` | An argument the wire cannot carry, refused HERE rather than sent for the device to reject. |
| `tfw.ArgRangeError.Error` | `func (e *ArgRangeError) Error() string` | Implements error. |
| `tfw.AuthSignError` | `type AuthSignError struct` | The caller-supplied signer failed, or produced something not convertible to the wire's raw R\|\|S form. |
| `tfw.AuthSignError.Error` | `func (e *AuthSignError) Error() string` | Implements error. |
| `tfw.AuthSignError.Unwrap` | `func (e *AuthSignError) Unwrap() error` | Returns the underlying error, so errors.Is and errors.As see through it. |
| `tfw.BoardID` | `type BoardID uint8` | The one-byte board identifier CAPS reports. |
| `tfw.BoardATmega328P`, `tfw.BoardATmega2560`, `tfw.BoardATmega32U4`, `tfw.BoardATtiny85`, `tfw.BoardMPS2AN505`, `tfw.BoardFRDMIMX95M7`, `tfw.BoardESP32P4PoE`, `tfw.BoardESP32QEMU` | `const BoardID` 0x01…0x81 | The ids allocated so far. |
| `tfw.BoardID.Family` | `func (b BoardID) Family() string` | Names the architecture family whose range the id falls in, or "unallocated" for 0x00 and 0xC0-0xFF. |
| `tfw.BoardID.String` | `func (b BoardID) String() string` | The board's name when this SDK knows it. |
| `tfw.CRCError` | `type CRCError struct` | A checksum that disagrees with the bytes it covers, carrying both values because they say which side to suspect. |
| `tfw.CRCError.Error` | `func (e *CRCError) Error() string` | Implements error. |
| `tfw.CRCError.Is` | `func (e *CRCError) Is(target error) bool` | Reports whether target is ErrBadCRC. |
| `tfw.Caps` | `type Caps struct` | What a device reports about itself. |
| `tfw.Caps.Has` | `func (c Caps) Has(feature uint32) bool` | Reports whether a feature was compiled into this build. |
| `tfw.Cmd` | `type Cmd uint8` | A command id. |
| `tfw.CmdCaps`, `tfw.CmdPing`, `tfw.CmdReset`, `tfw.CmdSysInfo`, `tfw.CmdAppInfo`, `tfw.CmdPinMode`, `tfw.CmdDigitalRead`, `tfw.CmdDigitalWrite`, `tfw.CmdTimerSet`, `tfw.CmdTimerCancel`, `tfw.CmdTimerQuery`, `tfw.CmdAnalogRead`, `tfw.CmdADCRef`, `tfw.CmdEEPROMRead`, `tfw.CmdEEPROMWrite`, `tfw.CmdPWMWrite`, `tfw.CmdTone`, `tfw.CmdNoTone`, `tfw.CmdPulseIn`, `tfw.CmdPulseCapture`, `tfw.CmdI2CXfer`, `tfw.CmdSPIXfer`, `tfw.CmdOneWireReset`, `tfw.CmdOneWireWrite`, `tfw.CmdOneWireRead`, `tfw.CmdI2CConfig`, `tfw.CmdSPIConfig`, `tfw.CmdAttest` | `const Cmd` 0x00…0x60 | Commands. |
| `tfw.CmdAttestCert`, `tfw.CmdAttestCSR` | `const Cmd` 0x63, 0x64 | ATTEST_CERT, and ATTEST_CSR: request [b:32], response [sig:64], issue #30. |
| `tfw.CmdAuthChallenge`, `tfw.CmdAuthResponse` | `const Cmd` 0x61, 0x62 | The operator-signature auth session -- issue #4. |
| `tfw.CmdOTABegin`, `tfw.CmdOTAData`, `tfw.CmdOTAEnd`, `tfw.CmdOTAStatus`, `tfw.CmdOTAInfo`, `tfw.CmdOTAActivate` | `const Cmd` 0x68…0x6D | The OTA session: [total_len:4]; [offset:4]bytes; END; -> [state:1][received:4]; [slot:1] -> SlotInfo; [mode:1]. |
| `tfw.CmdOTAConfirm` | `const CmdOTAConfirm Cmd = 0x6E` | The rest of ADR decision 8 (#77), answered by an image whose tfw-boot runs the trial; any other answers StatusErrUnknownCmd. |
| `tfw.CmdOTAAuth` | `const CmdOTAAuth Cmd = 0x6F` | Holds the row key's ES256 authorization (r\|\|s) for the next OTA_ACTIVATE or OTA_CONFIRM (#137). |
| `tfw.CmdProvInfo`, `tfw.CmdProvWriteConfig`, `tfw.CmdProvLock`, `tfw.CmdProvGenKey`, `tfw.CmdProvPubKey`, `tfw.CmdProvSeedUDS` | `const Cmd` 0x70…0x75 | Provisioning commands. |
| `tfw.CmdHSMHeartbeat`, `tfw.CmdHSMReport`, `tfw.CmdHSMVerdict`, `tfw.CmdHSMSelect`, `tfw.CmdHSMLoad` | `const Cmd` 0x76…0x7A | The HSM self-defence channel -- docs/hsm-self-defence/; answered only by an image built with the channel, any other answers StatusErrUnknownCmd. |
| `tfw.CryptoTier` | `type CryptoTier uint8` | How strong the device's root of trust is. |
| `tfw.CryptoNone`, `tfw.CryptoSoftware`, `tfw.CryptoSecureElement`, `tfw.CryptoTPM2` | `const CryptoTier` 0…3 | The crypto tiers, numbered as TFW_CRYPTO_* in include/tfw/types.h. |
| `tfw.CryptoTier.String` | `func (c CryptoTier) String() string` | Names the tier, or says it is unrecognised with its number. |
| `tfw.Device` | `type Device struct` | One attached board. |
| `tfw.Device.AnalogRead` | `func (d *Device) AnalogRead(ctx context.Context, pin uint8) (uint16, error)` | Returns the RAW converter count, 0..1023 on a 10-bit ADC. |
| `tfw.Device.AppInfo` | `func (d *Device) AppInfo(ctx context.Context) (*AppInfo, error)` | Reads the application linked into the image and its runner's state. |
| `tfw.Device.Attest` | `func (d *Device) Attest(ctx context.Context, nonce []byte) ([]byte, error)` | Asks the device for nonce-bound attestation evidence. |
| `tfw.Device.AttestCSR` | `func (d *Device) AttestCSR(ctx context.Context, b [AliasNonceLen]byte) ([CSRSigLen]byte, error)` | Asks for the possession signature ACME finalize needs, as the 64 raw bytes r\|\|s the device sent. |
| `tfw.Device.AttestCert` | `func (d *Device) AttestCert(ctx context.Context, nonce []byte) ([]byte, error)` | Asks the device to mint an alias certificate carrying the challenge, and returns it as DER. |
| `tfw.Device.AuthChallenge` | `func (d *Device) AuthChallenge(ctx context.Context) ([]byte, error)` | Asks the device for a fresh, single-use nonce. |
| `tfw.Device.AuthResponse` | `func (d *Device) AuthResponse(ctx context.Context, signer crypto.Signer, nonce, serial []byte) error` | Signs the challenge with the operator key behind signer, opening a session that lasts until the device resets. |
| `tfw.Device.Caps` | `func (d *Device) Caps(ctx context.Context) (*Caps, error)` | Asks the device what it is. |
| `tfw.Device.ConfigureSPI` | `func (d *Device) ConfigureSPI(ctx context.Context, mode SPIMode, clkdiv SPIClockDiv, lsbFirst bool) error` | Reconfigures the generic SPI bus's mode, clock divider and bit order. |
| `tfw.Device.DigitalRead` | `func (d *Device) DigitalRead(ctx context.Context, pin uint8) (bool, error)` | Reads the level on a pin. |
| `tfw.Device.DigitalWrite` | `func (d *Device) DigitalWrite(ctx context.Context, pin uint8, high bool) error` | Drives a pin high or low. |
| `tfw.Device.Do` | `func (d *Device) Do(ctx context.Context, cmd Cmd, payload []byte) (Frame, error)` | Sends one command and returns its response frame. |
| `tfw.Device.EEPROMRead` | `func (d *Device) EEPROMRead(ctx context.Context, addr uint16, n uint8) ([]byte, error)` | Reads up to EEPROMChunk bytes. |
| `tfw.Device.EEPROMWrite` | `func (d *Device) EEPROMWrite(ctx context.Context, addr uint16, data []byte) error` | Writes up to EEPROMChunk bytes at or above EEPROMReservedEnd, outside any range the bootloader keeps. |
| `tfw.Device.Events` | `func (d *Device) Events() []Frame` | Returns and clears any unsolicited events seen so far. |
| `tfw.Device.HSMHeartbeat` | `func (d *Device) HSMHeartbeat(ctx context.Context) (*HSMState, error)` | Tells the engine the A55 is alive. |
| `tfw.Device.HSMLoad` | `func (d *Device) HSMLoad(ctx context.Context, envelope []byte) (*HSMState, error)` | Sends a signed policy set or script envelope in HSMLoadChunk pieces and returns the state after the last. |
| `tfw.Device.HSMReport` | `func (d *Device) HSMReport(ctx context.Context, signal byte) (*HSMState, error)` | Reports S4 or S5. |
| `tfw.Device.HSMSelect` | `func (d *Device) HSMSelect(ctx context.Context, index byte) (*HSMState, error)` | Names the policy of the held set to apply. |
| `tfw.Device.HSMVerdict` | `func (d *Device) HSMVerdict(ctx context.Context, verdict byte) (*HSMState, error)` | Delivers the verifier's verdict. |
| `tfw.Device.I2CTransfer` | `func (d *Device) I2CTransfer(ctx context.Context, addr7 uint8, write []byte, readLen uint8) ([]byte, error)` | Writes then reads in one transaction. |
| `tfw.Device.NoTone` | `func (d *Device) NoTone(ctx context.Context, pin uint8) error` | Stops the tone generator. |
| `tfw.Device.OTAActivate` | `func (d *Device) OTAActivate(ctx context.Context, mode byte) error` | Takes the staged image for the next boot. |
| `tfw.Device.OTAAuth` | `func (d *Device) OTAAuth(ctx context.Context, sig [OTAAuthSigLen]byte) error` | Sends the row key's authorization, signed by go-xkms, which the device holds for the next command only. |
| `tfw.Device.OTABegin` | `func (d *Device) OTABegin(ctx context.Context, total uint32) error` | Opens a session for an image of total bytes, trailer included. |
| `tfw.Device.OTAConfirm` | `func (d *Device) OTAConfirm(ctx context.Context) error` | Confirms the image on trial, after attestation. |
| `tfw.Device.OTAData` | `func (d *Device) OTAData(ctx context.Context, offset uint32, data []byte) error` | Sends the bytes at offset, which must be the next one the device expects; anything else is StatusErrRange. |
| `tfw.Device.OTAEnd` | `func (d *Device) OTAEnd(ctx context.Context) error` | Closes the session and has the device check the image against its trailer. |
| `tfw.Device.OTAInfo` | `func (d *Device) OTAInfo(ctx context.Context, slot byte) (*SlotInfo, error)` | Reads one slot's state: 0 the running image, 1 the store. |
| `tfw.Device.OTAStatus` | `func (d *Device) OTAStatus(ctx context.Context) (OTAState, uint32, error)` | Reports the session's state and how many bytes it has taken. |
| `tfw.Device.OneWireRead` | `func (d *Device) OneWireRead(ctx context.Context, pin uint8, n uint8) ([]byte, error)` | Reads n bytes from the bus. |
| `tfw.Device.OneWireReset` | `func (d *Device) OneWireReset(ctx context.Context, pin uint8) (bool, error)` | Pulses the bus and reports whether any device answered. |
| `tfw.Device.OneWireWrite` | `func (d *Device) OneWireWrite(ctx context.Context, pin uint8, data []byte) error` | Sends bytes on the bus. |
| `tfw.Device.PWMWrite` | `func (d *Device) PWMWrite(ctx context.Context, pin uint8, duty uint8) error` | Sets a duty cycle, 0..255. |
| `tfw.Device.PinInfo` | `func (d *Device) PinInfo(ctx context.Context, pin uint8) (*PinInfo, error)` | Asks the device about one pin. |
| `tfw.Device.Ping` | `func (d *Device) Ping(ctx context.Context, msg []byte) ([]byte, error)` | Echoes a payload through the device. |
| `tfw.Device.ProvGenKey` | `func (d *Device) ProvGenKey(ctx context.Context, slot uint8) ([]byte, error)` | Generates the DeviceID keypair inside the element and returns the public point. |
| `tfw.Device.ProvInfo` | `func (d *Device) ProvInfo(ctx context.Context) (*ElementInfo, error)` | Reads the element's identity and lock state. |
| `tfw.Device.ProvLock` | `func (d *Device) ProvLock(ctx context.Context, zone uint8, summaryCRC uint16) error` | Locks a zone. |
| `tfw.Device.ProvPubKey` | `func (d *Device) ProvPubKey(ctx context.Context, slot uint8) ([]byte, error)` | Recomputes the public point from the private key already in the slot. |
| `tfw.Device.ProvSeedUDS` | `func (d *Device) ProvSeedUDS(ctx context.Context, slot uint8) error` | Fills the UDS slot from the element's own TRNG. |
| `tfw.Device.ProvWriteConfig` | `func (d *Device) ProvWriteConfig(ctx context.Context, block uint8, data []byte) error` | Writes one 32-byte block of the element's config zone. |
| `tfw.Device.PulseCapture` | `func (d *Device) PulseCapture(ctx context.Context, pin uint8, startHigh bool, maxEdges uint8, timeout time.Duration) ([]uint16, error)` | Records edge-to-edge intervals and returns them in microseconds. |
| `tfw.Device.PulseIn` | `func (d *Device) PulseIn(ctx context.Context, pin uint8, high bool, timeout time.Duration) (time.Duration, error)` | Measures one pulse of the given level. |
| `tfw.Device.ReadNetConfig` | `func (d *Device) ReadNetConfig(ctx context.Context) (NetConfig, error)` | Reads the network block back with EEPROM_READ, in EEPROMChunk pieces, and decodes it. |
| `tfw.Device.Reset` | `func (d *Device) Reset(ctx context.Context, mode ResetMode) error` | Asks the device to restart. |
| `tfw.Device.SPITransfer` | `func (d *Device) SPITransfer(ctx context.Context, cs uint8, write []byte) ([]byte, error)` | Clocks len(write) bytes out and as many in, chip-select asserted throughout. |
| `tfw.Device.SetADCReference` | `func (d *Device) SetADCReference(ctx context.Context, ref ADCReference) error` | Selects the ADC reference. |
| `tfw.Device.SetI2CClock` | `func (d *Device) SetI2CClock(ctx context.Context, hz uint32) error` | Reconfigures the generic I2C bus's clock. |
| `tfw.Device.SetPinMode` | `func (d *Device) SetPinMode(ctx context.Context, pin uint8, mode PinMode) error` | Configures a pin. |
| `tfw.Device.StageImage` | `func (d *Device) StageImage(ctx context.Context, image []byte) error` | Runs a whole session: BEGIN, the image in OTAChunk pieces, END. |
| `tfw.Device.SysInfo` | `func (d *Device) SysInfo(ctx context.Context) (*SysInfo, error)` | Reads the device's runtime state. |
| `tfw.Device.TimerCancel` | `func (d *Device) TimerCancel(ctx context.Context, pin uint8, applyFinal bool) error` | Stops a pin's timer. |
| `tfw.Device.TimerQuery` | `func (d *Device) TimerQuery(ctx context.Context, pin uint8) (*TimerState, error)` | Reports a pin's timer. |
| `tfw.Device.TimerSet` | `func (d *Device) TimerSet(ctx context.Context, pin uint8, high bool, dur time.Duration) (uint8, error)` | Drives a pin to a level and has the DEVICE drive it back after dur. |
| `tfw.Device.Tone` | `func (d *Device) Tone(ctx context.Context, pin uint8, hz uint16) error` | Drives a square wave on a pin. |
| `tfw.Device.WriteNetConfig` | `func (d *Device) WriteNetConfig(ctx context.Context, c NetConfig) error` | Writes the block in EEPROMChunk-sized EEPROM_WRITEs. |
| `tfw.ElementInfo` | `type ElementInfo struct` | What a provisioning jig can learn about the part before it writes anything. |
| `tfw.EvidenceError` | `type EvidenceError struct` | A certificate or claims set that is not the shape the device writes. |
| `tfw.EvidenceError.Error` | `func (e *EvidenceError) Error() string` | Implements error. |
| `tfw.EvidenceError.Is` | `func (e *EvidenceError) Is(target error) bool` | Reports whether target is ErrBadFormat. |
| `tfw.Frame` | `type Frame struct` | One decoded protocol frame. |
| `tfw.Frame.Cmd` | `func (f Frame) Cmd() Cmd` | The command this frame relates to, with the response bit cleared. |
| `tfw.Frame.IsEvent` | `func (f Frame) IsEvent() bool` | Reports whether this is an unsolicited event. |
| `tfw.Frame.IsResponse` | `func (f Frame) IsResponse() bool` | Reports whether Op carries the response bit. |
| `tfw.FrameError` | `type FrameError struct` | Bytes that cannot be a frame at all: a wrong start byte, an impossible length, a response with no room for its status. |
| `tfw.FrameError.Error` | `func (e *FrameError) Error() string` | Implements error. |
| `tfw.FrameError.Is` | `func (e *FrameError) Is(target error) bool` | Reports whether target is ErrBadFormat. |
| `tfw.HSMState` | `type HSMState struct` | The engine's state after a command: [flags:1][selected:1][refused:4]. |
| `tfw.LinkError` | `type LinkError struct` | A failure of the byte stream underneath the protocol, wrapping whatever the transport said. |
| `tfw.LinkError.Error` | `func (e *LinkError) Error() string` | Implements error. |
| `tfw.LinkError.Unwrap` | `func (e *LinkError) Unwrap() error` | Returns the underlying error, so errors.Is and errors.As see through it. |
| `tfw.Listener` | `type Listener struct` | Accepts the TCP connections Ethernet devices open -- issue #55. |
| `tfw.Listener.Accept` | `func (l *Listener) Accept() (*TCPDevice, error)` | Waits for the next device to connect. |
| `tfw.Listener.Addr` | `func (l *Listener) Addr() net.Addr` | The address the listener is bound to: the endpoint in the device's network block. |
| `tfw.Listener.Close` | `func (l *Listener) Close() error` | Stops accepting. |
| `tfw.NetConfig` | `type NetConfig struct` | The device's network block: its own address, and the endpoint it connects to. |
| `tfw.NetConfig.MarshalBinary` | `func (c NetConfig) MarshalBinary() ([]byte, error)` | Serialises the block: magic, version, the fields, and the frame CRC over everything before it. |
| `tfw.NetConfig.UnmarshalBinary` | `func (c *NetConfig) UnmarshalBinary(b []byte) error` | Decodes a 32-byte network block. |
| `tfw.NetConfigError` | `type NetConfigError struct` | Names the NetConfig field MarshalBinary cannot encode. |
| `tfw.NetConfigError.Error` | `func (e *NetConfigError) Error() string` | Implements error. |
| `tfw.OTAState` | `type OTAState uint8` | OTA_STATUS's state byte. |
| `tfw.OTAIdle`, `tfw.OTAReceiving`, `tfw.OTAComplete`, `tfw.OTAFailed` | `const OTAState` 0…3 | The OTA_STATUS states. |
| `tfw.OTAState.String` | `func (s OTAState) String() string` | Names the state, or prints an unrecognised one as OTAState(n). |
| `tfw.Option` | `type Option func(*Device)` | Configures a Device. |
| `tfw.PWMSupport` | `type PWMSupport uint8` | What became of a pin's output-compare unit: THREE states rather than two. |
| `tfw.PWMNone`, `tfw.PWMAvailable`, `tfw.PWMSystemTick` | `const PWMSupport` 0, 1, 2 | No output-compare unit; one this build drives; one whose timer is the millisecond tick. |
| `tfw.PWMSupport.String` | `func (p PWMSupport) String() string` | Names the PWM state, or says it is unrecognised with its number. |
| `tfw.PayloadSizeError` | `type PayloadSizeError struct` | A payload the framing cannot express. |
| `tfw.PayloadSizeError.Error` | `func (e *PayloadSizeError) Error() string` | Implements error. |
| `tfw.PayloadSizeError.Is` | `func (e *PayloadSizeError) Is(target error) bool` | Reports whether target is ErrPayloadTooLarge. |
| `tfw.PinFlags` | `type PinFlags uint8` | One pin's capability bits. |
| `tfw.PinDigital`, `tfw.PinPWM`, `tfw.PinADC`, `tfw.PinReserved`, `tfw.PinInUse`, `tfw.PinLED` | `const PinFlags` 0x01, 0x02, 0x04, 0x20, 0x40, 0x80 | Has a port; has an output-compare unit; has an analog channel; /RESET or a software peripheral's; taken by this build; the onboard LED. |
| `tfw.PinFlags.Has` | `func (f PinFlags) Has(want PinFlags) bool` | Reports whether every flag in want is set. |
| `tfw.PinFlags.String` | `func (f PinFlags) String() string` | Names each flag set, unnamed bits as a number; "(none)" when no flag is set. |
| `tfw.PinInfo` | `type PinInfo struct` | One pin's descriptor, as CAPS reports it a page at a time. |
| `tfw.PinMode` | `type PinMode uint8` | Configures a pin's direction. |
| `tfw.PinInput`, `tfw.PinOutput`, `tfw.PinInputPullup` | `const PinMode` 0, 1, 2 | A plain input, pull-up off; drives the pin; an input with the internal pull-up enabled. |
| `tfw.ProtocolVersionError` | `type ProtocolVersionError struct` | A device whose protocol MAJOR differs from this host's: nothing beyond CAPS can be trusted to mean what it appears to. |
| `tfw.ProtocolVersionError.Error` | `func (e *ProtocolVersionError) Error() string` | Implements error. |
| `tfw.ResetCause` | `type ResetCause uint8` | Why the part last restarted -- the difference between a commanded reboot and a device that is failing. |
| `tfw.ResetUnknown`, `tfw.ResetPowerOn`, `tfw.ResetExternal`, `tfw.ResetBrownout`, `tfw.ResetWatchdog`, `tfw.ResetSoftware`, `tfw.ResetPanic` | `const ResetCause` 0…6 | The reset causes SYS_INFO reports. |
| `tfw.ResetCause.String` | `func (r ResetCause) String() string` | Names the cause; ResetUnknown and any unrecognised value are "unknown". |
| `tfw.ResetMode` | `type ResetMode uint8` | Selects what a reset lands in. |
| `tfw.ResetSoft`, `tfw.ResetBootloader` | `const ResetMode` 0, 1 | Restarts the application firmware; restarts into tfw-boot's OTA session, for updating a healthy device. |
| `tfw.SPIClockDiv` | `type SPIClockDiv uint8` | The raw two-bit divider field written into SPCR's SPR1:SPR0 -- a code, not a divisor. |
| `tfw.SPIClockDiv4`, `tfw.SPIClockDiv16`, `tfw.SPIClockDiv64`, `tfw.SPIClockDiv128` | `const SPIClockDiv` 0…3 | Fastest to slowest; SPIClockDiv128 is what tfw_bus_init() sets at boot. |
| `tfw.SPIMode` | `type SPIMode uint8` | SPI's clock polarity and phase, numbered the way every SPI datasheet does: 0 through 3. |
| `tfw.SPIMode0`, `tfw.SPIMode1`, `tfw.SPIMode2`, `tfw.SPIMode3` | `const SPIMode` 0…3 | The four SPI modes, numbered as the datasheet numbers them. |
| `tfw.Seq` | `type Seq uint8` | Correlates a request with its response. |
| `tfw.ShortReplyError` | `type ShortReplyError struct` | A well-formed frame whose payload is smaller than the layout its command defines. |
| `tfw.ShortReplyError.Error` | `func (e *ShortReplyError) Error() string` | Implements error. |
| `tfw.SlotInfo` | `type SlotInfo struct` | OTA_INFO's reply: [slots:1][active:1][flags:1][floor:4][state:1][image_version:4][measurement:32]. |
| `tfw.Status` | `type Status uint8` | Mirrors tfw_status_t in include/tfw/status.h. |
| `tfw.StatusOK`, `tfw.StatusErrCRC`, `tfw.StatusErrFraming`, `tfw.StatusErrTooLong`, `tfw.StatusErrUnknownCmd`, `tfw.StatusErrBadArgc`, `tfw.StatusErrBadPin`, `tfw.StatusErrUnsupported`, `tfw.StatusErrBusy`, `tfw.StatusErrTimeout`, `tfw.StatusErrNACK`, `tfw.StatusErrRange`, `tfw.StatusErrNoSlot`, `tfw.StatusErrReservedPin`, `tfw.StatusErrCrypto`, `tfw.StatusErrNotProvisioned`, `tfw.StatusErrProtectedRegion`, `tfw.StatusErrAuthRequired`, `tfw.StatusErrInternal`, `tfw.StatusErrBadState`, `tfw.StatusErrNoStore` | `const Status` 0…20 | The status codes, numbered as include/tfw/status.h numbers them; they extend and never renumber. |
| `tfw.Status.AsError` | `func (s Status) AsError() error` | Returns nil for StatusOK and the status itself otherwise. |
| `tfw.Status.Error` | `func (s Status) Error() string` | Lets a Status be returned directly as an error. |
| `tfw.Status.String` | `func (s Status) String() string` | Names the status, or says it is unrecognised with its number. |
| `tfw.SysInfo` | `type SysInfo struct` | The device's runtime state. |
| `tfw.TCPDevice` | `type TCPDevice struct` | A Device on an accepted connection. |
| `tfw.TCPDevice.Close` | `func (d *TCPDevice) Close() error` | Drops the connection. |
| `tfw.TCPDevice.RemoteAddr` | `func (d *TCPDevice) RemoteAddr() net.Addr` | Where the connection came from. |
| `tfw.TimeoutError` | `type TimeoutError struct` | A command that went unanswered. |
| `tfw.TimeoutError.Error` | `func (e *TimeoutError) Error() string` | Implements error. |
| `tfw.TimeoutError.Is` | `func (e *TimeoutError) Is(target error) bool` | Reports whether target is ErrTimeout. |
| `tfw.TimerState` | `type TimerState struct` | What a pin's timer is doing. |

### `serialport` (`sdk/go/serialport`)

| Symbol | Signature | Contract |
|---|---|---|
| `serialport.DefaultBaud` | `const DefaultBaud = 115200` | Matches TFW_UART_BAUD in the firmware's config.h. |
| `serialport.List` | `func List() ([]string, error)` | Returns the serial ports the system can see. |
| `serialport.Open` | `func Open(path string, baud int) (io.ReadWriteCloser, error)` | Opens a serial port in 8N1 at the given baud. |

### `tfwsim` (`sdk/go/tfwsim`, build tag `tfwsim`)

| Symbol | Signature | Contract |
|---|---|---|
| `tfwsim.DirEnv` | `const DirEnv = simproc.DirEnv` | The environment variable that points at the directory holding the built simulators. |
| `tfwsim.TestImage` | `const TestImage = "tfwsim-test-firmware-image-v1"` | The stand-in for the application image tfw-boot measured. |
| `tfwsim.TestFWHash` | `var TestFWHash = sha256.Sum256([]byte(TestImage))` | The FWHash the element-tier test identity reports in the evidence frame and the alias certificate. |
| `tfwsim.TestSerial` | `var TestSerial [16]byte` | The DeviceID of the element-tier test identity: 16 bytes, "tfwsim-testid-01". |
| `tfwsim.Start` | `func Start(tb testing.TB, opts ...tfw.Option) *tfw.Device` | Runs the software-tier simulator and returns a Device speaking to it. |
| `tfwsim.StartBlankElement` | `func StartBlankElement(tb testing.TB, opts ...tfw.Option) *tfw.Device` | Runs the element-tier simulator with nothing provisioned. |
| `tfwsim.StartElement` | `func StartElement(tb testing.TB, opts ...tfw.Option) *tfw.Device` | Runs the element-tier simulator provisioned with the test identity. |
| `tfwsim.StartElementApp` | `func StartElementApp(tb testing.TB, opts ...tfw.Option) *tfw.Device` | The element tier with test/app/example linked, provisioned as StartElement is. |
| `tfwsim.StartElementEthernet` | `func StartElementEthernet(tb testing.TB, opts ...tfw.Option) (serial *tfw.Device, eth *tfw.TCPDevice)` | The element tier with the test identity as an Ethernet device -- issue #55. |
| `tfwsim.StartElementVia` | `func StartElementVia(tb testing.TB, wrap func(io.ReadWriter) io.ReadWriter, opts ...tfw.Option) *tfw.Device` | StartElement with the connection passed through wrap before the Device sees it. |
| `tfwsim.StartElementWith` | `func StartElementWith(tb testing.TB, args []string, opts ...tfw.Option) *tfw.Device` | The element tier with exactly the given command-line arguments, for a test that supplies its own identity. |
| `tfwsim.StartElementWithOperator` | `func StartElementWithOperator(tb testing.TB, operator *ecdsa.PublicKey, opts ...tfw.Option) *tfw.Device` | StartElement with operator baked into the platform block as the auth session's trust anchor. |
| `tfwsim.StartProvisioningWith` | `func StartProvisioningWith(tb testing.TB, args []string, opts ...tfw.Option) *tfw.Device` | The element tier as the provisioning jig image, tfwsim-prov, with exactly the given command-line arguments. |
| `tfwsim.TestDeviceIDPublicKey` | `func TestDeviceIDPublicKey() *ecdsa.PublicKey` | The public key the element-tier test identity signs with. |
