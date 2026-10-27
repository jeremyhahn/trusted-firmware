// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"io"
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The firmware's stager and OTA control, driven through ota-stage,
// ota-status, ota-info, ota-activate and ota-confirm.
//
// tfwctl sends OTA_AUTH immediately before every OTA_ACTIVATE and OTA_CONFIRM
// and refuses either without a signature, so no OTA_AUTH and one signature for
// one command go through the SDK's own calls (TestE2EOTAConfirmsARunningTrial);
// a command between that drops it is pinned in C by
// test/unit/test_ota_stage.c test_activate_and_confirm_need_ota_auth; ACTIVATE mode 2 by
// test_activate_takes_a_verified_image_and_resets_on_mode_1; DATA at an
// offset that is not the next and END before the last byte by
// test_write_past_the_end_is_refused. An empty DATA never leaves the SDK
// (sdk/go ota_total_test.go TestOTADataOutsideOneChunkIsRefused).
//
// The TestFirmware* rows reach their device through the peer seam
// (e2e_peer_test.go), so `make hw-test` runs them on a board flashed with the
// stager. They expect its OTA state fresh: nothing in TRIAL, the counter at
// floor 0. The signatures they hand over are any 64 bytes, which the
// application only holds; on a board, the image ota-activate leaves PENDING is
// refused by tfw-boot at the next reset.

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

// otaCmds is every OTA command an image's stager brings (commands.def): an
// image without one knows none of them.
var otaCmds = []tfw.Cmd{tfw.CmdOTABegin, tfw.CmdOTAData, tfw.CmdOTAEnd, tfw.CmdOTAStatus,
	tfw.CmdOTAInfo, tfw.CmdOTAActivate, tfw.CmdOTAConfirm, tfw.CmdOTAAuth}

// otaServing reports whether p's image carries the OTA stager. CAPS has no
// bit for it, so the row asks OTA_STATUS, which reads and changes nothing.
// Every simulator a row starts stages. A board whose image has no stager
// (the MPS2 boards, which have no OTA store: docs/cortex-m/README.md) must
// refuse every OTA command as unknown, and the row checks that instead.
// The probes carry no payload, and the loop runs only on an image that
// refused OTA_STATUS: an unknown command is refused before its length is
// checked (src/core/dispatch.c), so none of them stages or settles anything.
func otaServing(t *testing.T, p serialPeer) bool {
	t.Helper()
	_, err := p.serial.Do(context.Background(), tfw.CmdOTAStatus, nil)
	var st tfw.Status
	if !errorsAsStatus(err, &st) || st != tfw.StatusErrUnknownCmd {
		return true
	}
	require.True(t, p.board, "the simulator does not know OTA_STATUS")
	for _, cmd := range otaCmds {
		_, err := p.serial.Do(context.Background(), cmd, nil)
		require.True(t, errorsAsStatus(err, &st), "command 0x%02x: %v, want a device status", byte(cmd), err)
		assert.Equal(t, tfw.StatusErrUnknownCmd, st,
			"command 0x%02x: OTA_STATUS is unknown, so the image has no stager and must not know it", byte(cmd))
	}
	return false
}

// otaCtlServing reports whether p's image has OTA_INFO and OTA_CONFIRM, which
// src/core/ota_ctl.c serves where tfw-boot runs the trial (BOOT_AB) and
// nowhere else: a single-slot stager has OTA_ACTIVATE alone. CAPS has no bit
// for them either, so the row asks OTA_INFO for slot 0, which reads and
// changes nothing. An image without it must refuse OTA_CONFIRM as unknown
// too, and the row checks that instead; that refusal comes before OTA_AUTH
// is asked for, as an unknown command is refused before its handler runs.
func otaCtlServing(t *testing.T, p serialPeer) bool {
	t.Helper()
	_, err := p.serial.Do(context.Background(), tfw.CmdOTAInfo, []byte{0})
	var st tfw.Status
	if !errorsAsStatus(err, &st) || st != tfw.StatusErrUnknownCmd {
		return true
	}
	require.True(t, p.board, "the simulator does not know OTA_INFO")
	_, err = p.serial.Do(context.Background(), tfw.CmdOTAConfirm, nil)
	require.True(t, errorsAsStatus(err, &st), "OTA_CONFIRM: %v, want a device status", err)
	assert.Equal(t, tfw.StatusErrUnknownCmd, st, "OTA_INFO is unknown, so OTA_CONFIRM must be too")
	return false
}

