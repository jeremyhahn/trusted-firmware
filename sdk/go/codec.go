// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import "fmt"

// The wire contract. Mirrors include/tfw/protocol.h; the two are pinned to the
// same vectors.
const (
	SOF               = 0xA5
	opResponseBit     = 0x80
	opEvent           = 0xFF
	frameOverhead     = 4 // SOF + LEN + CRC16
	lenMin            = 2 // SEQ + OP
	MaxPayload        = 64
	maxLen            = MaxPayload + 3
	SeqNone       Seq = 0

	// The version of the wire this host speaks, mirroring
	// TFW_PROTO_VERSION_MAJOR and _MINOR. Major changes when a frame cannot be
	// parsed by an older host; minor when fields are appended that an older
	// host can ignore, which is why only the major is ever rejected. 2 since
	// #12 made the CAPS pin page six bytes, which a 1.x host would misread.
	ProtoVersionMajor uint8 = 2
	ProtoVersionMinor uint8 = 0
)

// Seq correlates a request with its response. Zero means the caller wants no
// correlation; the device still echoes it, so a reply against zero is not
// evidence of which request it answered.
type Seq uint8

// Cmd is a command id.
type Cmd uint8

// Frame is one decoded protocol frame.
type Frame struct {
	Seq     Seq
	Op      byte
	Status  Status
	Payload []byte
}

// IsResponse reports whether Op carries the response bit. Events set it too,
// which is why they also carry a status byte.
func (f Frame) IsResponse() bool { return f.Op&opResponseBit != 0 }

// IsEvent reports whether this is an unsolicited event.
func (f Frame) IsEvent() bool { return f.Op == opEvent }

// Cmd returns the command this frame relates to, with the response bit cleared.
func (f Frame) Cmd() Cmd { return Cmd(f.Op &^ opResponseBit) }

// EncodeRequest builds a request frame. Requests carry no status byte.
func EncodeRequest(seq Seq, cmd Cmd, payload []byte) ([]byte, error) {
	return encode(seq, byte(cmd), nil, payload)
}

// EncodeResponse builds a response frame, as a device would. Used by tests and
// by any host-side simulator; keeping one encoder for both directions is what
// stops request and response framing from drifting apart.
func EncodeResponse(seq Seq, cmd Cmd, st Status, payload []byte) ([]byte, error) {
	s := byte(st)
	return encode(seq, byte(cmd)|opResponseBit, &s, payload)
}

func encode(seq Seq, op byte, status *byte, payload []byte) ([]byte, error) {
	if len(payload) > MaxPayload {
		return nil, &PayloadSizeError{Size: len(payload), Limit: MaxPayload}
	}
	n := 2 + len(payload)
	if status != nil {
		n++
	}
	out := make([]byte, 0, n+frameOverhead)
	out = append(out, SOF, byte(n), byte(seq), op)
	if status != nil {
		out = append(out, *status)
	}
	out = append(out, payload...)

	// The CRC spans LEN through the end of the body. Covering LEN is what makes
	// a corrupted length detectable before it has mis-framed whatever follows.
	crc := CRC16(out[1:])
	return append(out, byte(crc), byte(crc>>8)), nil
}

// Decode parses one frame from the front of b, returning it and the number of
// bytes consumed. A caller feeding a stream advances by n and calls again.
func Decode(b []byte) (Frame, int, error) {
	if len(b) < frameOverhead+lenMin {
		return Frame{}, 0, ErrShortFrame
	}
	if b[0] != SOF {
		// A fixed reason, not the byte that was there. This is the resync path
		// -- a host recovering from line noise walks it for every candidate
		// start -- and the byte is by definition the garbage being skipped,
		// which is not worth formatting a string to say.
		return Frame{}, 0, &FrameError{Reason: "does not begin with the start byte"}
	}
	bodyLen := int(b[1])
	if bodyLen < lenMin || bodyLen > maxLen {
		return Frame{}, 0, &FrameError{Reason: fmt.Sprintf("length field is %d, outside %d..%d", bodyLen, lenMin, maxLen)}
	}
	total := bodyLen + frameOverhead
	if len(b) < total {
		return Frame{}, 0, ErrShortFrame
	}
	if got, want := CRC16(b[1:1+bodyLen+1]), uint16(b[total-2])|uint16(b[total-1])<<8; got != want {
		return Frame{}, 0, &CRCError{Computed: got, Claimed: want}
	}

	f := Frame{Seq: Seq(b[2]), Op: b[3]}
	body := b[4 : 2+bodyLen]
	if f.IsResponse() {
		// A frame whose OP claims a status byte its length leaves no room for
		// is malformed. The CRC does not catch this: a sender can be
		// internally consistent and still wrong.
		if bodyLen < 3 {
			return Frame{}, 0, &FrameError{Reason: "response with no room for a status byte"}
		}
		f.Status = Status(body[0])
		f.Payload = body[1:]
	} else {
		f.Payload = body
	}
	return f, total, nil
}
