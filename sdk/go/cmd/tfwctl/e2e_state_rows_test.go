// SPDX-License-Identifier: AGPL-3.0-or-later

//go:build tfwsim

package main

import (
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// statePeer answers PING and SYS_INFO as a board does: it sends no BOOT on
// connect, and its framer counters are crc and format, kept from whatever
// rows ran before (a reset board reports zero). Each SYS_INFO reports the
// next of causes as its reset cause and stays on the last; none is power-on.
func statePeer(t *testing.T, crc, format uint16, causes ...tfw.ResetCause) *tfw.Device {
	t.Helper()
	if len(causes) == 0 {
		causes = []tfw.ResetCause{tfw.ResetPowerOn}
	}
	return startFakeDevice(t, func(f tfw.Frame) (tfw.Status, []byte) {
		switch f.Cmd() {
		case tfw.CmdPing:
			return tfw.StatusOK, f.Payload
		case tfw.CmdSysInfo:
			s := binary.LittleEndian.AppendUint32(nil, 61000)
			s = binary.LittleEndian.AppendUint16(s, 2048)
			s = append(s, byte(causes[0]))
			if len(causes) > 1 {
				causes = causes[1:]
			}
			s = binary.LittleEndian.AppendUint16(s, crc)
			return tfw.StatusOK, binary.LittleEndian.AppendUint16(s, format)
		}
		return tfw.StatusErrUnknownCmd, nil
	})
}

// A board found running (a TCP wire, no reset) runs the boot-event row and
// the SYS row in the order the P4 run takes them, after the malformed burst
// has raised its framer counters: both rows hold on the state it keeps.
func TestStateRowsHoldInSequenceOnABoardFoundRunning(t *testing.T) {
	dev := statePeer(t, 3, 2)
	t.Run("PingDeliversTheBootEventWithNoBootWait", func(t *testing.T) {
		bootEventRow(t, dev, false)
	})
	t.Run("Sys", func(t *testing.T) {
		sysRow(t, dev, []tfw.ResetCause{tfw.ResetPowerOn}, false)
	})
}

// A reset peer reports clean counters, and the SYS row still requires them.
func TestSysRowRequiresCleanCountersAfterAReset(t *testing.T) {
	sysRow(t, statePeer(t, 0, 0), []tfw.ResetCause{tfw.ResetPowerOn}, true)
}

// A board found running reports the cause of its last boot, which the run
// did not choose: power-on after a power cycle, software after a RESET
// command (src/hal/esp32/hal_sys.c). The row takes either of the board's
// held causes, and fails one that is not among them.
func TestSysRowTakesTheCauseABoardFoundRunningReports(t *testing.T) {
	held := peerBoards["esp32p4-poe"].causes(false)
	for _, cause := range []tfw.ResetCause{tfw.ResetPowerOn, tfw.ResetSoftware} {
		dev := statePeer(t, 3, 2, cause)
		assert.Empty(t, rowFailure(t, func(tb testing.TB) { sysRow(tb, dev, held, false) }), "%s", cause)
	}
	dev := statePeer(t, 3, 2, tfw.ResetPanic)
	assert.Contains(t, rowFailure(t, func(tb testing.TB) { sysRow(tb, dev, held, false) }), "reset cause")
}

// The cause of a board found running is stable across exchanges: one that
// changes between two SYS_INFOs restarted under the row.
func TestSysRowFailsACauseThatChangesAcrossExchanges(t *testing.T) {
	held := peerBoards["esp32p4-poe"].causes(false)
	dev := statePeer(t, 3, 2, tfw.ResetPowerOn, tfw.ResetSoftware)
	assert.Contains(t, rowFailure(t, func(tb testing.TB) { sysRow(tb, dev, held, false) }), "restarted")
}
