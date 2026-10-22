// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"reflect"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// frameVectors are the pinned frames, requests and responses both.
var frameVectors = map[string]string{
	"I2C_CONFIG req":         vectors.FrameI2CConfigReq,
	"SPI_CONFIG req":         vectors.FrameSPIConfigReq,
	"OTA_BEGIN bad state":    vectors.FrameOTABeginBadStateResp,
	"OTA_BEGIN no store":     vectors.FrameOTABeginNoStoreResp,
	"OTA_INFO req":           vectors.FrameOTAInfoReq,
	"OTA_INFO resp":          vectors.FrameOTAInfoResp,
	"OTA_ACTIVATE req":       vectors.FrameOTAActivateReq,
	"OTA_ACTIVATE resp":      vectors.FrameOTAActivateResp,
	"OTA_ACTIVATE bad state": vectors.FrameOTAActivateBadStateResp,
	"OTA_CONFIRM req":        vectors.FrameOTAConfirmReq,
	"OTA_CONFIRM resp":       vectors.FrameOTAConfirmResp,
	"HSM_HEARTBEAT req":      vectors.FrameHSMHeartbeatReq,
	"HSM_HEARTBEAT resp":     vectors.FrameHSMHeartbeatResp,
	"HSM_REPORT req":         vectors.FrameHSMReportReq,
	"HSM_REPORT resp":        vectors.FrameHSMReportResp,
	"HSM_REPORT range":       vectors.FrameHSMReportRangeResp,
	"HSM_VERDICT req":        vectors.FrameHSMVerdictReq,
	"HSM_SELECT req":         vectors.FrameHSMSelectReq,
	"HSM_SELECT resp":        vectors.FrameHSMSelectResp,
	"HSM_LOAD req":           vectors.FrameHSMLoadReq,
	"HSM_LOAD range":         vectors.FrameHSMLoadRangeResp,
}

// isFrameErr reports whether err is one of the errors Decode documents.
func isFrameErr(err error) bool {
	var fe *FrameError
	var ce *CRCError
	return errors.Is(err, ErrShortFrame) || errors.As(err, &fe) || errors.As(err, &ce)
}

// TestDecodeMutations: every single-byte change, flip and truncation of a
// pinned frame is refused (the CRC-16 catches every single-byte change);
// a frame with bytes appended decodes to the original and consumes exactly
// its own length, the rest being the next frame's; nothing panics.
func TestDecodeMutations(t *testing.T) {
	for name, s := range frameVectors {
		valid := []byte(s)
		orig, n, err := Decode(valid)
		if err != nil || n != len(valid) {
			t.Fatalf("%s: the pinned frame decodes to %d bytes, %v", name, n, err)
		}
		eachMutation(valid, 700+uint64(valid[3]), func(m mutation) {
			var f Frame
			var n int
			var err error
			noPanic(t, m, func() { f, n, err = Decode(m.b) })
			if err != nil {
				if !isFrameErr(err) {
					t.Fatalf("%s %v: error %v is not one Decode documents", name, m, err)
				}
				if m.kind == "append" {
					t.Fatalf("%s %v: refused: %v", name, m, err)
				}
				return
			}
			switch m.kind {
			case "append":
				if n != len(valid) || !reflect.DeepEqual(f, orig) {
					t.Fatalf("%s %v: consumed %d bytes as %+v", name, m, n, f)
				}
			case "byte", "flip", "truncate":
				t.Fatalf("%s %v: decoded %d bytes as %+v", name, m, n, f)
			}
		})
	}
}

// withCRC returns b with its frame CRC made good, over [len .. body end].
func withCRC(b []byte) []byte {
	b = bytes.Clone(b)
	binary.LittleEndian.PutUint16(b[len(b)-2:], CRC16(b[1:len(b)-2]))
	return b
}

// TestDecodeMutationsPastTheCRC: every value of the seq, op and status bytes
// of each pinned response, its CRC made good, decodes to exactly that seq, op
// and status: an unknown status reads as unrecognised, never as a known one.
func TestDecodeMutationsPastTheCRC(t *testing.T) {
	if CRC16([]byte(vectors.CRCCheckInput)) != vectors.CRCCheckResult {
		t.Fatal("CRC16 does not answer its check vector")
	}
	for name, s := range frameVectors {
		valid := []byte(s)
		if valid[3]&0x80 == 0 {
			continue
		}
		for off := 2; off <= 4; off++ {
			for x := range 256 {
				b := bytes.Clone(valid)
				b[off] = byte(x)
				b = withCRC(b)
				m := mutation{kind: "byte+crc", lo: off, hi: off + 1, b: b}
				var f Frame
				var err error
				noPanic(t, m, func() { f, _, err = Decode(b) })
				if err != nil {
					t.Fatalf("%s %v: %v", name, m, err)
				}
				if byte(f.Seq) != b[2] || f.Op != b[3] {
					t.Fatalf("%s %v: seq %d op %#02x", name, m, f.Seq, f.Op)
				}
				if b[3]&0x80 == 0 {
					// A request or event: no status byte, the whole body is payload.
					if f.Status != StatusOK || !bytes.Equal(f.Payload, b[4:len(b)-2]) {
						t.Fatalf("%s %v: decoded as %+v", name, m, f)
					}
					continue
				}
				if byte(f.Status) != b[4] || !bytes.Equal(f.Payload, b[5:len(b)-2]) {
					t.Fatalf("%s %v: decoded as %+v", name, m, f)
				}
			}
		}
	}
}

