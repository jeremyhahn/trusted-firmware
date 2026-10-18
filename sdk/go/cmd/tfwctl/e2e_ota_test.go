// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The firmware's stager and OTA control, driven through ota-stage,
// ota-status, ota-info, ota-activate and ota-confirm.
//
// tfwctl sends OTA_AUTH immediately before every OTA_ACTIVATE and OTA_CONFIRM
// and refuses either without a signature, so what only a raw sequence can
// show is pinned in C: no OTA_AUTH, a command between that drops it, and one
// signature for one command by test/unit/test_ota_stage.c
// test_activate_and_confirm_need_ota_auth; ACTIVATE mode 2 by
// test_activate_takes_a_verified_image_and_resets_on_mode_1; DATA at an
// offset that is not the next and END before the last byte by
// test_write_past_the_end_is_refused. An empty DATA never leaves the SDK
// (sdk/go ota_total_test.go TestOTADataOutsideOneChunkIsRefused).

type slotInfo struct {
	Slots        int    `json:"slots"`
	Active       int    `json:"active"`
	Flags        int    `json:"flags"`
	Floor        int    `json:"floor"`
	State        int    `json:"state"`
	ImageVersion int    `json:"image_version"`
	Measurement  string `json:"measurement"`
}

func cliOTAInfo(t *testing.T, dev *tfw.Device, slot string) slotInfo {
	t.Helper()
	var si slotInfo
	cliJSON(t, dev, &si, "ota-info", slot)
	return si
}

func cliStage(t *testing.T, dev *tfw.Device, img []byte, args ...string) otaStatus {
	t.Helper()
	var s otaStatus
	cliJSON(t, dev, &s, "ota-stage", append(args, writeFile(t, "img.bin", img))...)
	return s
}

// startOperator starts the element tier with an operator key provisioned and
// returns it with the key's PEM path and the serial auth takes.
func startOperator(t *testing.T) (dev *tfw.Device, serial, keyPath string) {
	t.Helper()
	pub, keyPath := writeOperatorKey(t)
	return tfwsim.StartElementWithOperator(t, pub), hex.EncodeToString(tfwsim.TestSerial[:]), keyPath
}

// A whole session over serial, then over TCP, then a tampered image the
// device must refuse at END.
func TestE2EStagesAnImage(t *testing.T) {
	serial, eth := startEthernet(t)

	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 1024}, cliStage(t, serial, stageImage(1024, 700)), "serial")

	img := stageImage(2048, 1500)
	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 2048}, cliStage(t, eth, img), "tcp")

	img[100] ^= 0x40
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, eth, "ota-stage", writeFile(t, "bad.bin", img)), "tampered image")
	assert.Equal(t, otaStatus{tfw.OTAFailed.String(), 2048}, cliOTAStatus(t, eth))
}

// A length that is not whole pages is refused at BEGIN.
func TestE2EStagerRefusesALengthNotWholePages(t *testing.T) {
	serial, _ := startEthernet(t)
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, serial, "ota-stage", writeFile(t, "odd.bin", make([]byte, 1000))))
}

// OTA_AUTH on the soft tier (#137): the firmware, not a fake. The simulator
// does not verify the signature -- tfw-boot does, at reset -- so any 64 bytes
// pass here, and each command gets past the gate to its own checks.
func TestE2EOTAAuthLetsTheCommandPastTheGate(t *testing.T) {
	d := tfwsim.Start(t)
	cliStage(t, d, stageImage(512, 200))

	// No tfw-boot handed off a running trial here.
	err := run(t.Context(), d, "ota-confirm", []string{heldSig}, false, io.Discard)
	require.Error(t, err)
	assert.NotErrorIs(t, err, tfw.StatusErrAuthRequired, "confirm, authorized")

	// This unprovisioned simulator's auth gate refuses next, so the MARK leg
	// is test_ota_stage.c's.
	err = run(t.Context(), d, "ota-activate", []string{heldSig}, false, io.Discard)
	require.Error(t, err)
	assert.NotErrorIs(t, err, tfw.StatusErrAuthRequired, "activate, authorized")
}

// The element tier, because it is the tfwsim tier with a platform block: the
// software tier carries none and answers every decision-8 command with its
// load's no-identity status. Values against the ADR's decision 8 (the wire
// row of docs/ota/adr-ota-image-format-and-trust.md).
func TestE2EOTAInfo(t *testing.T) {
	d := tfwsim.StartElement(t)

	// slots 2 and active 0 are the AVR's fixed answer (one primary, the
	// store); flags 0, the block neither fell back nor is in TRIAL; floor 0,
	// the element's counter is fresh; state 0, the block is EMPTY (slot 0)
	// and nothing is staged (slot 1, IDLE); no image version and no
	// measurement, as slot 0 does not report the running image's and slot 1
	// has no MARK.
	want := slotInfo{Slots: 2, Measurement: strings.Repeat("00", 32)}
	for _, slot := range []string{"0", "1"} {
		assert.Equal(t, want, cliOTAInfo(t, d, slot), "ota-info %s", slot)
	}
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, d, "ota-info", "2"))
}

func TestE2EOTAActivate(t *testing.T) {
	d, serial, keyPath := startOperator(t)

	for _, args := range [][]string{{heldSig}, {"now", heldSig}} {
		assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "ota-activate", args...), "ota-activate %v unauthenticated", args[:len(args)-1])
	}
	// auth opens the session and runs the one verb chained after it; the
	// session outlives the verb, so the second mode needs no second auth.
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-activate", heldSig), "nothing staged")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "ota-activate", "now", heldSig), "now, nothing staged")

	img := stageImage(1024, 700)
	cliStage(t, d, img, "--activate", heldSig)
	sum := sha256.Sum256(img[:700])
	si := cliOTAInfo(t, d, "1")
	assert.Equal(t, 4, si.State, "PENDING")
	assert.Equal(t, 3, si.ImageVersion)
	assert.Equal(t, hex.EncodeToString(sum[:]), si.Measurement)
}

func TestE2EOTAConfirm(t *testing.T) {
	d, serial, keyPath := startOperator(t)

	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "ota-confirm", heldSig), "unauthenticated")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "outside TRIAL")
}
