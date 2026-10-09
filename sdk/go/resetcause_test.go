// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"testing"

	"github.com/stretchr/testify/assert"
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
}

func TestResetCauseStrings(t *testing.T) {
	assert.Equal(t, "unknown", ResetUnknown.String())
	assert.Equal(t, "power-on", ResetPowerOn.String())
	assert.Equal(t, "external", ResetExternal.String())
	assert.Equal(t, "brownout", ResetBrownout.String())
	assert.Equal(t, "watchdog", ResetWatchdog.String())
}

// A value a newer firmware defines and this SDK does not is read as
// unrecognised, never as a different known cause.
func TestAnUnknownResetCauseIsReadAsUnrecognised(t *testing.T) {
	assert.Equal(t, "unknown", ResetCause(5).String())
	assert.Equal(t, "unknown", ResetCause(200).String())
}
