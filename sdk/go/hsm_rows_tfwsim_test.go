// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The rules of row hsm-self-defence (docs/hsm-self-defence/architecture.md §5)
// end to end over the channel, against tfwsim-atecc608a, on the literal
// hsmPolicyEnvelope of hsm_tfwsim_test.go. The heartbeat caveat there holds
// here: a late heartbeat locks down too, so a case asserts the lockdown flag
// only where the case itself locks down, and never asserts its absence.

// R1: with no set held, a signal locks down under the built-in default, which
// never wipes.
func TestSimHSMReportWithNoSetLocksDownAndNeverWipes(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()

	st, err := d.HSMVerdict(ctx, HSMVerdictCompromised)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld)

	st, err = d.HSMReport(ctx, HSMSignalHotplug)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld)
	assert.NotZero(t, st.Flags&HSMLocked, "S5 with no set did not lock down: %+v", st)
	assert.Zero(t, st.Flags&HSMWiped, "the built-in default wiped: %+v", st)
}

// R6: lockdown is latched. No later verdict, selection, set or heartbeat
// lowers it, and the set loaded again is still held under the latch.
func TestSimHSMLockdownIsLatchedOverTheChannel(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()
	heldSet(t, d)

	st, err := d.HSMReport(ctx, HSMSignalIMAViolation)
	require.NoError(t, err)
	require.NotZero(t, st.Flags&HSMLocked, "S4 did not lock down: %+v", st)

	steps := []struct {
		name string
		do   func() (*HSMState, error)
	}{
		{"verdict clear", func() (*HSMState, error) { return d.HSMVerdict(ctx, HSMVerdictClear) }},
		{"select 0", func() (*HSMState, error) { return d.HSMSelect(ctx, 0) }},
		{"heartbeat", func() (*HSMState, error) { return d.HSMHeartbeat(ctx) }},
		{"the set again", func() (*HSMState, error) { return d.HSMLoad(ctx, hsmPolicyEnvelope) }},
	}
	for _, s := range steps {
		st, err := s.do()
		require.NoError(t, err, s.name)
		assert.NotZero(t, st.Flags&HSMSetHeld, "%s: %+v", s.name, st)
		assert.NotZero(t, st.Flags&HSMLocked, "%s lowered the lockdown: %+v", s.name, st)
		assert.Zero(t, st.Flags&HSMWiped, "%s: %+v", s.name, st)
	}
}

// R7: a refused envelope leaves the previous set and its selection, and is
// counted.
func TestSimHSMARefusedEnvelopeKeepsTheHeldSet(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()
	heldSet(t, d)

	env := bytes.Clone(hsmPolicyEnvelope)
	env[57] ^= 0x01 // a byte of the COSE_Sign1 signature
	_, err := d.HSMLoad(ctx, env)
	require.Equal(t, StatusErrCrypto, asStatus(t, err))

	st, err := d.HSMSelect(ctx, 0)
	require.NoError(t, err)
	assert.NotZero(t, st.Flags&HSMSetHeld, "the refused envelope dropped the set: %+v", st)
	assert.Equal(t, uint8(0), st.Selected)
	assert.Equal(t, uint32(1), st.Refused)
}

// The device's own refusals, asked for with raw frames the SDK would not send:
// a signal the A55 does not observe (S1), a verdict past compromised, and a
// selection with no set are TFW_ERR_RANGE and leave the engine as it was.
func TestSimHSMDeviceRefusesWhatTheA55MayNotSend(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()

	for _, c := range []struct {
		name string
		cmd  Cmd
		arg  byte
	}{
		{"report S1", CmdHSMReport, 1},
		{"report S6", CmdHSMReport, 6},
		{"verdict past compromised", CmdHSMVerdict, HSMVerdictCompromised + 1},
		{"select with no set", CmdHSMSelect, 0},
	} {
		_, err := d.Do(ctx, c.cmd, []byte{c.arg})
		require.Equal(t, StatusErrRange, asStatus(t, err), c.name)
	}

	st, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld)
	assert.Zero(t, st.Flags&HSMWiped)
	assert.Equal(t, uint32(0), st.Refused)
}

// HSM_LOAD reassembly on the device: a chunk out of sequence drops the pending
// envelope with TFW_ERR_RANGE and holds nothing; offset 0 begins again.
func TestSimHSMLoadChunkOutOfSequenceDropsTheEnvelope(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()

	chunk := func(off int) []byte {
		p := make([]byte, 4, 4+HSMLoadChunk)
		binary.LittleEndian.PutUint16(p[0:2], uint16(len(hsmPolicyEnvelope)))
		binary.LittleEndian.PutUint16(p[2:4], uint16(off))
		return append(p, hsmPolicyEnvelope[off:off+HSMLoadChunk]...)
	}

	_, err := d.Do(ctx, CmdHSMLoad, chunk(0))
	require.NoError(t, err)
	_, err = d.Do(ctx, CmdHSMLoad, chunk(2*HSMLoadChunk))
	require.Equal(t, StatusErrRange, asStatus(t, err))
	// The dropped envelope does not resume where it stopped.
	_, err = d.Do(ctx, CmdHSMLoad, chunk(HSMLoadChunk))
	require.Equal(t, StatusErrRange, asStatus(t, err))

	st, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld)

	heldSet(t, d)
}
