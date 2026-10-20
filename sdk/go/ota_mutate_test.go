// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"fmt"
	"reflect"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// mutateReply mutates the payload of a one-frame reply and hands each copy
// to parse, which sends one command to a device answering with it. Every
// answer is an error or a value, never a panic. A truncation is refused. An
// append answers an error or the original's value: replies extend by
// appending fields an older host ignores (codec.go, the minor version). A
// single-byte change answers an error or a value other than the original's,
// unless ignored says the changed byte is documented as not read. A payload
// longer than MaxPayload cannot be framed and is passed over.
func mutateReply(t *testing.T, name string, valid []byte, stream uint64,
	parse func(*Device) (any, error), ignored func(m mutation) bool,
) {
	t.Helper()
	orig, err := parse(New(&cannedLink{status: StatusOK, payload: valid}))
	if err != nil {
		t.Fatalf("%s: the valid reply % x is refused: %v", name, valid, err)
	}
	eachMutation(valid, stream, func(m mutation) {
		if len(m.b) > MaxPayload {
			return
		}
		var v any
		var err error
		noPanic(t, m, func() { v, err = parse(New(&cannedLink{status: StatusOK, payload: m.b})) })
		if err != nil {
			if m.kind == "append" {
				t.Fatalf("%s %v: an appended field is refused: %v", name, m, err)
			}
			return
		}
		same := reflect.DeepEqual(v, orig)
		switch m.kind {
		case "truncate":
			t.Fatalf("%s %v: answered %+v", name, m, v)
		case "append":
			if !same {
				t.Fatalf("%s %v: an appended field moved the answer to %+v", name, m, v)
			}
		case "byte", "flip":
			if same != ignored(m) {
				t.Fatalf("%s %v: answered %+v (original %+v)", name, m, v, orig)
			}
		}
	})
}

// framePayload is the payload of a pinned response frame:
// [0xA5][len][seq][cmd|0x80][status] payload [crc lo][crc hi].
func framePayload(t *testing.T, frame string) []byte {
	t.Helper()
	b := []byte(frame)
	if len(b) < 7 || b[0] != 0xa5 || int(b[1]) != len(b)-4 {
		t.Fatalf("% x is not a response frame", b)
	}
	return b[5 : len(b)-2]
}

func readNothing(mutation) bool { return false }

// TestOTAInfoMutations: the pinned OTA_INFO reply, every field of which is
// read.
func TestOTAInfoMutations(t *testing.T) {
	valid := framePayload(t, vectors.FrameOTAInfoResp)
	if len(valid) != 44 {
		t.Fatalf("OTA_INFO payload is %d bytes, want 44", len(valid))
	}
	mutateReply(t, "OTA_INFO", valid, 500, func(d *Device) (any, error) {
		return d.OTAInfo(context.Background(), 1)
	}, readNothing)
}

// TestOTAStatusMutations: an OTA_STATUS reply, [state][received:4 LE] --
// receiving, 19456 bytes, the 76-page image of test-matrix.md -- every byte
// of which is read.
func TestOTAStatusMutations(t *testing.T) {
	type status struct {
		State    OTAState
		Received uint32
	}
	mutateReply(t, "OTA_STATUS", []byte{0x01, 0x00, 0x4c, 0x00, 0x00}, 501, func(d *Device) (any, error) {
		s, n, err := d.OTAStatus(context.Background())
		return status{s, n}, err
	}, readNothing)
}

// TestOTAStateEveryValue: the four states the protocol defines read as their
// names; every other value reads as unrecognised with its own number, never
// as a defined state.
func TestOTAStateEveryValue(t *testing.T) {
	known := map[uint8]string{0: "idle", 1: "receiving", 2: "complete", 3: "failed"}
	for v := range 256 {
		want, ok := known[uint8(v)]
		if !ok {
			want = fmt.Sprintf("OTAState(%d)", v)
		}
		if got := OTAState(v).String(); got != want {
			t.Errorf("state %d: %q, want %q", v, got, want)
		}
	}
}
