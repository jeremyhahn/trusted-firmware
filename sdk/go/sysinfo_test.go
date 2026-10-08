// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// SYS_INFO is the second reply whose LENGTH is part of the contract (#5,
// after CAPS): err_crc and err_format are appended, so this host must read a
// reply from firmware older than itself without failing and a reply from
// firmware at least as new as itself with both counters. tfwsim proves the SDK
// agrees with the firmware as it is TODAY; the older shape is one tfwsim
// cannot be, which is what testdata/vectors.h's TFWV_SYSINFO_OLD_REPLY is for.

func TestSysInfoParsesTheAppendedFramerCounters(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.SysInfoNewReply)}
	si, err := New(link).SysInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, 0x12345678*time.Millisecond, si.Uptime)
	assert.Equal(t, uint16(0x0321), si.FreeRAM)
	assert.Equal(t, ResetWatchdog, si.ResetCause)
	assert.Equal(t, uint16(5), si.ErrCRC)
	assert.Equal(t, uint16(7), si.ErrFormat)
}

// Firmware built before the counters existed sends seven bytes and is
// CORRECT: the reply is complete by the contract it was built against. A host
// that rejected it would take a fleet down on an upgrade of the host alone.
func TestSysInfoAcceptsTheSevenByteReplyOfOlderFirmware(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.SysInfoOldReply)}
	si, err := New(link).SysInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, 0x12345678*time.Millisecond, si.Uptime)
	assert.Equal(t, uint16(0x0321), si.FreeRAM)
	assert.Equal(t, ResetWatchdog, si.ResetCause)
	// Not reported, not zero-errors: there is no way to tell the two apart
	// from a reply this short, and the zero value is what a host that never
	// knew the field existed already assumed.
	assert.Equal(t, uint16(0), si.ErrCRC)
	assert.Equal(t, uint16(0), si.ErrFormat)
}

func TestSysInfoRejectsAReplyShorterThanTheOriginalShape(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.SysInfoOldReply)[:6]}
	_, err := New(link).SysInfo(context.Background())
	require.Error(t, err)

	var se *ShortReplyError
	require.ErrorAs(t, err, &se)
	assert.Equal(t, CmdSysInfo, se.Cmd)
	assert.Equal(t, 6, se.Got)
	assert.Equal(t, sysInfoLen, se.Need)
}
