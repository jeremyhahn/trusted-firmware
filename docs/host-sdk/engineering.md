# Host SDK — engineering reference

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Design rationale lives in `architecture.md`. This is the surface: names, as
grepped from `sdk/go`, and what a caller must know to use them correctly.

## Exported surface

Package `aggeneric`, module `github.com/jeremyhahn/arduino-generic/sdk/go`.

**Connection**
`Device`, `New(conn io.ReadWriter, opts ...Option) *Device`, `Option`,
`WithTimeout(d time.Duration) Option`, `WithBootWait(d time.Duration) Option`,
`DefaultBootWait` (2s), `(*Device) Do(ctx, cmd, payload) (Frame, error)`,
`(*Device) Events() []Frame`.

**Discovery and system** — `Caps`, `PinInfo`, `SysInfo`, `Reset`.

**GPIO and timers** — `SetPinMode`, `DigitalWrite`, `DigitalRead`, `TimerSet`,
`TimerCancel`, `TimerQuery`.

**Analog, PWM, tone, pulse** — `AnalogRead`, `SetADCReference`, `PWMWrite`,
`Tone`, `NoTone`, `PulseIn`, `PulseCapture`.

**EEPROM, buses** — `EEPROMRead`, `EEPROMWrite`, `I2CTransfer`, `SPITransfer`,
`OneWireReset`, `OneWireWrite`, `OneWireRead`.

**Attestation** — `Attest(ctx, nonce []byte) ([]byte, error)`,
`AttestCert(ctx, nonce []byte) ([]byte, error)`.

**Provisioning** (only against a jig image; see `provision.go`) — `ProvInfo`,
`ProvWriteConfig`, `ProvLock`, `ProvGenKey`, `ProvPubKey`, `ProvSeedUDS`.

**Codec, pure functions** — `EncodeRequest`, `EncodeResponse`, `Decode`,
`CRC16`, `CRC16Update`.

**Value types** — `Frame`, `Cmd`, `Seq`, `Status`, `Caps`, `PinInfo`, `SysInfo`,
`ElementInfo`, `TimerState`, `CryptoTier`, `AlgorithmSet`, `PinFlags`,
`PWMSupport`, `ResetCause`, `ResetMode`, `PinMode`, `ADCReference`.

**Constants** — `Cmd*` (protocol command ids), `Event*` (unsolicited event
ids), `Feature*` (capability bitmap bits), `Alg*` (algorithm-set bits),
`Status*` (wire status codes), `SOF`, `MaxPayload` (64), `EEPROMChunk` (8),
`EEPROMReservedEnd` (0x80), `I2CReservedAddr` (0x60), `AttestNonceLen` (16),
`AliasNonceLen` (32), `EvidenceLen` (164), `ProtoVersionMajor`,
`ProtoVersionMinor`, `SlotDeviceID`, `SlotUDS`, `ZoneConfig`, `ZoneData`.

Package `serialport` — `Open(path string, baud int) (io.ReadWriteCloser, error)`,
`List() ([]string, error)`, `DefaultBaud` (115200).

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
  device cannot know; see `sdk/go/cmd/irrigation` for where that meaning lives.
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

`ErrShortFrame` is a bare sentinel, not a type: on a stream it means "need more
bytes" and `Device.Do`'s read loop tests for exactly it to keep reading.
`ShortReplyError` deliberately does **not** match it — the frame arrived whole
and its CRC held, so the link is fine and the disagreement is about a reply's
shape (`TestShortReplyErrorIsNotAShortFrame`). Collapsing the two would send a
caller to reconnect when it should upgrade its firmware assumptions.

## Configuration

There is no config file and no environment variable. Everything is either a
constructor option or a CLI flag (see `cli.md`):

- `ag.New(conn, ag.WithTimeout(d))` — reply timeout, default 10s. Deliberately
  generous: a device deriving an Alias key in software spends seconds inside
  `Attest` on a 16 MHz part.
- `ag.New(conn, ag.WithBootWait(d))` — how long `New` waits for the firmware's
  BOOT event before proceeding anyway, default `DefaultBootWait` (2s); `d <= 0`
  disables the wait entirely. Every classic Arduino resets on DTR, which this
  exists to ride out; raise it for an `ag-boot` `BOOT_MEASURE=atecc608a` image,
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
| `sdk-test` | `go test -count=1 ./...`; depends on `agsim` |
| `sdk-lint` | `go fmt` (fails if it rewrites anything) + `go vet` |
| `sdk-verify` | `go mod verify` + the Layer 0 dependency grep |
| `verify-vectors` | asserts `internal/vectors` and `testdata/vectors.h` agree |
| `agsim` | builds `build/host/agsim` from the real firmware + mock HAL |

Unit coverage by file: `codec_test.go` (round trips, corruption, resync,
`TestDecodeSurvivesArbitraryBytes` fuzz-style), `crc_test.go`, `errors_test.go`
(every type's message and sentinel match/non-match), `caps_test.go` (the
14-byte legacy reply, pin paging, protocol-major mismatch), `provision_test.go`,
`attest_test.go`, `fuzz_test.go` (`go test -fuzz`, codec only — no I/O to fuzz
against).

`agsim_test.go` is the cross-implementation suite: it starts `build/host/agsim`
as a subprocess over pipes and runs the SDK's public API against the actual C
firmware. It **fails** (never skips) if the binary is missing, with a message
to run `make agsim` — the one check that the C and Go codecs have not drifted.

## Observability

None built in. `Device` performs no logging and exposes no metrics; a caller
that wants either wraps the `io.ReadWriter` it hands to `New`, or inspects the
typed errors above. The one piece of built-in signal is `Events()` — an
unsolicited boot event during a call means the device reset underneath the
host, which every caller of `Do` should check for after an exchange completes
(see `agctl`'s post-command check in `cli.md`).
