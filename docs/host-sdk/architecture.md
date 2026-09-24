# Host SDK

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## What this is

**One host implementation.** Not one per tool: one codec, one framing
implementation, one transport, in `sdk/go`, and every command
(`agctl`, `agprov`, `irrigation`) is a `main` package in that same module.
`tools/agsim` is the peer on the other end — the firmware, not a client.

## Component boundaries

| Component | Owns | Depends on |
|---|---|---|
| `sdk/go` (package `aggeneric`) | frame codec, CRC, `Device`, commands, provisioning, attestation, errors | stdlib only |
| `sdk/go/serialport` | opening a physical port | `go.bug.st/serial` |
| `sdk/go/internal/vectors` | published wire vectors, mirrored from `testdata/vectors.h` | stdlib only |
| `sdk/go/templates` | reviewable provisioning templates (data, not code) | — |
| `sdk/go/cmd/agctl` | reference client: every command, human output and `--json` | `aggeneric`, `serialport` |
| `sdk/go/cmd/agprov` | provisioning with a commit guard | `aggeneric`, `serialport` |
| `sdk/go/cmd/irrigation` | migration example (see `docs/migration/`) | `aggeneric`, `serialport` |
| `tools/agsim` | firmware sources + mock HAL, linked for the host | `src/proto`, `src/core`, `src/caps`, `test/mock` |

`serialport` is its own package for one reason: a consumer that only needs the
codec (`go-xkms` verifying evidence, say) links no serial library at all. Only
something that opens a physical device pays for `go.bug.st/serial`.

## Data flow

`Device.Do` is the one path every command goes through: encode a request with
the next `Seq`, write it, then read until a frame with the matching sequence
comes back or the timeout elapses. Frames that arrive out of turn are handled,
not dropped blindly — an event is queued on `Device.events` for `Events()` to
drain, and a reply to an already-timed-out request is discarded, because
returning it would answer the wrong caller. See `sdk/go/device.go:Do`.

The codec (`sdk/go/codec.go`) is pure: `EncodeRequest`/`EncodeResponse`/`Decode`
touch no I/O and are shared by production code and `agsim_test.go`'s
cross-implementation suite. `Decode` resynchronises on the next `SOF` rather
than discarding a whole buffer — a genuine frame may start inside garbage.

## Trust boundaries

- **`sdk/go` holds no private keys.** It transports attestation evidence
  (`Attest`, `AttestCert`) and public keys (`ProvPubKey`, `ProvGenKey`'s
  return value) and never a private one — key generation happens inside the
  element, in `ProvGenKey`, and the private half never crosses the wire. If
  that ever changes, this module needs `go-enclave` — the constitution's rule
  that no project hand-rolls locked-memory handling — and stops being a Layer 0
  module. Written down here so that's a decision, not a discovery.
- **`I2CReservedAddr` (0x60)** is refused by the device, not by this SDK: the
  secure element shares the bus, and a generic I2C transfer that could reach
  it would let a host bypass the firmware's crypto layer entirely.
- **`EEPROMReservedEnd` (0x80)** marks the boundary the device enforces on
  writes: below it lives the bootloader's public-key hash, the serial and the
  boot counter. `EEPROMRead` may read the reserved region — it's a hash and a
  serial, public by nature — `EEPROMWrite` may not.
- **Provisioning commands (`0x70`–`0x75`) exist on the wire permanently and are
  implemented almost nowhere.** A production build carries
  `AG_FEATURE_PROVISION 0`, so its dispatch table has no entry for them and it
  answers "unknown command". Only a provisioning-jig image has the handlers.
  See `docs/device-provisioning/user.md` for the full sequence and why.

## Zero in-house dependencies is a hard constraint

`sdk/go`'s dependency list is `go.bug.st/serial` and `stretchr/testify`, and
nothing else. This is imported by `go-xkms` (Layer 3) and
`go-trusted-platform` (Layer 5) — both sit above it in the dependency graph. A
Layer 0 module that reached upward for an in-house package would invert that
graph and make itself unimportable by the very projects it exists for.

`make sdk-verify` enforces this: it runs `go mod verify` and greps
`go list -m all` for any `jeremyhahn/go-*` or `jeremyhahn/xkey` module. The
gate runs now, in this repository's CI, rather than at the point a Layer 3 or
Layer 5 consumer discovers the violation.

## `agsim` is the firmware, not a stub

`tools/agsim` links the real `src/proto`, `src/core` and `src/caps` sources
against the mock HAL and speaks the protocol on stdio. It is the device,
compiled for the host — not a hand-written fake that would drift the moment
either side changed. `sdk/go/agsim_test.go` runs the SDK against it, which is
what makes the whole protocol testable with zero hardware and, more
importantly, is the only check that the C codec and the Go codec have not
drifted apart. That test **fails**, with an explicit message to run
`make agsim`, if the binary is missing — never a skip, because a skipped
cross-implementation test reports success while checking nothing.

## The codec exists exactly twice

Once in C (`src/proto`) for the device, once in Go (`sdk/go/codec.go`) for the
host, because Go does not run on an 8-bit AVR. That is the floor, not
duplication — everything above the codec is single-sourced in `sdk/go`. The
two are held to the same wire by `sdk/go/internal/vectors/vectors.go` and
`testdata/vectors.h`, published literal byte values that `make verify-vectors`
asserts are identical. A vector computed from the code it pins would move
whenever the code moves and agree with a regression as readily as with the
contract; a failure here is a wire break, not a test to update. See
`docs/wire-protocol/architecture.md`.

## Alternatives rejected

- **Importing any in-house SDK from `sdk/go`.** Would invert the Layer 0
  dependency graph — see above. `sdk-verify` exists specifically to catch this
  by accident, not just by intent.
- **A separate flashing tool.** `avrdude` is the tool; `agprov` and `agctl`
  call it nowhere and reimplement none of it. See `docs/development/flashing.md`.
- **A C `agctl` beside the Go one.** Two host-side parsers to keep in
  agreement for no gain — the C codec is already proven by the firmware itself
  and by `agsim`.
- **A YAML library for provisioning templates.** A dependency added to
  `sdk/go` is a dependency added to every Layer 3/5 consumer. The template
  format is six keys; `sdk/go/cmd/agprov/template.go` parses `key = value` by
  hand rather than pull in a parser for the whole graph.
- **A transport abstraction with one implementation.** Serial is the
  transport. `Device` takes an `io.ReadWriter`, which already admits a pty or
  an `agsim` subprocess pipe; when Ethernet lands it is a second concrete
  implementation, not a plugin system designed in advance for it.

## What this deliberately does not do

- **No second parser anywhere.** A tool that needs to read a frame imports
  `aggeneric`. A wrapper package around it would be a second place for the
  wire format to be described, which is a second place for it to be wrong.
- **No key handling.** See "trust boundaries" above; it is what keeps this
  module at Layer 0.
- **No `delay()`-style blocking inside the codec.** Timeouts are the caller's
  `Option` (`WithTimeout`) and the transport's own deadline; the codec itself
  performs no I/O and cannot block.

## See also

- `docs/wire-protocol/architecture.md` — the wire frame this SDK encodes and decodes
- `docs/device-provisioning/user.md` — the provisioning sequence `agprov` drives
- `docs/development/testing.md` — how `agsim` fits the three test tiers
- `docs/migration/from-cropdroid-irrigation.md` — the irrigation example in full
