# Host SDK — engineering reference

> **Status: NORMATIVE** · Scope: trusted-firmware · SPEC row: `host-sdk` · Last verified: 2026-10-20
> Conflicts with this document are defects. Fix them here first.

Design rationale lives in `architecture.md`. This is the surface: names, as
grepped from `sdk/go`, and what a caller must know to use them correctly.

## The row's requirements on `main`

The `host-sdk` row of [`../SPEC.md`](../SPEC.md), clause by clause, with the
clauses SPEC's "What it owns" and the `ethernet-transport` row give `sdk/go`,
and the test standard (CA m655, the handbook's `integration-testing.md`), as
met or unmet on `main`, with the file that meets each. A clause another
repository owns is named as theirs, not marked unmet here.

| # | Requirement | Source | On `main` | Met by |
|---|---|---|---|---|
| 1 | `sdk/go` is the Layer 0 host implementation: the codec | row | met | `sdk/go/codec.go` (`EncodeRequest`, `EncodeResponse`, `Decode`), `sdk/go/crc.go` (`CRC16`), `sdk/go/cbor.go` (the claims the evidence carries) |
| 2 | … framing | row | met | `sdk/go/codec.go` (`Frame`, `SOF`, resynchronisation past a bad frame); `sdk/go/device.go` (`Device.Do`: one request at a time, `Seq` matched, events queued) |
| 3 | … transport | row | met | `sdk/go/device.go` (`New` over any `io.ReadWriter`); `sdk/go/serialport/` (serial); `sdk/go/tcp.go` (TCP) |
| 4 | … provisioning calls | row | met | `sdk/go/provision.go` (`Prov*`); `sdk/go/platform.go` (`BuildPlatformBlock`); `sdk/go/netcfg.go` (`NetConfig`, `ReadNetConfig`, `WriteNetConfig`) |
| 5 | … attestation calls | row | met | `sdk/go/attest.go` (`Attest`, `AttestCert`); `sdk/go/attestcsr.go` (`AttestCSR`); `sdk/go/appevidence.go` (`AliasUCCS`, `ApplicationEvidence`) |
| 6 | … with zero in-house dependencies | row | met | `sdk/go/go.mod` (testify and `go.bug.st/serial`, nothing in-house); `make sdk-verify` (`mk/go.mk`) fails on one |
| 7 | `sdk/go` holds no private key | What it owns | met | `sdk/go/auth.go` (`AuthResponse` takes a `crypto.Signer`); `sdk/go/provision.go` (`ProvSeedUDS` takes no seed: the element's TRNG fills the slot); `sdk/go/cmd/tfwctl/peripherals.go` (`externalECKeySigner`: `tfwctl auth` signs through `openssl` and never reads the key into the process) |
| 8 | `tfwctl` is a command in that module | row | met | `sdk/go/cmd/tfwctl/` |
| 9 | `tfwprov` is a command in that module | row | met | `sdk/go/cmd/tfwprov/` |
| 10 | `tfwsim` is the real firmware built for the host | row | met | `tools/tfwsim/main.c`; `mk/host.mk` (`tfwsim`, `tfwsim-atecc608a`, `tfwsim-auto`): the firmware's own `src/` over the mock HAL |
| 11 | … and the SDK is tested against it | row | met | `sdk/go/internal/simproc/simproc.go` (the one launcher, `NotBuiltError` when a binary is missing); `sdk/go/tfwsim/tfwsim.go` (the public harness); `sdk/go/cmd/tfwctl/e2e_*_test.go` (each `tfwctl` verb against the C firmware); `make sdk-test` fails, never skips |
| 12 | Consumed by go-trusted-platform at `v0.3.0-alpha` | What it owns | met, this repository's half: the tag `sdk/go/v0.3.0-alpha` is cut; the import is go-trusted-platform's | `ROADMAP.md` (`host-sdk`) |
| 13 | A TCP transport beside serial: the device dials, the host listens | `ethernet-transport` | met | `sdk/go/tcp.go` (`Listen`, `NewListener`, `Listener.Accept`, `TCPDevice`) |
| 14 | 90 % unit statement coverage in every package | CA m655 | met | `make sdk-coverage` (`mk/go.mk`, `go-cover-floor`) fails a package under 90 %, unit tests only, no simulator |
| 15 | Integration through the CLI: every verb on every link the device serves | CA m655; `integration-testing.md` §1, §9 | on open PR #180 (`row/163`): one case table per command family, run on serial and on TCP. On `main` the verbs run on serial, and on TCP only the Ethernet allow-list (`e2e_ethernet_test.go`) | not on `main` |

## Exported surface

Package `tfw`, module `github.com/jeremyhahn/trusted-firmware/sdk/go`.

**Connection**
`Device`, `New(conn io.ReadWriter, opts ...Option) *Device`, `Option`,
`WithTimeout(d time.Duration) Option`, `WithBootWait(d time.Duration) Option`,
`DefaultBootWait` (2s), `(*Device) Do(ctx, cmd, payload) (Frame, error)`,
`(*Device) Events() []Frame`.

**TCP** (issue #55; the device dials, the host listens) —
`Listen(addr string, opts ...Option) (*Listener, error)`,
`NewListener(l net.Listener, opts ...Option) *Listener`, `(*Listener) Accept()
(*TCPDevice, error)`, `(*Listener) Addr()`, `(*Listener) Close()`; `TCPDevice`
embeds `*Device` and adds `Close()` and `RemoteAddr()`. Over Ethernet the device
answers only its allow-list (`docs/ethernet-transport/api.md`).

**Discovery and system** — `Ping`, `Caps`, `PinInfo`, `SysInfo`, `AppInfo`,
`Reset`.

**GPIO and timers** — `SetPinMode`, `DigitalWrite`, `DigitalRead`, `TimerSet`,
`TimerCancel`, `TimerQuery`.

**Analog, PWM, tone, pulse** — `AnalogRead`, `SetADCReference`, `PWMWrite`,
`Tone`, `NoTone`, `PulseIn`, `PulseCapture`.

**EEPROM, buses** — `EEPROMRead`, `EEPROMWrite`, `I2CTransfer`, `SetI2CClock`,
`SPITransfer`, `ConfigureSPI`, `OneWireReset`, `OneWireWrite`, `OneWireRead`.

**Attestation** — `Attest(ctx, nonce []byte) ([]byte, error)`,
`AttestCert(ctx, nonce []byte) ([]byte, error)`,
`AttestCSR(ctx, b [AliasNonceLen]byte) ([CSRSigLen]byte, error)`; the pure
readers `AliasUCCS(der []byte) ([]byte, error)` and
`ApplicationEvidence(uccs []byte) ([ApplicationEvidenceLen]byte, error)`.

**Auth session** (issue #4, element tier only) —
`AuthChallenge(ctx) ([]byte, error)`, `AuthResponse(ctx, signer crypto.Signer,
nonce, serial []byte) error`. `signer` is the standard library's own
`crypto.Signer`: this package never holds or generates the operator private
key — Layer 0 holds no key material, per the repo's `CLAUDE.md` — only accepts
something that can sign. `AuthResponse` produces the wire's raw R‖S from
whatever ASN.1 DER `crypto.Signer` returns, the universal convention for an
`*ecdsa.PrivateKey`.

**Provisioning** (only against a jig image; see `provision.go`) — `ProvInfo`,
`ProvWriteConfig`, `ProvLock`, `ProvGenKey`, `ProvPubKey`, `ProvSeedUDS`; the
pure `BuildPlatformBlock(serial, pubkeyHash, operatorPubkey)
[PlatformBlockLen]byte`.

**Network block** (`docs/ethernet-transport/api.md`) — `ReadNetConfig`,
`WriteNetConfig`, `NetConfig` (`MarshalBinary`, `UnmarshalBinary`), and the pure
`DerivedMAC(id []byte) net.HardwareAddr`.

**OTA** (`docs/ota/api.md`) — `OTABegin`, `OTAData`, `OTAEnd`, `OTAStatus`,
`StageImage` (the whole session), `OTAAuth`, `OTAInfo`, `OTAActivate`,
`OTAConfirm`.

**Codec, pure functions** — `EncodeRequest`, `EncodeResponse`, `Decode`,
`CRC16`, `CRC16Update`.

**Value types** — `Frame`, `Cmd`, `Seq`, `Status`, `Caps`, `PinInfo`, `SysInfo`,
`AppInfo`, `AppState`, `ElementInfo`, `TimerState`, `CryptoTier`,
`AlgorithmSet`, `PinFlags`, `PWMSupport`, `ResetCause`, `ResetMode`, `PinMode`,
`ADCReference`, `SPIMode`, `SPIClockDiv`, `BoardID` (`String`, `Family`),
`NetConfig`, `OTAState`, `SlotInfo`.

**Constants** — `Cmd*` (protocol command ids), `Event*` (unsolicited event
ids), `Feature*` (capability bitmap bits), `Alg*` (algorithm-set bits),
`Status*` (wire status codes), `Board*` (the board id `CAPS` reports, `docs/boards/board-id-registry.md`), `App*`, `OTA*`
(`OTAIdle` … `OTAFailed`, `OTAActivateAtReset`, `OTAActivateResetNow`),
`Slot*` (`SlotDeviceID`, `SlotUDS`; the slot flags `SlotFellBack`,
`SlotRefused`, `SlotTrial`), `SOF`, `SeqNone`, `MaxPayload` (64),
`EEPROMChunk` (8), `EEPROMReservedEnd` (0xC0), `I2CReservedAddr` (0x60),
`I2CClockMin`, `I2CClockMax`, `AttestNonceLen` (16), `AliasNonceLen` (32),
`AuthNonceLen` (16), `EvidenceLen` (164), `CSRSigLen`,
`ApplicationEvidenceLen` (32), `OTAChunk` (60), `OTAInfoLen` (44),
`OTAAuthSigLen`, `NetConfigAddr`, `NetConfigLen`, `ProtoVersionMajor`,
`ProtoVersionMinor`, `ZoneConfig`, `ZoneData`, `PlatformBlockLen`,
`SerialLen`, `PubkeyHashLen`, `OperatorPubkeyLen`.

Package `serialport` — `Open(path string, baud int) (io.ReadWriteCloser, error)`,
`List() ([]string, error)`, `DefaultBaud` (115200).

Package `tfwsim` — the test harness that starts the simulators for a Go test
(`api.md`): `Start`, `StartElement`, `StartBlankElement`, `StartElementWith`,
`StartElementWithOperator`, `StartElementApp`, `StartElementVia`,
`StartElementEthernet`, `TestDeviceIDPublicKey`, `TestSerial`, `TestImage`,
`TestFWHash`, `DirEnv`.

## Invariants

- **Strict request/response.** `Device.Do` never pipelines; the firmware holds
  one response buffer. `Seq` correlates a reply with its request and a reply
  whose `Seq` doesn't match the one just sent is dropped, not returned.
- **Events are queued, not dropped, and not delivered inline.** A boot event
  arriving while `Do` awaits a different reply is appended to `Device.events`;
  the caller drains it with `Events()` after the call returns. The one
  exception is the BOOT event `New` itself waits for (see `WithBootWait`
  below): that one is consumed at connect and never appears in `Events()`, so
  it is never misreported as a reset happening mid-session. A stale BOOT left
  over from a PREVIOUS session never reaches this far at all: `New` purges the
  conn's backlog before it starts waiting (see `WithBootWait` below), so
  there is nothing here for `Do` to misreport as this session's reset in the
  first place.
- **The device owns timer deadlines, not the host.** `TimerSet` arms a
  deadline in firmware; a crashed host does not leave a pin held. Re-arming a
  pin that already has a timer extends it rather than consuming a second slot.
- **`ArgRangeError` is a client-side refusal, not a wire error.** `TimerSet`,
  `EEPROMRead`/`Write`, `ProvWriteConfig`, `ProvLock`, `Attest`, `AttestCert`
  reject an out-of-range argument before it is ever sent — a round trip to
  learn the same thing, or a truncating conversion that sends a *different*
  value the device then accepts, are both worse than a local refusal.
- **`Caps.Algorithms` may be the empty set on older firmware,** and that is
  not an error: the algorithm bitmap was appended after the feature bitmap
  (`capsHeaderLen` 14 → `capsAlgorithmsEnd` 16), so a reply that stops at 14
  bytes is complete by the contract the device was built against
  (`TestCapsAcceptsTheFourteenByteReplyOfOlderFirmware`).
- **`ProtocolVersionError` fires only on a `ProtoMajor` mismatch.** Minor is
  tolerated in both directions — that's what lets a fleet run mixed firmware.
  `Caps` still returns the parsed `*Caps` alongside the error, because what the
  device said about itself is worth logging when a host refuses to talk to it.
- **`AnalogRead` returns the raw ADC count (0..1023), never a calibrated
  value.** Calibration is a property of the sensor and its wiring, which the
  device cannot know, so it belongs to the host program.
- **Status codes and command ids extend; they never renumber.** `Status.String`
  on an unrecognised value returns `"unrecognised status N"` rather than
  erroring — forward compatibility from the old side of a version skew.

## Error semantics

Every failure is a typed value carrying the data a caller needs, grouped by
what to do next (`sdk/go/errors.go`):

| Type | Sentinel (`errors.Is`) | Recovery |
|---|---|---|
| `*FrameError` | `ErrBadFormat` | resynchronise — these bytes are not a frame |
| `*CRCError` | `ErrBadCRC` | retransmit — it's a frame, and it's damaged |
| `*PayloadSizeError` | `ErrPayloadTooLarge` | chunk; carries the limit, a property of the build |
| `*ShortReplyError` | *(none — deliberately)* | the two ends disagree about a layout, in practice older firmware |
| `*ProtocolVersionError` | *(none — deliberately)* | a policy decision; matches no sentinel on purpose |
| `*ArgRangeError` | *(none)* | the argument cannot be expressed on the wire; refused locally |
| `*TimeoutError` | `ErrTimeout` | carries the command; the deadline generous for `Attest` is absurd for `Ping` |
| `*LinkError` | wraps the transport error | reach it with `errors.Is(err, io.EOF)` etc. |
| `*EvidenceError` | `ErrBadFormat` | a certificate or claims set not in the shape the device writes; `AliasUCCS`, `ApplicationEvidence` |
| `*AuthSignError` | wraps the signer's error | the caller's `crypto.Signer` failed, or its output is not an ECDSA signature `AuthResponse` can make R‖S of |
| `*NetConfigError` | *(none)* | a `NetConfig` field `MarshalBinary` cannot encode; refused locally |
| `Status` (not `StatusOK`) | itself: `errors.Is(err, tfw.StatusErrBusy)` | the device answered and refused; `Status.AsError` is how a reply becomes one |

`ErrShortFrame` is a bare sentinel, not a type: on a stream it means "need more
bytes" and `Device.Do`'s read loop tests for exactly it to keep reading.
`ShortReplyError` deliberately does **not** match it — the frame arrived whole
and its CRC held, so the link is fine and the disagreement is about a reply's
shape (`TestShortReplyErrorIsNotAShortFrame`). Collapsing the two would send a
caller to reconnect when it should upgrade its firmware assumptions.

## Configuration

There is no config file and no environment variable. Everything is either a
constructor option or a CLI flag (see `cli.md`):

- `tfw.New(conn, tfw.WithTimeout(d))` — reply timeout, default 10s. Deliberately
  generous: on the element tier `Attest` and `AttestCert` wait out the
  ATECC608A's datasheet maximum signing time, and `AttestCert` also hashes the
  certificate body in software on a 16 MHz part.
- `tfw.New(conn, tfw.WithBootWait(d))` — how long `New` waits for the firmware's
  BOOT event before proceeding anyway, default `DefaultBootWait` (2s); `d <= 0`
  disables the wait entirely. Every classic Arduino resets on DTR, which this
  exists to ride out; raise it for an `tfw-boot` `BOOT_MEASURE=atecc608a` image,
  which can take several seconds to measure a large application before it
  starts. The bound depends on the conn returning from `Read` within it (see
  `serialport.Open` below) exactly as `WithTimeout` already does. Before
  waiting, `New` asks the conn to purge whatever it already has buffered, if
  the conn offers a `ResetInputBuffer() error` method (the same
  optional-interface pattern `SetReadDeadline` already uses) — a BOOT event a
  PREVIOUS session left unread is otherwise indistinguishable from a fresh
  one, and accepting it reintroduces the exact bug this option exists to fix.
  Deliberately a purge, not a discard window: a window sized to catch backlog
  reliably would also be long enough to swallow a genuine event on any board
  that answers fast (the ATtiny85's own BOOT event, no bootloader involved, is
  fully on the wire within single-digit milliseconds), where a purge cannot,
  because it cannot discard a byte that has not arrived yet. This assumes
  `New` runs reasonably promptly after the conn becomes valid for this
  session, which every caller in this repository does; a caller that reads or
  writes the conn itself before handing it to `New` should not expect the
  purge to tell that traffic apart from genuine backlog. If the purge is
  offered and fails, `New` treats that as reason to trust nothing it might
  read and falls through immediately, the same as a disabled wait, rather
  than risk accepting a stale BOOT the failed purge could not remove.
- `serialport.Open(path, baud)` — `baud <= 0` uses `DefaultBaud` (115200);
  opens 8N1 with a 200ms internal read timeout as the floor beneath the SDK's
  own deadline, so a missing device can't hang a read forever. Also purges
  the port's receive buffer once, immediately, so nothing a previous session
  left unread survives into this one -- independent of, and redundant with,
  `Device`'s own purge above, which is what actually reaches a conn that did
  not come from here at all.

## Test strategy

| Make target | What it runs |
|---|---|
| `sdk-build` | `go build ./...` in `sdk/go` |
| `sdk-test` | `go test -count=1 ./...` with the `tfwsim` build tag, the cross-implementation suite; depends on `tfwsim`, `tfwsim-atecc608a`, `tfwsim-auto` and `tfwsim-app-example` |
| `sdk-coverage` | the unit tests alone (no `tfwsim` tag, no simulator), failing if any package is under 90 % (CA m655) |
| `sdk-lint` | `go fmt` (fails if it rewrites anything) + `go vet` |
| `sdk-verify` | `go mod verify` + the Layer 0 dependency grep |
| `verify-vectors` | asserts `internal/vectors` and `testdata/vectors.h` agree |
| `tfwsim` | builds `build/host/tfwsim` from the real firmware + mock HAL |
| `tfwsim-atecc608a` | builds `build/host/tfwsim-atecc608a`: the same, with the real ATECC608A driver over the software element model; `sdk-test` depends on it too |

Unit coverage by file: `codec_test.go` (round trips, corruption, resync,
`TestDecodeSurvivesArbitraryBytes` fuzz-style), `crc_test.go`, `errors_test.go`
(every type's message and sentinel match/non-match), `caps_test.go` (the
14-byte legacy reply, pin paging, protocol-major mismatch), `provision_test.go`,
`attest_test.go`, `fuzz_test.go` (`go test -fuzz`, codec only — no I/O to fuzz
against).

`cmd/tfwctl/e2e_firmware_test.go` holds the cross-implementation scenarios: each
starts `build/host/tfwsim` through `sdk/go/internal/peer` and runs one `tfwctl`
verb against the actual C firmware, refusals included; `make hw-test` runs the
same scenarios against a board on `PORT`. The DICE, AUTH, attest-csr, OTA and
application rows take the same seam (`e2e_peer_test.go`); which board each
needs is tabled in [testing](../development/testing.md#which-rows-run-on-a-board). It **fails** (never skips) if the binary is missing, with a message
to run `make tfwsim` — the one check that the C and Go codecs have not drifted.

`sdk/go/internal/simproc` is the one launcher: it locates the binaries
(`$TFWSIM_DIR`, else this checkout's `build/host`), starts them and returns a
`NotBuiltError` naming the make target when one is missing. The `tfwctl` scenarios and
the public `sdk/go/tfwsim` package both use it. `sdk/go/tfwsim/*_test.go` is the
element-tier suite: DICE evidence over the wire, the identity options (including
an `tfwprov --bake-eeprom` image), and refusal to start on a bad option. It
asserts structure and claims, never a signature — the element model has no curve
arithmetic.

## Observability

None built in. `Device` performs no logging and exposes no metrics; a caller
that wants either wraps the `io.ReadWriter` it hands to `New`, or inspects the
typed errors above. The one piece of built-in signal is `Events()` — an
unsolicited boot event during a call means the device reset underneath the
host, which every caller of `Do` should check for after an exchange completes
(see `tfwctl`'s post-command check in `cli.md`).
