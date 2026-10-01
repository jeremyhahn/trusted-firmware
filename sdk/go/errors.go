// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"errors"
	"fmt"
	"time"
)

// Every failure this package reports is a TYPE carrying the values that
// produced it.
//
// The distinction that matters to a caller is not the sentence: it is whether
// to retry, resynchronise, refuse the device or fix its own argument. A host
// that had to read "reply is 9 bytes, expected at least 14" to learn the reply
// was truncated would be coupled to wording nobody promised to keep, and would
// still not have the two numbers it needs to log anything useful. So the
// numbers are fields, and the sentence is only how they print.
//
// The sentinels below stay, because they name the CATEGORY a caller usually
// wants and they are already published. Each type reports itself as its
// sentinel through Is, so errors.Is(err, ErrBadCRC) means exactly what it
// always meant while errors.As now reaches the computed and claimed checksums
// behind it.
var (
	// ErrShortFrame says a buffer does not yet hold the frame its length field
	// claims. Deliberately a bare sentinel and not a type: on a stream it is a
	// request for MORE BYTES rather than a failure, and Device.Do's read loop
	// tests for precisely it to decide whether to keep reading. A reply that
	// arrived complete but too short for the command's layout is a different
	// fact and is reported as a ShortReplyError.
	ErrShortFrame = errors.New("aggeneric: truncated frame")
	// ErrBadCRC is the category of a frame whose checksum does not match.
	ErrBadCRC = errors.New("aggeneric: frame CRC mismatch")
	// ErrBadFormat is the category of a structurally impossible frame.
	ErrBadFormat = errors.New("aggeneric: malformed frame")
	// ErrPayloadTooLarge is the category of a payload over MaxPayload.
	ErrPayloadTooLarge = errors.New("aggeneric: payload exceeds the protocol maximum")
	// ErrTimeout is the category of a device that did not answer in time.
	ErrTimeout = errors.New("aggeneric: device did not respond")
)

// Pointer receivers throughout, and every site below returns the composite
// literal directly rather than a variable that could be a nil *T. A nil
// pointer stored in a non-nil error interface is the one way a typed error can
// report failure when nothing failed, and the cheapest defence against it is
// to never have a variable of the concrete type at all.
var (
	_ error = (*FrameError)(nil)
	_ error = (*CRCError)(nil)
	_ error = (*PayloadSizeError)(nil)
	_ error = (*ShortReplyError)(nil)
	_ error = (*ProtocolVersionError)(nil)
	_ error = (*ArgRangeError)(nil)
	_ error = (*TimeoutError)(nil)
	_ error = (*LinkError)(nil)
)

// FrameError reports bytes that cannot be a frame at all: a wrong start byte,
// an impossible length, a response with no room for the status byte its OP
// promises. Reason is prose because there is nothing else to carry -- the
// frame did not parse far enough to yield a number worth keeping -- but the
// type separates "these bytes are not a frame" from "this frame is damaged",
// which are recovered from differently: the first resynchronises, the second
// retransmits.
type FrameError struct {
	Reason string
}

func (e *FrameError) Error() string { return "aggeneric: malformed frame: " + e.Reason }

func (e *FrameError) Is(target error) bool { return target == ErrBadFormat }

// CRCError reports a checksum that disagrees with the bytes it covers, and
// carries both values because they say which side to suspect: a computed
// checksum that is stable across retries points at a sender writing the wrong
// bytes, one that differs every time points at the line.
type CRCError struct {
	Computed, Claimed uint16
}

func (e *CRCError) Error() string {
	return fmt.Sprintf("aggeneric: frame CRC mismatch: computed %04x, frame claims %04x",
		e.Computed, e.Claimed)
}

func (e *CRCError) Is(target error) bool { return target == ErrBadCRC }

// PayloadSizeError reports a payload the framing cannot express. It carries
// the limit alongside the size because the limit is a property of the BUILD --
// a device may report a smaller MaxPayload in CAPS than this host compiles
// with -- so a caller that only saw the size could not tell how far to chunk.
type PayloadSizeError struct {
	Size, Limit int
}

