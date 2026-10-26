// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"encoding/binary"
	"errors"
	"math/rand"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// The wire frames testdata/vectors.h froze for I2C_CONFIG and SPI_CONFIG
// (issue #21). This is the cross-implementation check the vectors exist for:
// EncodeRequest is a from-scratch Go implementation of the same framing
// tfw_frame_encode_request performs in C, so the two agreeing on these exact
// bytes -- rather than merely on each other's round trip -- is what pins the
// wire rather than a regression.
func TestEncodeRequestMatchesTheFrozenWireFrames(t *testing.T) {
	i2cPayload := make([]byte, 4)
	binary.LittleEndian.PutUint32(i2cPayload, 100_000)
	got, err := EncodeRequest(1, CmdI2CConfig, i2cPayload)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, []byte(vectors.FrameI2CConfigReq)) {
		t.Errorf("I2C_CONFIG frame = % x, want % x", got, vectors.FrameI2CConfigReq)
	}

	got, err = EncodeRequest(2, CmdSPIConfig, []byte{3, 2, 1})
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, []byte(vectors.FrameSPIConfigReq)) {
		t.Errorf("SPI_CONFIG frame = % x, want % x", got, vectors.FrameSPIConfigReq)
	}
}

// TestBadStateResponseMatchesVector pins status 19 on the wire. The literal,
// not the constant, is what a renumber cannot move.
func TestBadStateResponseMatchesVector(t *testing.T) {
	got, err := EncodeResponse(3, Cmd(0x68), StatusErrBadState, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, []byte(vectors.FrameOTABeginBadStateResp)) {
		t.Errorf("OTA_BEGIN BAD_STATE frame = % x, want % x", got, vectors.FrameOTABeginBadStateResp)
	}
	if StatusErrBadState.String() != "command not valid in the current OTA state" {
		t.Errorf("StatusErrBadState = %q", StatusErrBadState.String())
	}
}

// TestNoStoreResponseMatchesVector pins status 20 on the wire (#167): OTA_BEGIN
// refused because no part answered on the store's select.
func TestNoStoreResponseMatchesVector(t *testing.T) {
	got, err := EncodeResponse(3, CmdOTABegin, StatusErrNoStore, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, []byte(vectors.FrameOTABeginNoStoreResp)) {
		t.Errorf("OTA_BEGIN NO_STORE frame = % x, want % x", got, vectors.FrameOTABeginNoStoreResp)
	}
	if StatusErrNoStore.String() != "external store did not answer" {
		t.Errorf("StatusErrNoStore = %q", StatusErrNoStore.String())
	}
}

// ADR decision 8's control commands (#77), each frame against its literal.
// The reserved end and the network block both moved to 0x140 (#125).
func TestTheEEPROMMapMatchesTheVector(t *testing.T) {
	if EEPROMReservedEnd != vectors.EEPROMReservedEnd || NetConfigAddr != vectors.EEPROMReservedEnd {
		t.Fatalf("EEPROMReservedEnd 0x%X, NetConfigAddr 0x%X, want 0x%X", EEPROMReservedEnd, NetConfigAddr, vectors.EEPROMReservedEnd)
	}
}