// otaRefusedWithoutASession is an OTA control row on a board that cannot open
// the operator session (CAPS bit 14 clear, which operatorServing has checked
// the board refuses). The verb is still refused: by the auth gate where the
// image has the command, and as unknown on an image without the stager
// (otaServing) or without that command. The soft tier has no session to gate
// on -- its gate is open (src/core/auth_session_none.c) -- so there the
// refusal checked is the row key's: the command sent with no OTA_AUTH before
// it is AUTH_REQUIRED on every tier (#137).
func otaRefusedWithoutASession(t *testing.T, p serialPeer, verb string, args ...string) {
	t.Helper()
	if !otaServing(t, p) {
		return
	}
	var st tfw.Status
	if cliCaps(t, p.serial).CryptoTier == tfw.CryptoSoftware {
		var err error
		switch verb {
		case "ota-activate":
			err = p.serial.OTAActivate(context.Background(), tfw.OTAActivateAtReset)
		case "ota-confirm":
			err = p.serial.OTAConfirm(context.Background())
		default:
			require.FailNow(t, "no OTA command for "+verb)
		}
		require.True(t, errorsAsStatus(err, &st), "%s with no OTA_AUTH: %v, want a device status", verb, err)
	} else {
		st = cliStatus(t, p.serial, verb, args...)
	}
	if p.board && st == tfw.StatusErrUnknownCmd {
		return
	}
	assert.Equal(t, tfw.StatusErrAuthRequired, st, "%s with no operator session", verb)
}

// A whole session over serial, then over TCP, then a tampered image the
// device must refuse at END.
func TestEthernetStagesAnImage(t *testing.T) {
	p := startEthernet(t)
	serial, eth := p.serial, p.eth

	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 1024}, cliStage(t, serial, stageImage(1024, 700)), "serial")

	img := stageImage(2048, 1500)
	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 2048}, cliStage(t, eth, img), "tcp")

	img[100] ^= 0x40
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, eth, "ota-stage", writeFile(t, "bad.bin", img)), "tampered image")
	assert.Equal(t, otaStatus{tfw.OTAFailed.String(), 2048}, cliOTAStatus(t, eth))
}

// A length that is not whole pages is refused at BEGIN.
func TestEthernetStagerRefusesALengthNotWholePages(t *testing.T) {
	p := startEthernet(t)
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, p.serial, "ota-stage", writeFile(t, "odd.bin", make([]byte, 1000))))
}

// configH is where the firmware's default OTA_BEGIN budget is defined; no
// board profile overrides it, and the test reads it rather than repeat it.
const configH = "../../../../include/tfw/config.h"

// otaBeginBudget reads TFW_OTA_BEGIN_BUDGET from configH.
func otaBeginBudget(t *testing.T) int {
	t.Helper()
	src, err := os.ReadFile(configH)
	require.NoError(t, err)
	m := regexp.MustCompile(`(?m)^#define\s+TFW_OTA_BEGIN_BUDGET\s+(\d+)u?\s*$`).FindSubmatch(src)
	require.NotNil(t, m, "%s defines no TFW_OTA_BEGIN_BUDGET", configH)
	n, err := strconv.Atoi(string(m[1]))
	require.NoError(t, err)
	require.Positive(t, n)
	return n
}

