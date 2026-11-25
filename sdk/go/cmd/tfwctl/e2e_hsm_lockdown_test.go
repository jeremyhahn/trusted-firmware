// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// R1 and R4 of the self-defence row end to end on the element simulator,
// which holds no policy set: a reported signal runs the built-in lockdown
// (src/core/hsm_engine.c lockdown), and a verdict with no signal is one
// decision entry that acts on nothing. The simulator's port answers TFW_OK
// to every action (tools/tfwsim/hsm_port.c), so every result is 0.

func TestSimHSMReportWithNoSetHeldLocksDownAndRecordsIt(t *testing.T) {
	dev := tfwsim.StartElement(t)
	st := cliHSMState(t, dev, "hsm-report", "ima")
	assert.True(t, st.Locked, "R1: a signal with no set held locks down")
	assert.False(t, st.SetHeld)
	assert.False(t, st.Wiped)

	var r hsmRecordJSON
	cliJSON(t, dev, &r, "hsm-records")
	assert.Equal(t, uint8(5), r.Count)
	assert.Equal(t, uint16(0), r.Dropped)
	// The signal, then PHY_OFF, POWER_OFF, KEYS_LOCK and RECORD (src/core/hsm.h).
	want := []hsmEntryJSON{
		{Index: 0, Kind: "signal", What: 4},
		{Index: 1, Kind: "action", What: 1},
		{Index: 2, Kind: "action", What: 2},
		{Index: 3, Kind: "action", What: 3},
		{Index: 4, Kind: "action", What: 6},
	}
	got := make([]hsmEntryJSON, len(r.Entries))
	for i, e := range r.Entries {
		e.At = 0
		got[i] = e
	}
	assert.Equal(t, want, got)

	// R6: the lockdown is latched.
	assert.True(t, cliHSMState(t, dev, "hsm-heartbeat").Locked)
}

func TestSimHSMVerdictAloneIsRecordedAndLocksNothing(t *testing.T) {
	dev := tfwsim.StartElement(t)
	st := cliHSMState(t, dev, "hsm-verdict", "compromised")
	assert.False(t, st.Locked, "R4: a verdict with no signal acts on nothing")
	assert.False(t, st.Wiped)

	var r hsmRecordJSON
	cliJSON(t, dev, &r, "hsm-records")
	assert.Equal(t, uint8(1), r.Count)
	assert.Equal(t, uint16(0), r.Dropped)
	got := make([]hsmEntryJSON, len(r.Entries))
	for i, e := range r.Entries {
		e.At = 0
		got[i] = e
	}
	assert.Equal(t, []hsmEntryJSON{{Index: 0, Kind: "decision", What: 1}}, got)
}