func TestOTAControlFramesMatchVectors(t *testing.T) {
	info := append([]byte{2, 0, 0x05, 7, 0, 0, 0, 4, 0x2A, 0, 0, 0}, bytes.Repeat([]byte{0xAB}, 32)...)
	enc := func(f func() ([]byte, error)) []byte {
		b, err := f()
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	for _, c := range []struct {
		name string
		got  []byte
		want string
	}{
		{"OTA_INFO req", enc(func() ([]byte, error) { return EncodeRequest(4, CmdOTAInfo, []byte{1}) }), vectors.FrameOTAInfoReq},
		{"OTA_INFO resp", enc(func() ([]byte, error) { return EncodeResponse(4, CmdOTAInfo, StatusOK, info) }), vectors.FrameOTAInfoResp},
		{"OTA_ACTIVATE req", enc(func() ([]byte, error) { return EncodeRequest(5, CmdOTAActivate, []byte{OTAActivateResetNow}) }), vectors.FrameOTAActivateReq},
		{"OTA_ACTIVATE resp", enc(func() ([]byte, error) { return EncodeResponse(5, CmdOTAActivate, StatusOK, nil) }), vectors.FrameOTAActivateResp},
		{"OTA_ACTIVATE BAD_STATE", enc(func() ([]byte, error) { return EncodeResponse(5, CmdOTAActivate, StatusErrBadState, nil) }), vectors.FrameOTAActivateBadStateResp},
		{"OTA_CONFIRM req", enc(func() ([]byte, error) { return EncodeRequest(6, CmdOTAConfirm, nil) }), vectors.FrameOTAConfirmReq},
		{"OTA_CONFIRM resp", enc(func() ([]byte, error) { return EncodeResponse(6, CmdOTAConfirm, StatusOK, nil) }), vectors.FrameOTAConfirmResp},
		{"EEPROM_WRITE old netcfg req", enc(func() ([]byte, error) {
			return EncodeRequest(7, CmdEEPROMWrite, []byte{0xC0, 0x00, 'T', 'F', 'N', 'C'})
		}), vectors.FrameEEPROMWriteOldNetcfgReq},
		{"EEPROM_WRITE protected resp", enc(func() ([]byte, error) { return EncodeResponse(7, CmdEEPROMWrite, StatusErrProtectedRegion, nil) }), vectors.FrameEEPROMWriteProtectedResp},
		{"EEPROM_WRITE netcfg req", enc(func() ([]byte, error) {
			return EncodeRequest(8, CmdEEPROMWrite, []byte{byte(NetConfigAddr & 0xFF), byte(NetConfigAddr >> 8), 'T', 'F', 'N', 'C'})
		}), vectors.FrameEEPROMWriteNetcfgReq},
		{"EEPROM_WRITE netcfg resp", enc(func() ([]byte, error) { return EncodeResponse(8, CmdEEPROMWrite, StatusOK, nil) }), vectors.FrameEEPROMWriteNetcfgResp},
		{"EEPROM_WRITE netcfg auth required", enc(func() ([]byte, error) {
			return EncodeResponse(8, CmdEEPROMWrite, StatusErrAuthRequired, nil)
		}), vectors.FrameEEPROMWriteNetcfgAuthRequiredResp},
		{"OTA_BEGIN req", enc(func() ([]byte, error) { return EncodeRequest(9, CmdOTABegin, []byte{0x00, 0x04, 0x00, 0x00}) }), vectors.FrameOTABeginReq},
		{"OTA_BEGIN over budget resp", enc(func() ([]byte, error) { return EncodeResponse(9, CmdOTABegin, StatusErrBadState, nil) }), vectors.FrameOTABeginOverBudgetResp},
		{"HSM_HEARTBEAT req", enc(func() ([]byte, error) { return EncodeRequest(1, CmdHSMHeartbeat, nil) }), vectors.FrameHSMHeartbeatReq},
		{"HSM_HEARTBEAT resp", enc(func() ([]byte, error) {
			return EncodeResponse(1, CmdHSMHeartbeat, StatusOK, []byte{0x01, 0x00, 0x02, 0x01, 0x00, 0x00})
		}), vectors.FrameHSMHeartbeatResp},
		{"HSM_REPORT req", enc(func() ([]byte, error) { return EncodeRequest(2, CmdHSMReport, []byte{0x04}) }), vectors.FrameHSMReportReq},
		{"HSM_REPORT resp", enc(func() ([]byte, error) {
			return EncodeResponse(2, CmdHSMReport, StatusOK, []byte{0x03, 0x00, 0x00, 0x00, 0x00, 0x00})
		}), vectors.FrameHSMReportResp},
		{"HSM_REPORT RANGE", enc(func() ([]byte, error) { return EncodeResponse(2, CmdHSMReport, StatusErrRange, nil) }), vectors.FrameHSMReportRangeResp},
		{"HSM_VERDICT req", enc(func() ([]byte, error) { return EncodeRequest(3, CmdHSMVerdict, []byte{0x01}) }), vectors.FrameHSMVerdictReq},
		{"HSM_SELECT req", enc(func() ([]byte, error) { return EncodeRequest(4, CmdHSMSelect, []byte{0x00}) }), vectors.FrameHSMSelectReq},
		{"HSM_SELECT resp", enc(func() ([]byte, error) {
			return EncodeResponse(4, CmdHSMSelect, StatusOK, []byte{0x01, 0x00, 0x00, 0x00, 0x00, 0x00})
		}), vectors.FrameHSMSelectResp},
		{"HSM_LOAD req", enc(func() ([]byte, error) {
			return EncodeRequest(5, CmdHSMLoad, []byte{0x0e, 0x01, 0x00, 0x00, 0xd8, 0x6b, 0xa3, 0x02})
		}), vectors.FrameHSMLoadReq},
		{"HSM_LOAD RANGE", enc(func() ([]byte, error) { return EncodeResponse(5, CmdHSMLoad, StatusErrRange, nil) }), vectors.FrameHSMLoadRangeResp},
	} {
		if !bytes.Equal(c.got, []byte(c.want)) {
			t.Errorf("%s frame = % x, want % x", c.name, c.got, c.want)
		}
	}
	f, _, err := Decode([]byte(vectors.FrameOTAInfoResp))
	if err != nil {
		t.Fatal(err)
	}
	if len(f.Payload) != OTAInfoLen {
		t.Errorf("OTA_INFO reply payload = %d bytes, want %d", len(f.Payload), OTAInfoLen)
	}
}

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
