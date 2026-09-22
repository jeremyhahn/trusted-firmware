// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"bytes"
	"errors"
	"math/rand"
	"testing"
)

func TestRequestRoundTrips(t *testing.T) {
	payload := []byte{0x0D, 0x01}
	wire, err := EncodeRequest(0x42, CmdDigitalWrite, payload)
	if err != nil {
		t.Fatal(err)
	}
	f, n, err := Decode(wire)
	if err != nil {
		t.Fatal(err)
	}
	if n != len(wire) {
		t.Errorf("consumed %d of %d bytes", n, len(wire))
	}
	if f.Seq != 0x42 || f.Cmd() != CmdDigitalWrite || f.IsResponse() {
		t.Errorf("decoded %+v", f)
	}
	if !bytes.Equal(f.Payload, payload) {
		t.Errorf("payload = %x, want %x", f.Payload, payload)
	}
}

func TestResponseCarriesStatusSeparateFromPayload(t *testing.T) {
	wire, err := EncodeResponse(7, CmdPing, StatusErrBadPin, nil)
	if err != nil {
		t.Fatal(err)
	}
	f, _, err := Decode(wire)
	if err != nil {
		t.Fatal(err)
	}
	if !f.IsResponse() {
		t.Error("response bit not set")
	}
	if f.Status != StatusErrBadPin {
		t.Errorf("status = %v, want %v", f.Status, StatusErrBadPin)
	}
	if len(f.Payload) != 0 {
		t.Errorf("payload = %x, want empty -- the status byte must not leak into it", f.Payload)
	}
}

func TestDecodeRejectsACorruptedFrame(t *testing.T) {
	wire, _ := EncodeRequest(1, CmdPing, []byte{1, 2, 3})
	wire[5] ^= 0x01
	if _, _, err := Decode(wire); !errors.Is(err, ErrBadCRC) {
		t.Fatalf("err = %v, want ErrBadCRC", err)
	}
}

// A sender can be internally consistent and still wrong: this frame's CRC is
// correct but its OP claims a status byte its length leaves no room for.
func TestDecodeRejectsAResponseTooShortForItsStatus(t *testing.T) {
	body := []byte{0x02, 0x01, byte(CmdPing) | opResponseBit}
	crc := CRC16(body)
	wire := append(append([]byte{SOF}, body...), byte(crc), byte(crc>>8))
	if _, _, err := Decode(wire); !errors.Is(err, ErrBadFormat) {
		t.Fatalf("err = %v, want ErrBadFormat", err)
	}
}

func TestDecodeRejectsAnImplausibleLength(t *testing.T) {
	for _, l := range []byte{0, 1, maxLen + 1} {
		wire := []byte{SOF, l, 0, 0, 0, 0}
		if _, _, err := Decode(wire); !errors.Is(err, ErrBadFormat) {
			t.Errorf("length %d: err = %v, want ErrBadFormat", l, err)
		}
	}
}

func TestEncodeRefusesAnOversizePayload(t *testing.T) {
	if _, err := EncodeRequest(1, CmdPing, make([]byte, MaxPayload+1)); !errors.Is(err, ErrPayloadTooLarge) {
		t.Fatalf("err = %v, want ErrPayloadTooLarge", err)
	}
}

// Decode must never panic, never read out of bounds and never report a frame
// that was not there, whatever it is handed. The C framer has the same test.
func TestDecodeSurvivesArbitraryBytes(t *testing.T) {
	r := rand.New(rand.NewSource(20260922))
	buf := make([]byte, 160)
	for i := 0; i < 200000; i++ {
		n := r.Intn(len(buf))
		for j := 0; j < n; j++ {
			buf[j] = byte(r.Intn(256))
		}
		f, consumed, err := Decode(buf[:n])
		if err == nil {
			if consumed <= 0 || consumed > n {
				t.Fatalf("consumed %d of %d", consumed, n)
			}
			if len(f.Payload) > MaxPayload {
				t.Fatalf("payload len %d exceeds the protocol maximum", len(f.Payload))
			}
		}
	}
}