// The peer-binding ADR's decision 1 B over Ethernet: a peer looping OTA_BEGIN
// is refused StatusErrBadState once the budget since reset is spent, on both
// links, and the refused BEGIN erases nothing. The last budgeted session is
// left part-way; after the refusal it carries on at the next offset and
// OTA_END, which reads the whole image back from the store, verifies it. A
// BEGIN that had run would have reset the session and erased the MARK.
// OTA_ACTIVATE's refill is test_ota_stage.c's: on the wire, mode 0 leaves an
// install pending that BEGIN refuses on its own, and mode 1 resets.
func TestEthernetStagingLoopIsRefusedAfterTheBudget(t *testing.T) {
	p := startEthernet(t)
	serial, eth := p.serial, p.eth
	ctx := t.Context()
	budget := otaBeginBudget(t)
	img := stageImage(1024, 700)

	for i := 1; i < budget; i++ {
		assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 1024}, cliStage(t, eth, img), "BEGIN %d of %d", i, budget)
	}
	require.NoError(t, eth.OTABegin(ctx, uint32(len(img))), "BEGIN %d of %d", budget, budget)
	require.NoError(t, eth.OTAData(ctx, 0, img[:tfw.OTAChunk]))
	part := otaStatus{tfw.OTAReceiving.String(), tfw.OTAChunk}
	require.Equal(t, part, cliOTAStatus(t, eth))

	assert.ErrorIs(t, eth.OTABegin(ctx, uint32(len(img))), tfw.StatusErrBadState, "BEGIN %d over TCP", budget+1)
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, eth, "ota-stage", writeFile(t, "img.bin", img)), "ota-stage over TCP")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, serial, "ota-stage", writeFile(t, "img.bin", img)), "the budget is the device's, not the link's")
	assert.Equal(t, part, cliOTAStatus(t, eth), "the refused BEGINs left the session as it was")

	for off := tfw.OTAChunk; off < len(img); off += tfw.OTAChunk {
		require.NoError(t, eth.OTAData(ctx, uint32(off), img[off:min(off+tfw.OTAChunk, len(img))]), "DATA at %d", off)
	}
	require.NoError(t, eth.OTAEnd(ctx), "the image reads back whole: nothing was erased")
	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 1024}, cliOTAStatus(t, eth))
}

// OTA_AUTH on the soft tier (#137): the firmware, not a fake. The simulator
// does not verify the signature -- tfw-boot does, at reset -- so any 64 bytes
// pass here, and each command gets past the gate to its own checks. On a
// board the image is staged under the board id CAPS reports, which the
// stager requires of the trailer.
func TestFirmwareOTAAuthLetsTheCommandPastTheGate(t *testing.T) {
	p := startPeer(t, simproc.Software)
	if !otaServing(t, p) {
		return
	}
	d := p.serial
	caps, err := d.Caps(t.Context())
	require.NoError(t, err)
	cliStage(t, d, stageImageFor(512, 200, caps.BoardID))

	// No tfw-boot handed off a running trial here. A single-slot image has
	// no OTA_CONFIRM at all (ota_ctl.c is linked with BOOT_AB alone), and
	// the refusal is by status code: CAPS has no bit for it. Past OTA_AUTH,
	// this unprovisioned simulator refuses both commands as not provisioned,
	// so the no-trial and MARK legs are test_ota_stage.c's.
	err = run(t.Context(), d, "ota-confirm", []string{heldSig}, false, io.Discard)
	assert.NotErrorIs(t, err, tfw.StatusErrAuthRequired, "confirm, authorized")
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "confirm: %v", err)
	switch {
	case !otaCtlServing(t, p):
		assert.Equal(t, tfw.StatusErrUnknownCmd, st, "confirm on a single-slot image")
	case !p.board:
		assert.Equal(t, tfw.StatusErrNotProvisioned, st, "confirm")
	}

	// A board's platform block is provisioned and GOOD, and the soft tier's
	// gate is open (src/core/auth_session_none.c), so there the activate may
	// write the MARK: tfw-boot refuses heldSig at the next reset and forgets
	// the MARK (boot/boot_stage.c).
	err = run(t.Context(), d, "ota-activate", []string{heldSig}, false, io.Discard)
	assert.NotErrorIs(t, err, tfw.StatusErrAuthRequired, "activate, authorized")
	if p.board {
		return
	}
	require.True(t, errorsAsStatus(err, &st), "activate: %v", err)
	assert.Equal(t, tfw.StatusErrNotProvisioned, st, "activate")
}

