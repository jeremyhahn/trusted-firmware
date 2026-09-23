# Clients

> **Status: NORMATIVE** · Scope: `sdk/go`, `tools/agsim`, `tools/irrigation`
> · Last verified: 2026-09-22

There is **one host implementation**. Not one per tool, not one per consumer:
one codec, one framing implementation, one transport, in `sdk/go`, and every
command is a `main` package in that same module.

The codec necessarily exists exactly twice — in C for the device and in Go for
the host — because Go does not run on an ATmega328P. That is the floor, not
duplication, and the shared golden vectors are what hold the two to the same
wire. Everything above it is single-sourced.

## `sdk/go` — the only host implementation

A **Layer 0 ordinary library**: dependencies are `go.bug.st/serial` and
`stretchr/testify`, and nothing else. `make sdk-verify` fails the build on an
in-house dependency, and that gate runs now rather than at first use, because
the property has to hold *before* a Layer 3 or Layer 5 consumer reaches for it.

The comparison that makes the constraint concrete: `go-xkms/sdk/go` resolves to
237 modules and `go-quicbaas/sdk/go` to 222, each inheriting its parent's AWS,
Azure, GCP, Pebble and MongoDB closures through a `replace`. Neither is a thing
a device client can import. `go-auth` is the model instead — fourteen direct
requires and nothing in-house.

**It holds no private keys.** It transports attestation evidence and public
keys. If that ever changes, `go-enclave` becomes mandatory — the constitution is
absolute that no project hand-rolls locked-memory handling — and the module
stops being Layer 0. Written down so the decision is deliberate rather than
discovered.

### Errors are types

Twenty `fmt.Errorf` call sites became eight error types, grouped by **what a
caller does next** rather than by where they are raised:

| type | recovery |
|---|---|
| `*FrameError` | resynchronise — these bytes are not a frame |
| `*CRCError` | retransmit — it is a frame, and it is damaged |
| `*PayloadSizeError` | chunk; carries the limit, which is a property of the build |
| `*ShortReplyError` | the two ends disagree about a layout — in practice, firmware older than the host |
| `*ProtocolVersionError` | a policy decision, deliberately matching no sentinel |
| `*ArgRangeError` | the argument cannot be expressed on the wire; refused here rather than sent for the device to reject |
| `*TimeoutError` | carries the command, because the deadline that is generous for `ATTEST` is absurd for `PING` |
| `*LinkError` | wraps the transport; the distinction a caller acts on already lives in the wrapped error |

The five published sentinels still work as `errors.Is` targets. `ShortReplyError`
deliberately does **not** match `ErrShortFrame`: the frame arrived whole and its
CRC held, so the link is fine and the disagreement is about layout. A test
asserts that non-match, because collapsing the two would send a caller to
reconnect when it should upgrade.

## `tools/agsim` — the firmware, built for the host

The real firmware sources plus the mock HAL, linked into an executable that
speaks the protocol on stdio.

It is **not a stub and not a client**: it is the device, compiled for x86. The
Go suite runs against it in CI, which is what makes the whole protocol testable
with zero hardware — and, more importantly, what stops the Go codec and the C
codec drifting, because there is no second implementation of the device to
drift from.

## `agctl` — the reference client

Every protocol command, human output and `--json`. If a command exists on the
wire and is not reachable from `agctl`, that is a gap in `agctl`.

## `agprov` — provisioning, with a guard

`--dry-run` prints the plan and touches nothing. `--verify` reads the part back
and compares. `--commit` requires `--yes-i-mean-it`.

The guard is the reason the tool exists rather than a convenience wrapper around
a few writes. An ATECC608A's config and data zones cannot be unlocked — no
erase, no reset, no factory command — so a wrong template bricks the part. See
`docs/security/provisioning.md`.

## `tools/irrigation` — the migration example

Reproduces the old CropDroid behaviour host-side. The VPD and heat-index
arithmetic moves to Go, which removes floating point and the soft-float library
from the MCU; the auto-off timer stays in firmware, because it is the
safety-critical part and must survive the host going away.

## What this deliberately does not do

- **No second parser anywhere.** A tool that needs to read a frame imports the
  SDK. A wrapper package around it would be a second place for the wire format
  to be described, which is a second place for it to be wrong.
- **No transport abstraction with one implementation.** Serial is the transport.
  When Ethernet lands it will be a second `Link`, not a plugin system designed
  in advance for it.
- **No key handling.** See above; it is what keeps the module at Layer 0.
