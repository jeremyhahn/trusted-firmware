// SPDX-License-Identifier: AGPL-3.0-or-later

//go:build tfwsim

package main

import (
	"encoding/binary"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// statePeer answers PING and SYS_INFO as a board does: it sends no BOOT on
// connect, and its framer counters are crc and format, kept from whatever
// rows ran before (a reset board reports zero).
func statePeer(t *testing.T, crc, format uint16) *tfw.Device {
	t.Helper()
	return startFakeDevice(t, func(f tfw.Frame) (tfw.Status, []byte) {
		switch f.Cmd() {
		case tfw.CmdPing:
			return tfw.StatusOK, f.Payload
		case tfw.CmdSysInfo:
			s := binary.LittleEndian.AppendUint32(nil, 61000)
			s = binary.LittleEndian.AppendUint16(s, 2048)
			s = append(s, byte(tfw.ResetCause(1)))
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
		sysRow(t, dev, tfw.ResetCause(1), false)
	})
}

// A reset peer reports clean counters, and the SYS row still requires them.
func TestSysRowRequiresCleanCountersAfterAReset(t *testing.T) {
	sysRow(t, statePeer(t, 0, 0), tfw.ResetCause(1), true)
}