// The element tier, because it is the tfwsim tier with a platform block: the
// software tier carries none and answers every decision-8 command with its
// load's no-identity status. Values against the ADR's decision 8 (the wire
// row of docs/ota/adr-ota-image-format-and-trust.md).
func TestFirmwareOTAInfo(t *testing.T) {
	// The stager first: an image without one is refused whatever its tier.
	// Then OTA_INFO itself: a single-slot image (the MPS2 boards' store row)
	// stages without it and refuses it and OTA_CONFIRM by status code, on
	// every tier, so the tier below is asked of an image that has them.
	p := openPeer(t, nil, simproc.Element, elementArgs(t, nil, nil)...)
	if !otaServing(t, p) || !otaCtlServing(t, p) {
		return
	}
	requireTier(t, p, simproc.Element)
	d := p.serial

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

func TestFirmwareOTAActivate(t *testing.T) {
	p, keyPath, ok := operatorServing(t)
	if !ok {
		otaRefusedWithoutASession(t, p, "ota-activate", heldSig)
		return
	}
	d, serial := p.serial, p.serialHex(t)

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

func TestFirmwareOTAConfirm(t *testing.T) {
	p, keyPath, ok := operatorServing(t)
	if !ok {
		otaRefusedWithoutASession(t, p, "ota-confirm", heldSig)
		return
	}
	d, serial := p.serial, p.serialHex(t)

	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "ota-confirm", heldSig), "unauthenticated")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "outside TRIAL")
}

// The platform block's state bytes and its CRC (include/tfw/boot.h), and
// the values tfw-boot writes there (docs/ota/architecture.md, "What runs
// today").
const (
	blockOffBootState = 48
	blockOffOTAState  = 49
	blockOffCRC       = 190

	bootGood      = 2
	bootTrial     = 3
	bootConfirmed = 4
	otaIdle       = 0
	otaFailed     = 3
)

// startOperatorIn is startOperator with the platform block in the state
// tfw-boot leaves it in, and the handoff of the running image tfw-boot jumped
// to.
func startOperatorIn(t *testing.T, bootState, otaState byte) (dev *tfw.Device, serial, keyPath string) {
	t.Helper()
	block, keyPath := operatorBlock(t, bootState, otaState)
	dev = tfwsim.StartElementWith(t, []string{"--eeprom-bin", writeFile(t, "platform.bin", block),
		"--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])})
	return dev, hex.EncodeToString(tfwsim.TestSerial[:]), keyPath
}

// operatorBlock is a platform block with an operator key provisioned and the
// state bytes given, and the key's PEM path.
func operatorBlock(t *testing.T, bootState, otaState byte) (block []byte, keyPath string) {
	t.Helper()
	pub, keyPath := writeOperatorKey(t)
	sec1, err := pub.Bytes()
	require.NoError(t, err)
	var op [tfw.OperatorPubkeyLen]byte
	copy(op[:], sec1[1:])

	b := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, op)
	b[blockOffBootState], b[blockOffOTAState] = bootState, otaState
	binary.LittleEndian.PutUint16(b[blockOffCRC:], tfw.CRC16(b[:blockOffCRC]))
	return b[:], keyPath
}

// A trial running, as tfw-boot hands it off (#77): OTA_INFO reports it,
// OTA_ACTIVATE refuses it, and OTA_CONFIRM, authorized, takes it. With the
// session open, the refusal of a command that came without OTA_AUTH is the
// signature's alone, and one OTA_AUTH lets one command through.
func TestE2EOTAConfirmsARunningTrial(t *testing.T) {
	d, serial, keyPath := startOperatorIn(t, bootTrial, otaIdle)
	ctx := t.Context()

	trial := slotInfo{Slots: 2, Flags: int(tfw.SlotTrial), State: bootTrial, Measurement: strings.Repeat("00", 32)}
	assert.Equal(t, trial, cliOTAInfo(t, d, "0"))
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "ota-confirm", heldSig), "no session")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-activate", heldSig), "activate in TRIAL")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "ota-activate", "now", heldSig), "activate now in TRIAL")

	assert.ErrorIs(t, d.OTAConfirm(ctx), tfw.StatusErrAuthRequired, "confirm without OTA_AUTH")
	assert.ErrorIs(t, d.OTAActivate(ctx, tfw.OTAActivateAtReset), tfw.StatusErrAuthRequired, "activate without OTA_AUTH")

	require.NoError(t, run(ctx, d, "ota-confirm", []string{heldSig}, false, io.Discard))
	assert.ErrorIs(t, d.OTAConfirm(ctx), tfw.StatusErrAuthRequired, "the signature is spent")

	// The confirm writes its request, never the block: tfw-boot moves TRIAL
	// to CONFIRMED at the next reset, so the trial still reads as running.
	assert.Equal(t, trial, cliOTAInfo(t, d, "0"))
}

