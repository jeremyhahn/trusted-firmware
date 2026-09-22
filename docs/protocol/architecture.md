# The wire protocol

> **Status: NORMATIVE** · Scope: `include/ag/protocol.h`, `src/proto/`,
> `sdk/go` · Last verified: 2026-09-22
>
> Conflicts with this document are defects. Fix them here first.

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

Total on the wire is `LEN + 4`.

## Decisions, and what they cost

**Length prefix, not byte stuffing.** COBS or SLIP would make the worst-case
encoded size twice the payload, so a 64-byte payload would have to reserve 132
bytes on a part with 2 KB of SRAM. A length prefix gives a buffer whose size is
known at compile time, which is what lets every allocation in the firmware be
static.

The cost is that `0xA5` occurs inside payloads. Resync handles it (below).

**The CRC covers LEN.** A corrupted length byte is otherwise undetectable until
it has already mis-framed everything after it, and the damage surfaces as a
cascade of unrelated failures rather than one bad frame.

**CRC-16/CCITT-FALSE**, poly `0x1021`, init `0xFFFF`. Not CRC-32: a table costs
1 KB of PROGMEM, an eighth of an ATtiny85's flash, for no gain at this frame
length. Not CRC-8: it falls to Hamming distance 2 above about fifteen bytes and
frames reach 132. This polynomial holds HD=4 there, so every one-, two- and
three-bit error is caught, as is every odd-weight error and every burst up to
sixteen bits.

**Little-endian**, and the codec never memcpy's a struct — every multi-byte
field is assembled by shift and mask. AVR is little-endian and so is every
plausible host, so nobody pays a byte swap, and the explicit form keeps the
logic free of any endianness assumption at all.

**Strict request/response.** The device holds one response buffer. Overlapping
requests would gain nothing and would cost correlation state on a part with two
kilobytes of RAM.

## Resync

The payload is not escaped, so `0xA5` will eventually appear inside one. The
framer recovers by construction rather than by scanning:

1. Outside a frame, every byte that is not `0xA5` is discarded silently.
2. A `LEN` below 2 or above the build's maximum is implausible, so the `0xA5`
   before it was not a frame start. The framer reports a format error, returns
   to hunting — **and reprocesses the failing byte as a candidate SOF**.

That last step is the one that matters. Without it, a `0xA5` payload byte
followed immediately by a genuine frame costs *two* frames instead of one,
because the real SOF is consumed as the false frame's length.

3. A partial frame that stops arriving is abandoned after `AG_FRAME_IDLE_MS`,
   so it cannot absorb the opening bytes of the next good frame.

The framer retains at most one frame's worth of bytes and every transition is
bounded, so an arbitrary byte stream can waste CPU but cannot overrun, allocate
or wedge it. Asserted over ten million random bytes under AddressSanitizer.

## Structural validation

After the CRC passes, one further check: a frame whose `OP` claims a `STATUS`
byte that its `LEN` leaves no room for is rejected as malformed.

The CRC does not catch this, because a sender can be internally consistent and
still wrong. Without the check, the payload length computes as `LEN - 3` on a
`LEN` of 2, wraps to 255, and hands the dispatcher a length longer than the
buffer.

## Versioning

**Command ids and status codes extend. They never renumber.**

A host built before a value existed reads it as unrecognised and falls through
to its generic failure path — degraded to exactly the behaviour it had before,
never a misclassification. Renumbering instead silently turns one failure into a
different one on every host that has not been rebuilt.

`CAPS` reports a major and a minor version. Major must match; minor is
tolerated in both directions, which is what lets a fleet run mixed firmware
without a flag day.

## Discovery

A host issues `CAPS` once on connect and caches the answer. Everything else
follows from it: which pins do PWM, which have an ADC channel and on which
channel, which are reserved, how large a payload the device accepts, and what
root of trust it has.

**The feature bitmap reports what was compiled in, not what the MCU could do.**
Two boards with the same part can ship different capability sets, so a host that
inferred capability from the MCU id would be wrong on the second one.

## Events

`OP = 0xFF` is an unsolicited event. The device emits one **only when no
response is in flight**; events arriving during a request are queued and flushed
afterwards. That keeps the host a strict request/response state machine rather
than a reassembler that must cope with an event landing mid-reply.

A boot event means the device restarted underneath the host, which invalidates
everything the host believed about its state.

## The vectors

`testdata/vectors.h` and `sdk/go/internal/vectors/vectors.go` carry the same
bytes in two languages, because neither can include the other's.
`make verify-vectors` holds them to each other.

They are literals on purpose. A vector computed from the code it pins moves
whenever the code moves, and would agree with a regression as readily as with
the contract. **A failure there is a wire break, not a test to update.**
