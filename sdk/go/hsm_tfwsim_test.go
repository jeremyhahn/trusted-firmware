// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The HSM self-defence channel end to end (docs/hsm-self-defence/ slice 4):
// HSM_HEARTBEAT to HSM_LOAD through this SDK against tfwsim-atecc608a, the one
// simulator built with TFW_FEATURE_HSM, whose port verifies under the literal
// tenant key. A missing binary fails the test and names `make tfwsim-atecc608a`.
//
// The heartbeat period is one second of real time and the simulator's clock is
// the host's, so a slow host may raise S6 between any two messages. Nothing
// here waits a period out: a case asserts the set-held flag, which S6 never
// clears, and the lockdown flag only where the case itself locks down.

// hsmPolicyEnvelope is testdata/suit_vectors.h SUIT_POLICY_ENVELOPE_CLASS_328P:
// the D5 policy set (one policy, S1: [keys-lock, shutdown]) integrated in a
// SUIT envelope of class UUIDv5(vendor, "atmega328p/policy"), the class of the
// board tfwsim is built for, signed by the throwaway tenant key. A literal, as
// the wire vectors are.
var hsmPolicyEnvelope = []byte{
	0xd8, 0x6b, 0xa3, 0x02, 0x58, 0x73, 0x82, 0x58, 0x24, 0x82, 0x2f, 0x58,
	0x20, 0x6c, 0x84, 0xca, 0xc8, 0x22, 0x30, 0x84, 0xd8, 0x77, 0xd7, 0x9c,
	0x39, 0xf3, 0xca, 0x03, 0xc4, 0x0b, 0x48, 0x64, 0x42, 0xee, 0xa9, 0xf3,
	0x00, 0xb6, 0x56, 0xe4, 0x45, 0x38, 0x60, 0x88, 0x3d, 0x58, 0x4a, 0xd2,
	0x84, 0x43, 0xa1, 0x01, 0x26, 0xa0, 0xf6, 0x58, 0x40, 0x1d, 0x89, 0x47,
	0xf0, 0xe8, 0x16, 0x18, 0x77, 0x01, 0xaa, 0x15, 0xe9, 0x36, 0x45, 0xa0,
	0x72, 0x77, 0xff, 0x9b, 0x92, 0xfc, 0x7b, 0xe5, 0xd4, 0xda, 0x1e, 0x55,
	0xf7, 0x8e, 0xf2, 0x99, 0x5e, 0x6c, 0xba, 0xce, 0xcf, 0x83, 0xd0, 0x2d,
	0x50, 0xae, 0x34, 0xd1, 0x64, 0xd5, 0xe1, 0x62, 0x07, 0xd8, 0x13, 0xff,
	0xe6, 0x44, 0x1d, 0x8c, 0x8d, 0xc6, 0x6f, 0xd4, 0x29, 0xda, 0xbd, 0xe2,
	0x00, 0x03, 0x58, 0x73, 0xa4, 0x01, 0x01, 0x02, 0x03, 0x03, 0x58, 0x59,
	0xa2, 0x02, 0x81, 0x81, 0x41, 0x00, 0x04, 0x58, 0x50, 0x82, 0x14, 0xa4,
	0x01, 0x50, 0xf5, 0xce, 0x00, 0x48, 0x68, 0xe5, 0x5f, 0xcf, 0xb7, 0x9b,
	0x5b, 0x1c, 0xff, 0xbf, 0x22, 0xac, 0x02, 0x50, 0xd0, 0xa2, 0x3e, 0x68,
	0x55, 0xd5, 0x51, 0xe3, 0xba, 0xb3, 0x4e, 0xd3, 0x29, 0x50, 0x9d, 0x68,
	0x03, 0x58, 0x24, 0x82, 0x2f, 0x58, 0x20, 0xc0, 0xe3, 0xa1, 0x14, 0x45,
	0x9d, 0xde, 0x65, 0x0b, 0xdf, 0x20, 0xae, 0x83, 0x4c, 0xa8, 0x1b, 0x53,
	0x5a, 0x4a, 0xcb, 0x4a, 0x99, 0x5f, 0xb5, 0x53, 0xfb, 0xc1, 0x00, 0x53,
	0xf3, 0x73, 0x8e, 0x0e, 0x16, 0x14, 0x50, 0x86, 0x14, 0xa1, 0x15, 0x67,
	0x23, 0x70, 0x6f, 0x6c, 0x69, 0x63, 0x79, 0x15, 0x0f, 0x03, 0x0f, 0x67,
	0x23, 0x70, 0x6f, 0x6c, 0x69, 0x63, 0x79, 0x56, 0xa5, 0x01, 0x01, 0x02,
	0x81, 0x81, 0x83, 0x01, 0x82, 0x03, 0x05, 0xf4, 0x03, 0xa0, 0x04, 0x19,
	0x03, 0xe8, 0x05, 0x19, 0x01, 0xf4,
}