// A confirm tfw-boot has not settled yet: the floor advance is still to
// come, so neither command takes the block.
func TestE2EOTARefusesAnUnsettledConfirm(t *testing.T) {
	d, serial, keyPath := startOperatorIn(t, bootConfirmed, otaIdle)

	assert.Equal(t, slotInfo{Slots: 2, State: bootConfirmed, Measurement: strings.Repeat("00", 32)}, cliOTAInfo(t, d, "0"))
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "confirm in CONFIRMED")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "ota-activate", heldSig), "activate in CONFIRMED")
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "ota-activate", "now", heldSig), "activate now in CONFIRMED")
}

// A trial that ended unconfirmed and was rolled back: GOOD with FAILED.
// OTA_INFO says so on both slots, and there is no trial left to confirm.
func TestE2EOTAReportsAFallBack(t *testing.T) {
	d, serial, keyPath := startOperatorIn(t, bootGood, otaFailed)

	zero := strings.Repeat("00", 32)
	assert.Equal(t, slotInfo{Slots: 2, Flags: int(tfw.SlotFellBack), State: bootGood, Measurement: zero}, cliOTAInfo(t, d, "0"))
	assert.Equal(t, slotInfo{Slots: 2, Flags: int(tfw.SlotFellBack), State: otaFailed, Measurement: zero}, cliOTAInfo(t, d, "1"))
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "confirm after the fall-back")
}

