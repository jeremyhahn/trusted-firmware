// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// A corrupted frame is answered TFW_ERR_CRC under its own SEQ; a LEN above
// TFW_MAX_LEN or below TFW_LEN_MIN is dropped unanswered, and the byte that
// failed as LEN is tried again as a start byte; garbage between frames is
// dropped. Each time the next good frame is the next one answered, and
// SYS_INFO's framer counters say what happened (docs/wire-protocol/
// architecture.md "Resync"). A truncated frame is the C suite's
// (test_transport.c's idle window, test_transport_mutate.c's every truncation):
// abandoning one takes TFW_FRAME_IDLE_MS of real time, and nothing here sleeps.
func TestSimFramingRejectsAndResynchronises(t *testing.T) {
	d, p, _ := startRaw(t, simproc.Software)

	ping := func(seq Seq, msg string) []byte { return rawRequest(t, seq, CmdPing, []byte(msg)) }
	answered := func(seq Seq, msg string) {
		t.Helper()
		f := p.reply()
		require.Equal(t, seq, f.Seq)
		require.Equal(t, byte(CmdPing)|0x80, f.Op)
		require.Equal(t, StatusOK, f.Status)
		require.Equal(t, []byte(msg), f.Payload)
	}

	bad := ping(0x31, "abc")
	bad[5] ^= 0x01 // a payload byte
	p.send(bad)
	f := p.reply()
	assert.Equal(t, Seq(0x31), f.Seq)
	assert.Equal(t, byte(CmdPing)|0x80, f.Op)
	assert.Equal(t, StatusErrCRC, f.Status)
	assert.Empty(t, f.Payload)

	// A lone start byte before a real frame: the real SOF (0xA5, 165) is read
	// as an over-length LEN, refused, and reprocessed as the frame's start.
	p.send(append([]byte{SOF}, ping(0x32, "over")...))
	answered(0x32, "over")

	// LEN 1, below the minimum, then the frame.
	p.send(append([]byte{SOF, 0x01}, ping(0x33, "under")...))
	answered(0x33, "under")

	// Garbage with no start byte in it, between two frames.
	garbage := bytes.Repeat([]byte{0x00, 0xFF, 0x5A, 0x81, 0x13}, 40)
	p.send(append(append(ping(0x34, "one"), garbage...), ping(0x35, "two")...))
	answered(0x34, "one")
	answered(0x35, "two")

	si, err := d.SysInfo(context.Background())
	require.NoError(t, err)
	assert.Equal(t, uint16(1), si.ErrCRC)
	assert.Equal(t, uint16(2), si.ErrFormat)
}