func (e *PayloadSizeError) Error() string {
	return fmt.Sprintf("aggeneric: payload of %d bytes exceeds the protocol maximum of %d",
		e.Size, e.Limit)
}

func (e *PayloadSizeError) Is(target error) bool { return target == ErrPayloadTooLarge }

// ShortReplyError reports a well-formed frame whose payload is smaller than
// the layout its command defines.
//
// Nothing is wrong with the link here -- the frame arrived whole and its CRC
// held -- so this is not ErrShortFrame. It is the two ends disagreeing about a
// reply's shape, which in practice means firmware older than this host, and
// the command plus the two lengths are what turn that into a diagnosis instead
// of a guess.
type ShortReplyError struct {
	Cmd  Cmd
	Got  int
	Need int
}

func (e *ShortReplyError) Error() string {
	return fmt.Sprintf("aggeneric: command 0x%02x reply is %d bytes, needs at least %d",
		byte(e.Cmd), e.Got, e.Need)
}

// ProtocolVersionError reports a device whose protocol MAJOR differs from this
// host's: the frames themselves may not parse the same way, so nothing beyond
// CAPS can be trusted to mean what it appears to mean. Minor is carried but
// never rejected -- tolerating it in both directions is what lets a fleet run
// mixed firmware without a flag day.
type ProtocolVersionError struct {
	DeviceMajor, DeviceMinor uint8
	HostMajor, HostMinor     uint8
}

func (e *ProtocolVersionError) Error() string {
	return fmt.Sprintf("aggeneric: device speaks protocol %d.%d, this host speaks %d.%d",
		e.DeviceMajor, e.DeviceMinor, e.HostMajor, e.HostMinor)
}

// ArgRangeError reports an argument the wire cannot carry, refused HERE rather
// than sent for the device to reject.
//
// The bounds are not taste: a timer duration is a uint32 of milliseconds and
// an EEPROM chunk is what the device can commit without missing a scheduled
// deadline. Sending an out-of-range value would cost a round trip to be told
// the same thing, and a truncating conversion would send a DIFFERENT value
// that the device then accepts -- a pump told to run for four milliseconds
// instead of fifty days is worse than a refusal.
type ArgRangeError struct {
	Cmd      Cmd
	Arg      string // the parameter as this SDK's API names it
	Value    int64
	Min, Max int64
	Unit     string // what Value counts: "ms", "bytes"
}

func (e *ArgRangeError) Error() string {
	return fmt.Sprintf("aggeneric: command 0x%02x: %s is %d %s, outside %d..%d",
		byte(e.Cmd), e.Arg, e.Value, e.Unit, e.Min, e.Max)
}

// TimeoutError reports a command that went unanswered. It carries the command
// because the deadline that is generous for one is not for another: an
// element-tier ATTEST waits on the secure element's signature, while a PING
// that takes that long means the link is gone.
type TimeoutError struct {
	Cmd     Cmd
	Elapsed time.Duration
}

func (e *TimeoutError) Error() string {
	return fmt.Sprintf("aggeneric: command 0x%02x did not respond after %s", byte(e.Cmd), e.Elapsed)
}

func (e *TimeoutError) Is(target error) bool { return target == ErrTimeout }

// LinkError reports a failure of the byte stream underneath the protocol,
// wrapping whatever the transport said.
//
// It adds the direction and otherwise gets out of the way: the distinction a
// caller acts on -- a closed port, which needs a reconnect, versus a transient
// read error, which does not -- lives in the wrapped error and is reached with
// errors.Is(err, io.EOF). Restating it in a field of our own would be a second
// copy of a fact the transport already owns.
type LinkError struct {
	Op  string // "read" or "write"
	Err error
}

func (e *LinkError) Error() string { return fmt.Sprintf("aggeneric: %s: %v", e.Op, e.Err) }

func (e *LinkError) Unwrap() error { return e.Err }