func startHSM(t *testing.T) *Device {
	t.Helper()
	require.Len(t, hsmPolicyEnvelope, 270)
	return New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
}

// heldSet loads the policy set and requires the device to hold it.
func heldSet(t *testing.T, d *Device) {
	t.Helper()
	st, err := d.HSMLoad(context.Background(), hsmPolicyEnvelope)
	require.NoError(t, err)
	require.NotZero(t, st.Flags&HSMSetHeld, "the verified set is not held: %+v", st)
}

func TestSimHSMLoadHoldsTheVerifiedSet(t *testing.T) {
	heldSet(t, startHSM(t))
}

func TestSimHSMLoadRefusesAFlippedSignature(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()

	env := bytes.Clone(hsmPolicyEnvelope)
	env[57] ^= 0x01 // a byte of the COSE_Sign1 signature
	_, err := d.HSMLoad(ctx, env)
	require.Equal(t, StatusErrCrypto, asStatus(t, err))

	st, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld, "a refused set is held: %+v", st)
	assert.Equal(t, uint32(1), st.Refused)
}

// The SDK refuses an envelope it knows the device cannot hold, so the device's
// own answer is asked for with the raw first chunk: a total one past the buffer.
func TestSimHSMLoadRefusesATotalPastTheBuffer(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()

	p := make([]byte, 4, 4+HSMLoadChunk)
	binary.LittleEndian.PutUint16(p[0:2], HSMEnvelopeMax+1)
	p = append(p, hsmPolicyEnvelope[:HSMLoadChunk]...)
	_, err := d.Do(ctx, CmdHSMLoad, p)
	require.Equal(t, StatusErrRange, asStatus(t, err))

	st, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
	assert.Zero(t, st.Flags&HSMSetHeld)
	assert.Equal(t, uint32(1), st.Refused)
}

func TestSimHSMSelectNamesOnlyAPolicyOfTheSet(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()
	heldSet(t, d)

	st, err := d.HSMSelect(ctx, 0)
	require.NoError(t, err)
	assert.NotZero(t, st.Flags&HSMSetHeld)
	assert.Equal(t, uint8(0), st.Selected)

	_, err = d.HSMSelect(ctx, 1)
	require.Equal(t, StatusErrRange, asStatus(t, err))
}

// R2: the set names S1 alone, so the A55's S4 is a signal it does not name and
// the engine locks down. A late heartbeat (S6) locks down as well, so the
// lockdown flag holds whatever the host's clock did; the case asserts it once,
// after the report.
func TestSimHSMReportOfS4LocksDown(t *testing.T) {
	d := startHSM(t)
	ctx := context.Background()
	heldSet(t, d)

	st, err := d.HSMVerdict(ctx, HSMVerdictClear)
	require.NoError(t, err)
	assert.NotZero(t, st.Flags&HSMSetHeld)

	st, err = d.HSMHeartbeat(ctx)
	require.NoError(t, err)
	assert.NotZero(t, st.Flags&HSMSetHeld)

	st, err = d.HSMReport(ctx, HSMSignalIMAViolation)
	require.NoError(t, err)
	assert.NotZero(t, st.Flags&HSMSetHeld)
	assert.NotZero(t, st.Flags&HSMLocked, "S4 did not lock down: %+v", st)
}