// statusNamesWant are the names of the 21 statuses the protocol defines,
// literal here: a renumbering moves one of them under another's name.
var statusNamesWant = []string{
	"ok", "frame CRC mismatch", "framing error", "message too long",
	"unknown command", "wrong argument count", "pin out of range",
	"unsupported on this build or pin", "peripheral busy", "timed out",
	"bus NACK", "value out of range", "no free slot", "pin is reserved",
	"crypto provider failed", "device has no provisioned identity",
	"target is platform-reserved", "command requires an authenticated session",
	"internal error", "command not valid in the current OTA state",
	"external store did not answer",
}

// TestStatusEveryValue: each of the 256 status bytes reads as its own name or
// as unrecognised with its own number, never as another status; a device
// answering it surfaces exactly that status, an error for all but OK, and
// errors.Is matches no other status.
func TestStatusEveryValue(t *testing.T) {
	seen := map[string]int{}
	for v := range 256 {
		s := Status(v)
		want := fmt.Sprintf("unrecognised status %d", v)
		if v < len(statusNamesWant) {
			want = statusNamesWant[v]
		}
		if got := s.String(); got != want {
			t.Errorf("status %d: %q, want %q", v, got, want)
		}
		if prev, dup := seen[s.String()]; dup {
			t.Errorf("statuses %d and %d both read %q", prev, v, s.String())
		}
		seen[s.String()] = v

		_, err := New(&cannedLink{status: s}).Do(context.Background(), CmdPing, nil)
		if v == 0 {
			if err != nil || s.AsError() != nil {
				t.Errorf("status 0: %v, %v", err, s.AsError())
			}
			continue
		}
		var got Status
		if !errors.As(err, &got) || got != s || s.AsError() != s {
			t.Errorf("status %d: Do answered %v", v, err)
			continue
		}
		for k := range len(statusNamesWant) {
			if k != v && errors.Is(err, Status(k)) {
				t.Errorf("status %d: errors.Is matches status %d", v, k)
			}
		}
	}
}

// opLink answers every request with one frame per op, seq echoed and op set
// to that value -- the frames a reply would be if its op byte had changed in
// flight and its CRC still held.
type opLink struct {
	ops []byte
	rx  bytes.Buffer
}

func (l *opLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	for _, op := range l.ops {
		b := []byte{0xa5, 0x04, byte(f.Seq), op, 0x00, 0x2a, 0x00, 0x00}
		l.rx.Write(withCRC(b))
	}
	return len(p), nil
}

func (l *opLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// TestDoTakesOnlyItsOwnReply: a reply whose op is the request's command with
// the response bit set is taken; an event is queued, not taken as the reply.
// A reply naming another command, or a frame without the response bit, is a
// FrameError, never the answer.
func TestDoTakesOnlyItsOwnReply(t *testing.T) {
	ctx := context.Background()
	own := byte(CmdOTAStatus) | 0x80
	f, err := New(&opLink{ops: []byte{own}}).Do(ctx, CmdOTAStatus, nil)
	if err != nil || f.Status != StatusOK || !bytes.Equal(f.Payload, []byte{0x2a}) {
		t.Fatalf("its own reply: %+v, %v", f, err)
	}
	// An event ahead of the reply is queued, never taken as the reply.
	d := New(&opLink{ops: []byte{opEvent, own}})
	f, err = d.Do(ctx, CmdOTAStatus, nil)
	if err != nil || f.Op != own || !bytes.Equal(f.Payload, []byte{0x2a}) {
		t.Fatalf("an event was taken as the reply: %+v, %v", f, err)
	}
	// With the seq right and the CRC good, any other op is refused: a5 04 01
	// ec 00 2a .. is OTA_INFO's reply to an OTA_STATUS request, and a5 04 01
	// 6b 00 2a .. a request frame with no response bit. FuzzReplyParsers
	// feeds payloads under the right op, so it cannot reach these.
	for op := range 0xff {
		if byte(op) == own {
			continue
		}
		f, err := New(&opLink{ops: []byte{byte(op)}}).Do(ctx, CmdOTAStatus, nil)
		var fe *FrameError
		if !errors.As(err, &fe) || !errors.Is(err, ErrBadFormat) {
			t.Fatalf("op %#02x answered %+v, %v; want a FrameError", op, f, err)
		}
	}
}