// The OTA control plane is serial only: OTA_INFO, OTA_ACTIVATE, OTA_AUTH and
// OTA_CONFIRM are not on the Ethernet allow-list (src/proto/link_eth.c,
// src/core/commands.def), so over TCP each is TFW_ERR_UNKNOWN_CMD while the
// same simulator answers it over serial. One tfwsim in a running trial, both
// transports, every verb: a refusal over TCP is the link's, never the trial's
// or the missing OTA_AUTH's, and nothing sent over TCP confirms the trial.
func TestE2EOTAControlIsSerialOnly(t *testing.T) {
	block, keyPath := operatorBlock(t, bootTrial, otaIdle)
	serialDev, eth := startEthernetWith(t, block)
	serial := hex.EncodeToString(tfwsim.TestSerial[:])

	cases := []struct {
		name   string
		args   []string
		serial tfw.Status
	}{
		{"ota-info 0", []string{"ota-info", "0"}, tfw.StatusOK},
		{"ota-info 1", []string{"ota-info", "1"}, tfw.StatusOK},
		{"ota-info 2", []string{"ota-info", "2"}, tfw.StatusErrRange},
		{"ota-activate, no OTA_AUTH", []string{"ota-activate", heldSig}, tfw.StatusErrAuthRequired},
		{"ota-activate now, no OTA_AUTH", []string{"ota-activate", "now", heldSig}, tfw.StatusErrAuthRequired},
		{"ota-confirm, no OTA_AUTH", []string{"ota-confirm", heldSig}, tfw.StatusErrAuthRequired},
		{"ota-activate in TRIAL", []string{"auth", serial, keyPath, "--", "ota-activate", heldSig}, tfw.StatusErrBadState},
		{"ota-activate now in TRIAL", []string{"ota-activate", "now", heldSig}, tfw.StatusErrBadState},
	}
	for _, tc := range cases {
		if tc.serial == tfw.StatusOK {
			assert.NoError(t, run(t.Context(), serialDev, tc.args[0], tc.args[1:], false, io.Discard), "serial: %s", tc.name)
		} else {
			assert.Equal(t, tc.serial, cliStatus(t, serialDev, tc.args[0], tc.args[1:]...), "serial: %s", tc.name)
		}
		assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, eth, tc.args[0], tc.args[1:]...), "tcp: %s", tc.name)
	}
	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, eth, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "tcp: ota-confirm in TRIAL")

	ctx := t.Context()
	assert.ErrorIs(t, eth.OTAAuth(ctx, [tfw.OTAAuthSigLen]byte{}), tfw.StatusErrUnknownCmd, "tcp: OTA_AUTH")
	assert.ErrorIs(t, eth.OTAConfirm(ctx), tfw.StatusErrUnknownCmd, "tcp: OTA_CONFIRM")
	assert.ErrorIs(t, eth.OTAActivate(ctx, tfw.OTAActivateAtReset), tfw.StatusErrUnknownCmd, "tcp: OTA_ACTIVATE")

	// Serial still holds the trial: TCP confirmed nothing, and serial does.
	trial := slotInfo{Slots: 2, Flags: int(tfw.SlotTrial), State: bootTrial, Measurement: strings.Repeat("00", 32)}
	assert.Equal(t, trial, cliOTAInfo(t, serialDev, "0"))
	require.NoError(t, run(ctx, serialDev, "ota-confirm", []string{heldSig}, false, io.Discard))
}

// The ADR's open question 8, decided (ruling 109, Software 2026-10-26, #257):
// an auth session ends when its connection closes, as well as at reset. Over
// TCP none opens (owner item 48 keeps the session, OTA_AUTH, OTA_ACTIVATE and
// OTA_CONFIRM on serial), so the board's next connection starts with nothing.
func TestE2EOTAAuthSessionEndsWithItsConnection(t *testing.T) {
	block, keyPath := operatorBlock(t, bootTrial, otaIdle)
	serialDev, first, l := startEthernetListen(t, block, nil)
	serial := hex.EncodeToString(tfwsim.TestSerial[:])
	ctx := t.Context()

	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, first.Device, "auth", serial, keyPath, "--", "ota-confirm", heldSig), "tcp: the session")
	assert.ErrorIs(t, first.OTAAuth(ctx, [tfw.OTAAuthSigLen]byte{}), tfw.StatusErrUnknownCmd, "tcp: OTA_AUTH")
	require.NoError(t, first.Close())

	// The board reads the peer's FIN, closes its socket and dials again.
	next, err := acceptDevice(l, deviceWait, tfw.WithTimeout(10*time.Second))
	require.NoError(t, err)
	t.Cleanup(func() { _ = next.Close() })

	assert.ErrorIs(t, next.OTAConfirm(ctx), tfw.StatusErrUnknownCmd, "tcp: OTA_CONFIRM on the next connection")
	assert.ErrorIs(t, next.OTAActivate(ctx, tfw.OTAActivateAtReset), tfw.StatusErrUnknownCmd, "tcp: OTA_ACTIVATE on the next connection")
	_, _, err = next.OTAStatus(ctx)
	require.NoError(t, err, "tcp: the next connection still stages")

	// Nothing the closed connection sent authorizes serial, and the trial stands.
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, serialDev, "ota-confirm", heldSig), "serial: no session")
	trial := slotInfo{Slots: 2, Flags: int(tfw.SlotTrial), State: bootTrial, Measurement: strings.Repeat("00", 32)}
	assert.Equal(t, trial, cliOTAInfo(t, serialDev, "0"))
}
