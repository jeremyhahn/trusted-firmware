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

// OTA_AUTH on the soft tier (#137): the firmware, not a fake. Without a
// signature held from the command just before, OTA_ACTIVATE and OTA_CONFIRM
// answer TFW_ERR_AUTH_REQUIRED; any command between drops it; one signature
// authorizes one command. The simulator does not verify it -- tfw-boot does,
// at reset -- so any 64 bytes pass here.
func TestAgainstFirmwareOTAAuthAuthorizesOneCommand(t *testing.T) {
	ctx := context.Background()
	d := tfwsim.Start(t)
	var sig [tfw.OTAAuthSigLen]byte
	for i := range sig {
		sig[i] = 0xA5
	}
	auth := func() {
		t.Helper()
		if err := d.OTAAuth(ctx, sig); err != nil {
			t.Fatalf("OTAAuth: %v", err)
		}
	}
	if err := d.StageImage(ctx, stageImage(512, 200)); err != nil {
		t.Fatalf("StageImage: %v", err)
	}

	if err := d.OTAActivate(ctx, tfw.OTAActivateAtReset); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("activate, no OTA_AUTH: %v, want %v", err, tfw.StatusErrAuthRequired)
	}
	if err := d.OTAConfirm(ctx); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("confirm, no OTA_AUTH: %v, want %v", err, tfw.StatusErrAuthRequired)
	}

	auth()
	if _, err := d.Do(ctx, tfw.CmdPing, nil); err != nil {
		t.Fatalf("PING: %v", err)
	}
	if err := d.OTAActivate(ctx, tfw.OTAActivateAtReset); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("activate after PING: %v, want %v", err, tfw.StatusErrAuthRequired)
	}

	// Authorized, confirm passes the gate to its own checks, which refuse:
	// no tfw-boot handed off a running trial here.
	auth()
	if err := d.OTAConfirm(ctx); err == nil || errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("confirm, authorized: %v, want a refusal past the gate", err)
	}

	// Authorized, activate passes OTA_AUTH's check; this unprovisioned
	// simulator's auth gate then refuses, so the MARK leg is the host suite's
	// (test/unit/test_ota_stage.c).
	auth()
	if err := d.OTAActivate(ctx, tfw.OTAActivateAtReset); err == nil || errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("activate, authorized: %v, want a refusal past OTA_AUTH's check", err)
	}
	if err := d.OTAActivate(ctx, tfw.OTAActivateAtReset); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("activate again, same OTA_AUTH: %v, want %v", err, tfw.StatusErrAuthRequired)
	}
}

// The element tier, because it is the tfwsim tier with a platform block: the
// software tier carries none and answers every decision-8 command with its
// load's no-identity status. Values against the ADR's decision 8 (the wire
// row of docs/ota/adr-ota-image-format-and-trust.md).
func TestAgainstFirmwareOTAInfo(t *testing.T) {
	ctx := context.Background()
	d := tfwsim.StartElement(t)

	for slot := byte(0); slot < 2; slot++ {
		si, err := d.OTAInfo(ctx, slot)
		if err != nil {
			t.Fatalf("INFO %d: %v", slot, err)
		}
		// slots 2 and active 0 are the AVR's fixed answer (one primary, the
		// store); flags 0, the block neither fell back nor is in TRIAL; floor
		// 0, the element's counter is fresh; state 0, the block is EMPTY
		// (slot 0) and nothing is staged (slot 1, IDLE); no image version and
		// no measurement, as slot 0 does not report the running image's and
		// slot 1 has no MARK.
		want := tfw.SlotInfo{Slots: 2}
		if *si != want {
			t.Fatalf("INFO %d: %+v, want %+v", slot, *si, want)
		}
	}
	if _, err := d.OTAInfo(ctx, 2); !errors.Is(err, tfw.StatusErrRange) {
		t.Fatalf("INFO 2: %v, want %v", err, tfw.StatusErrRange)
	}
}

func TestAgainstFirmwareOTAActivate(t *testing.T) {
	ctx := context.Background()
	op := operatorKey(t)
	d := tfwsim.StartElementWithOperator(t, &op.PublicKey)

	// The mode is checked before the session; the SDK refuses 2 itself, so
	// it goes raw.
	if _, err := d.Do(ctx, tfw.CmdOTAActivate, []byte{2}); !errors.Is(err, tfw.StatusErrRange) {
		t.Fatalf("ACTIVATE 2: %v, want %v", err, tfw.StatusErrRange)
	}
	for mode := byte(0); mode < 2; mode++ {
		if err := d.OTAActivate(ctx, mode); !errors.Is(err, tfw.StatusErrAuthRequired) {
			t.Fatalf("ACTIVATE %d unauthenticated: %v, want %v", mode, err, tfw.StatusErrAuthRequired)
		}
	}
	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AuthResponse(ctx, op, nonce, tfwsim.TestSerial[:]); err != nil {
		t.Fatalf("AuthResponse: %v", err)
	}
	for mode := byte(0); mode < 2; mode++ {
		heldAuth(t, d)
		if err := d.OTAActivate(ctx, mode); !errors.Is(err, tfw.StatusErrBadState) {
			t.Fatalf("ACTIVATE %d, nothing staged: %v, want %v", mode, err, tfw.StatusErrBadState)
		}
	}

	img := stageImage(1024, 700)
	if err := d.StageImage(ctx, img); err != nil {
		t.Fatalf("StageImage: %v", err)
	}
	heldAuth(t, d)
	if err := d.OTAActivate(ctx, tfw.OTAActivateAtReset); err != nil {
		t.Fatalf("ACTIVATE at reset, staged: %v", err)
	}
	si, err := d.OTAInfo(ctx, 1)
	if err != nil {
		t.Fatalf("INFO 1: %v", err)
	}
	if si.State != 4 || si.ImageVersion != 3 || si.Measurement != sha256.Sum256(img[:700]) {
		t.Fatalf("INFO 1 after ACTIVATE: %+v, want PENDING (4), version 3 and the image's hash", *si)
	}
}

func TestAgainstFirmwareOTAConfirm(t *testing.T) {
	ctx := context.Background()
	op := operatorKey(t)
	d := tfwsim.StartElementWithOperator(t, &op.PublicKey)

	if err := d.OTAConfirm(ctx); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("CONFIRM unauthenticated: %v, want %v", err, tfw.StatusErrAuthRequired)
	}
	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AuthResponse(ctx, op, nonce, tfwsim.TestSerial[:]); err != nil {
		t.Fatalf("AuthResponse: %v", err)
	}
	heldAuth(t, d)
	if err := d.OTAConfirm(ctx); !errors.Is(err, tfw.StatusErrBadState) {
		t.Fatalf("CONFIRM outside TRIAL: %v, want %v", err, tfw.StatusErrBadState)
	}
}

// heldAuth sends OTA_AUTH (#137) so the next command passes the application's
// gate to the checks a test is about. The simulator only holds the signature;
// tfw-boot verifies it at reset, so any 64 bytes do here.
func heldAuth(t *testing.T, d *tfw.Device) {
	t.Helper()
	var sig [tfw.OTAAuthSigLen]byte
	for i := range sig {
		sig[i] = 0xA5
	}
	if err := d.OTAAuth(context.Background(), sig); err != nil {
		t.Fatalf("OTAAuth: %v", err)
	}
}
