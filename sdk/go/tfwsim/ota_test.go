// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// stageImage builds an image the stager accepts: a body, erased padding to
// whole pages, and the v1 trailer naming the simulator's board (the 328P's
// 0x01) with the body's SHA-256.
func stageImage(total, body int) []byte {
	img := make([]byte, total)
	for i := range img {
		img[i] = 0xFF
	}
	for i := 0; i < body; i++ {
		img[i] = byte(i*7 + 3)
	}
	t := img[total-128:]
	for i := range t {
		t[i] = 0
	}
	copy(t, "TFIM")
	binary.LittleEndian.PutUint16(t[4:], 1)
	t[6] = 0x01
	binary.LittleEndian.PutUint32(t[8:], 3)
	binary.LittleEndian.PutUint32(t[12:], uint32(body))
	sum := sha256.Sum256(img[:body])
	copy(t[16:], sum[:])
	return img
}

func expectOTA(t *testing.T, d *tfw.Device, state tfw.OTAState, received uint32) {
	t.Helper()
	s, n, err := d.OTAStatus(context.Background())
	if err != nil {
		t.Fatalf("OTAStatus: %v", err)
	}
	if s != state || n != received {
		t.Fatalf("OTAStatus = %v, %d; want %v, %d", s, n, state, received)
	}
}

// The firmware's stager, into the simulated SPI store, driven over each link
// an Ethernet image has: a whole session over serial, then over TCP, then a
// tampered image the device must refuse at END.
func TestAgainstFirmwareStagesAnImage(t *testing.T) {
	ctx := context.Background()
	serial, eth := tfwsim.StartElementEthernet(t)

	img := stageImage(1024, 700)
	if err := serial.StageImage(ctx, img); err != nil {
		t.Fatalf("serial: StageImage: %v", err)
	}
	expectOTA(t, serial, tfw.OTAComplete, 1024)

	img = stageImage(2048, 1500)
	if err := eth.StageImage(ctx, img); err != nil {
		t.Fatalf("tcp: StageImage: %v", err)
	}
	expectOTA(t, eth.Device, tfw.OTAComplete, 2048)

	img[100] ^= 0x40
	if err := eth.StageImage(ctx, img); !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("tcp: tampered image: %v, want %v", err, tfw.StatusErrCrypto)
	}
	expectOTA(t, eth.Device, tfw.OTAFailed, 2048)
}

// The refusals a host can provoke, over serial: a length that is not whole
// pages, an offset that is not the next one, and END before the last byte.
func TestAgainstFirmwareStagerRefuses(t *testing.T) {
	ctx := context.Background()
	serial, _ := tfwsim.StartElementEthernet(t)

	if err := serial.OTABegin(ctx, 1000); !errors.Is(err, tfw.StatusErrRange) {
		t.Fatalf("BEGIN 1000: %v, want %v", err, tfw.StatusErrRange)
	}
	if err := serial.OTABegin(ctx, 512); err != nil {
		t.Fatalf("BEGIN 512: %v", err)
	}
	img := stageImage(512, 200)
	if err := serial.OTAData(ctx, 0, img[:tfw.OTAChunk]); err != nil {
		t.Fatalf("DATA 0: %v", err)
	}
	if err := serial.OTAData(ctx, 0, img[:tfw.OTAChunk]); !errors.Is(err, tfw.StatusErrRange) {
		t.Fatalf("DATA 0 again: %v, want %v", err, tfw.StatusErrRange)
	}
	if err := serial.OTAEnd(ctx); !errors.Is(err, tfw.StatusErrFraming) {
		t.Fatalf("early END: %v, want %v", err, tfw.StatusErrFraming)
	}
	if err := serial.OTAData(ctx, 0, nil); err == nil {
		t.Fatal("empty DATA: no error")
	}
}
