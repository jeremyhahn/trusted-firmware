# Wire protocol — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

The framed binary protocol between a host and the device: framing, the codec,
dispatch, the command table, status codes, and the golden vectors that hold the
C and Go implementations to the same bytes. Covers `include/tfw/protocol.h`,
`src/proto/`, `src/core/dispatch.c`, `src/core/commands.def`,
`testdata/vectors.h`, `sdk/go/internal/vectors` and the Go codec in `sdk/go`.

## Component boundaries

```
L5  TRANSPORT   src/proto/transport.c   the only layer that touches a link (UART, W5x00)
L4  FRAMING     src/proto/frame.c       pure function of bytes; no I/O, no registers
L3  DISPATCH    src/core/dispatch.c     reader/writer + capability entry points
```

Full layering, including what sits below L3, is in
`../device-capabilities/architecture.md`. What matters here: **framing performs no I/O**
(it runs identically on the device and under `go test` on a host), and
**dispatch never calls the HAL** — a capability module does that, so this
protocol has no opinion about pins, buses or crypto beyond moving bytes to and
from a reader/writer pair.

The Go codec (`sdk/go/codec.go`) is the same layering restated: `Decode` and
`EncodeRequest`/`EncodeResponse` are L4, `Device.Do` in `device.go` is L5.
There is one host implementation; see `../host-sdk/architecture.md`.

## The frame

```
 off  size  field
  0    1    SOF      0xA5
  1    1    LEN      count of body bytes: SEQ, OP, optional STATUS, payload
  2    1    SEQ      request token, echoed verbatim in the response
  3    1    OP       request  CMD        0x00..0x7F
                     response CMD|0x80   0x80..0xFE
                     event    0xFF
  4    1    STATUS   responses and events only; ABSENT from a request
  5+   N    PAYLOAD
  ..   2    CRC16    little-endian, over bytes [1 .. end of body]
```

Total on the wire is `LEN + 4`. Field-level detail, including the LEN encoding
(`2 + payload_len`), is `api.md`.

**CRC-16/CCITT-FALSE**: poly `0x1021`, init `0xFFFF`, no reflection, no final
XOR. Check value (CRC of ASCII `"123456789"`) is `0x29B1`. The CRC covers LEN
through the end of the body — a corrupted length byte is otherwise
undetectable until it has already mis-framed everything after it.

**LEN is bound-checked on write and on read.** On write, `emit()` in
`frame.c` refuses a payload over `TFW_MAX_PAYLOAD` before it ever reaches the
wire. On read, the framer rejects any LEN outside `TFW_LEN_MIN..TFW_MAX_LEN`
before it is used for anything. An unchecked length folds bytes into the next
field: on read it would make the dispatcher read the payload cursor length as
`want - 2 - has_status`, which wraps to a huge value on a too-short LEN and
hands a capability handler a length longer than the buffer it points into.

## Data flow

```
bytes in -> tfw_framer_push (L4) -> TFW_FRAME_READY
         -> tfw_dispatch (L3): table lookup, arity check, handler call
         -> tfw_frame_encode_response (L4) -> bytes out (L5)
```

`tfw_dispatch` checks payload length against the command table's
`min_payload`/`max_payload` **once, for every command**, before the handler
runs — a handler validates values, never arity. After the handler returns,
the dispatcher checks the writer's sticky overflow flag and the reader's
sticky underflow flag once more, so a handler that read past its own
arguments or wrote past its own buffer cannot produce a frame that looks
well-formed.

## Versioning: ids and statuses extend, never renumber

Command ids (`include/tfw/protocol.h`) and status codes (`include/tfw/status.h`)
only ever gain new values. An older host reading an id or status it does not
recognise falls through to its generic failure path — degraded to exactly the
behaviour it had before that value existed, never misclassified as something
else. Renumbering would silently turn one failure into a different one on
every host that has not been rebuilt. `TFW_STATUS_MAX` (0xFE) bounds what the
one-byte STATUS field can ever carry.

