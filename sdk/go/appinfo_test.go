// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

func TestAppInfoVectorsAreTheWholeReply(t *testing.T) {
	for _, v := range []string{
		vectors.AppInfoHeartbeatReply,
		vectors.AppInfoOverrunReply,
		vectors.AppInfoRefusedReply,
	} {
		assert.Len(t, v, appInfoLen)
	}
}

func TestAppInfoParsesTheExample(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.AppInfoHeartbeatReply)}
	ai, err := New(link).AppInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, &AppInfo{
		State:        AppRunning,
		ABI:          1,
		VersionMajor: 1,
		RequiredCaps: 1 << 0,
		Name:         "heartbeat",
	}, ai)
}

func TestAppInfoParsesTheOverrunCount(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.AppInfoOverrunReply)}
	ai, err := New(link).AppInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, AppRunning, ai.State)
	assert.Equal(t, uint16(0x0102), ai.Overruns)
	assert.Equal(t, uint16(16), ai.ConfigLen)
	assert.Equal(t, "mock", ai.Name)
	assert.Empty(t, ai.Profile)
}

// Every multi-byte field holds two different bytes, so a field read at the
// wrong offset or in the wrong order cannot come out right.
func TestAppInfoParsesARefusedDescriptorAsWritten(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.AppInfoRefusedReply)}
	ai, err := New(link).AppInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, &AppInfo{
		State:        AppUnsupported,
		ABI:          1,
		OTASlots:     2,
		VersionMajor: 2,
		VersionMinor: 3,
		VersionPatch: 1029,
		ConfigLen:    0x0102,
		RequiredCaps: 1<<31 | 1<<0,
		Name:         "abcdefghijklmno",
		Profile:      "bmc/esp32-p4",
	}, ai)
}

func TestAppInfoReadsAFieldWithNoTerminator(t *testing.T) {
	p := []byte(vectors.AppInfoHeartbeatReply)
	copy(p[appInfoNameOff:appInfoProfileOff], "0123456789abcdef")
	link := &cannedLink{payload: p}
	ai, err := New(link).AppInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, "0123456789abcdef", ai.Name)
	assert.Empty(t, ai.Profile)
}

func TestAppInfoRejectsAShortReply(t *testing.T) {
	link := &cannedLink{payload: []byte(vectors.AppInfoHeartbeatReply)[:appInfoLen-1]}
	_, err := New(link).AppInfo(context.Background())

	var se *ShortReplyError
	require.ErrorAs(t, err, &se)
	assert.Equal(t, CmdAppInfo, se.Cmd)
	assert.Equal(t, appInfoLen-1, se.Got)
	assert.Equal(t, appInfoLen, se.Need)
}

func TestAppInfoFromADeviceWithNoApplication(t *testing.T) {
	link := &cannedLink{status: StatusErrUnknownCmd}
	_, err := New(link).AppInfo(context.Background())
	assert.True(t, errors.Is(err, StatusErrUnknownCmd), "err = %v", err)
}

func TestAppStateNames(t *testing.T) {
	for s, want := range map[AppState]string{
		AppIdle:        "idle",
		AppRunning:     "running",
		AppFailed:      "failed",
		AppUnsupported: "unsupported",
		AppStopped:     "stopped",
		AppState(5):    "unknown",
	} {
		assert.Equal(t, want, s.String())
	}
}
