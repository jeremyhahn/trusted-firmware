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

// ResetCause mirrors tfw_reset_cause_t (include/tfw/types.h), the
// architecture-neutral value SYS_INFO reports at byte 6 and tfw-boot's handoff
// carries at byte 76. The numbers are literals on purpose: they are the wire,
// they extend and never renumber, and a constant compared with itself would
// move with the code it is meant to pin.
func TestResetCauseValuesAreTheWireValues(t *testing.T) {
	assert.Equal(t, ResetCause(0), ResetUnknown)
	assert.Equal(t, ResetCause(1), ResetPowerOn)
	assert.Equal(t, ResetCause(2), ResetExternal)
	assert.Equal(t, ResetCause(3), ResetBrownout)
	assert.Equal(t, ResetCause(4), ResetWatchdog)
	assert.Equal(t, ResetCause(5), ResetSoftware)
	assert.Equal(t, ResetCause(6), ResetPanic)
}

func TestResetCauseStrings(t *testing.T) {
	assert.Equal(t, "unknown", ResetUnknown.String())
	assert.Equal(t, "power-on", ResetPowerOn.String())
	assert.Equal(t, "external", ResetExternal.String())
	assert.Equal(t, "brownout", ResetBrownout.String())
	assert.Equal(t, "watchdog", ResetWatchdog.String())
	assert.Equal(t, "software", ResetSoftware.String())
	assert.Equal(t, "panic", ResetPanic.String())
}

// The reset-cause values #48 appended arrive in SYS_INFO at byte 6, and the
// rest of the reply reads as it did.
func TestSysInfoReadsTheAppendedResetCauses(t *testing.T) {
	for _, c := range []struct {
		name    string
		payload string
		want    ResetCause
	}{
		{"software", vectors.SysInfoSoftwareReply, ResetSoftware},
		{"panic", vectors.SysInfoPanicReply, ResetPanic},
	} {
		si, err := New(&cannedLink{payload: []byte(c.payload)}).SysInfo(context.Background())
		require.NoError(t, err, c.name)
		assert.Equal(t, c.want, si.ResetCause, c.name)
		assert.Equal(t, uint32(0x12345678), uint32(si.Uptime/time.Millisecond), c.name)
		assert.Equal(t, uint16(5), si.ErrCRC, c.name)
	}
}

// A value a newer firmware defines and this SDK does not is read as
// unrecognised, never as a different known cause.
func TestAnUnknownResetCauseIsReadAsUnrecognised(t *testing.T) {
	assert.Equal(t, "unknown", ResetCause(7).String())
	assert.Equal(t, "unknown", ResetCause(200).String())
}