A reply's SHAPE extends the same way: new fields are appended, never
inserted, and LEN already says how long the reply is, so nothing on the wire
needs to say it grew. `CAPS` appends the algorithm bitmap after the feature
bitmap; `SYS_INFO` appends the framer's `err_crc`/`err_format` counters after
`reset_cause` (#5). Either way, a host built before the field existed reads
the prefix it always has and is correct, not truncated.

## Resync

The payload is not escaped, so `0xA5` will eventually occur inside one. The
framer (a five-state machine: wait-SOF, wait-LEN, body, CRC-lo, CRC-hi)
recovers by construction rather than by scanning:

1. Outside a frame, every byte that is not `0xA5` is dropped silently.
2. An implausible LEN (below `TFW_LEN_MIN` or above `TFW_MAX_LEN`) means the
   `0xA5` that preceded it was not a real frame start. The framer emits
   `TFW_FRAME_BAD_FORMAT` and returns to hunting — **and reprocesses the
   failing byte as a candidate SOF**. Without that reprocess, a `0xA5`
   payload byte immediately followed by a genuine frame costs two frames
   instead of one, because the real SOF is consumed as the false frame's
   length.
3. A partial frame abandoned after `TFW_FRAME_IDLE_MS` (`tfw_transport_poll`)
   cannot absorb the opening bytes of the next good frame.

The framer retains at most `TFW_MAX_FRAME` bytes and every transition is
bounded, so an arbitrary byte stream can waste CPU but cannot overrun,
allocate or wedge it — asserted over ten million random bytes under
AddressSanitizer (`test/unit/test_frame.c`) and mirrored by `FuzzDecode` in
`sdk/go/fuzz_test.go`.

## Events

`OP = 0xFF` is an unsolicited event. `tfw_transport_send_event` refuses to
queue one — returning 0 — while a response is still draining; the caller
retries on a later poll. That keeps the device a strict request/response
state machine: the host never has to reassemble a reply with an event landing
in the middle of it.

## Trust boundaries

- Framing (L4) trusts nothing: it is a pure function of an untrusted byte
  stream and cannot be driven to allocate or overrun regardless of input.
- Dispatch (L3) trusts the framer's structural check (a response-shaped OP
  with no room for STATUS is rejected before dispatch ever sees it) but
  re-validates every argument itself via the bounded reader/writer.
- The transport (L5) is the only layer that decides a byte came from the
  wire; nothing above it can be reached except through a decoded frame.
- A response frame arriving at a device (`OP` with the response bit set) is
  silently dropped by `handle()` in `transport.c` — it is either a host bug
  or the device's own echo on a shared line, and answering it would make two
  devices on one bus argue forever.

## The golden vectors

`testdata/vectors.h` and `sdk/go/internal/vectors/vectors.go` carry the same
bytes in two languages, because neither language can include the other's.
`make verify-vectors` holds them to each other. They are **literals on
purpose**: a vector computed from the code it pins moves whenever the code
moves, and would agree with a regression as readily as with the contract. **A
failure there is a wire break, not a test to update.**

## Alternatives rejected

- **Firmata.** A generic sysex-framed protocol with no explicit length field
  and no numeric, extensible status channel — recovery from noise means
  scanning for its own sysex markers, and errors are reported ad hoc per
  message type rather than through one status vocabulary that extends
  without renumbering. This protocol needs both properties for the same
  reason it needs static buffers: the target has no room to guess.
- **COBS or SLIP byte-stuffing**, instead of a length prefix. Worst case
  doubles the encoded size, so a 64-byte payload would need 132 bytes
  reserved on a part with 2 KB of SRAM. A length prefix gives a buffer size
  known at compile time, which is what lets every allocation in the firmware
  be static. The cost — `0xA5` can occur in a payload — is what the resync
  state machine exists to absorb.
- **CRC-32**, instead of CRC-16/CCITT-FALSE. A table-driven CRC-32 costs 1 KB
  of PROGMEM, an eighth of an ATtiny85's flash, for no gain at this frame
  length. See `src/proto/crc16.h` for the Hamming-distance argument against
  CRC-8 at the other end of the trade.
- **CBOR, MessagePack or protobuf on the wire.** All three are self-describing
  formats that need a runtime decoder, a length stack or a schema registry to
  parse — cost paid on every frame, on a part that has none of that room and
  where every buffer is sized at compile time from `config.h`. A fixed
  offset table costs nothing to decode; CBOR already exists in this firmware
  (`src/caps/uccs.c`) for the one payload that is inherently self-describing
  by an external spec, and it is confined there rather than adopted for the
  transport.

## What this deliberately does not do

- **No pipelining.** Strict stop-and-wait: the device holds one response
  buffer, and overlapping requests would gain nothing while costing
  correlation state on a part with two kilobytes of RAM.
- **No escaping or byte-stuffing.** The length prefix is the framing
  mechanism; resync is what pays for not stuffing.
- **No CLI or API surface of its own.** The protocol is exercised through
  `tfwctl` and the Go SDK, documented in `../host-sdk/architecture.md`, `cli.md` and
  `api.md` in this folder.
- **No dynamic allocation anywhere in the stack.** Every frame buffer is
  sized from `TFW_MAX_PAYLOAD` at compile time.
