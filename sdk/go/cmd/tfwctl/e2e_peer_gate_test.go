// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"fmt"
	"runtime"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The gates a row runs on its peer fail the row when the peer cannot serve it,
// naming what is missing and the peer: a row that cannot run is a failure, not
// a pass (#7). Each is run here with a TB that records the failure, on the
// fake device or a simulator without the feature.

// rowTB records a row's failure instead of failing the test. Goexit mirrors
// what the real FailNow does: the row's goroutine does not return.
type rowTB struct {
	testing.TB
	msg string
}

func (f *rowTB) Helper()                           {}
func (f *rowTB) Errorf(format string, args ...any) { f.msg += fmt.Sprintf(format, args...) }
func (f *rowTB) FailNow()                          { runtime.Goexit() }

func (f *rowTB) Fatal(args ...any) {
	f.msg += fmt.Sprint(args...)
	runtime.Goexit()
}

func (f *rowTB) Fatalf(format string, args ...any) {
	f.Errorf(format, args...)
	runtime.Goexit()
}

// rowFailure runs row with a rowTB and returns what it failed with: empty
// when it passed.
func rowFailure(t *testing.T, row func(testing.TB)) string {
	t.Helper()
	f := &rowTB{TB: t}
	done := make(chan struct{})
	go func() {
		defer close(done)
		row(f)
	}()
	<-done
	return f.msg
}

func TestRequireFeatureFailsARowWhosePeerLacksIt(t *testing.T) {
	dev := startFakeDevice(t, (&benchDevice{noFeatures: true}).answer)
	msg := rowFailure(t, func(tb testing.TB) { requireFeature(tb, dev, tfw.FeatureAuthSession) })
	assert.Contains(t, msg, fmt.Sprintf("this row needs CAPS feature auth (%#x)", tfw.FeatureAuthSession))
	assert.Contains(t, msg, peerName(t))
}

func TestRequireFeaturePassesARowWhosePeerReportsIt(t *testing.T) {
	dev := startFakeDevice(t, (&benchDevice{}).answer)
	assert.Empty(t, rowFailure(t, func(tb testing.TB) { requireFeature(tb, dev, tfw.FeatureAuthSession) }))
}

func TestRequireKnownFailsARowWhosePeerDoesNotKnowTheCommand(t *testing.T) {
	dev := startFakeDevice(t, (&benchDevice{refuse: map[tfw.Cmd]tfw.Status{tfw.CmdAppInfo: tfw.StatusErrUnknownCmd}}).answer)
	msg := rowFailure(t, func(tb testing.TB) { requireKnown(tb, dev, tfw.CmdAppInfo, nil, "an application (APP_INFO)") })
	assert.Contains(t, msg, "this row needs an application (APP_INFO)")
	assert.Contains(t, msg, peerName(t))
}

func TestRequireKnownLeavesAnotherRefusalToTheRow(t *testing.T) {
	// A device that knows the command and refuses it for another reason has
	// it: that refusal is the row's to check.
	dev := startFakeDevice(t, (&benchDevice{refuse: map[tfw.Cmd]tfw.Status{tfw.CmdAppInfo: tfw.StatusErrNotProvisioned}}).answer)
	assert.Empty(t, rowFailure(t, func(tb testing.TB) { requireKnown(tb, dev, tfw.CmdAppInfo, nil, "an application (APP_INFO)") }))
}

func TestStartServingFailsOnAPeerWithoutTheFeature(t *testing.T) {
	// The software tier issues no alias certificate (bit 13): certServing's
	// gate, and so every certificate row's, on a peer that lacks it.
	msg := rowFailure(t, func(tb testing.TB) { startServing(tb, tfw.FeatureAliasCert, simproc.Software) })
	assert.Contains(t, msg, fmt.Sprintf("this row needs CAPS feature attest-cert (%#x)", tfw.FeatureAliasCert))
	assert.Contains(t, msg, peerName(t))
}

// Each AVR board's list agrees with its rows' own gates: against a peer
// reporting the board's CAPS, every row the board runs passes requireFeature
// for each bit it needs and requireLacks for each bit it refuses.
func TestEachAVRBoardsRowsPassTheirGatesOnItsCaps(t *testing.T) {
	rows, err := firmwareRows(".")
	require.NoError(t, err)
	for board, img := range boardImages {
		dev := startFakeDevice(t, (&benchDevice{features: img.caps}).answer)
		selected := 0
		for _, r := range rows {
			need := firmwareRowNeeds[r]
			if !need.servedBy(img) {
				continue
			}
			selected++
			for bit := uint32(1); bit != 0; bit <<= 1 {
				if need.caps&bit != 0 {
					assert.Empty(t, rowFailure(t, func(tb testing.TB) { requireFeature(tb, dev, bit) }), "%s: %s", board, r)
				}
				if need.lacks&bit != 0 {
					assert.Empty(t, rowFailure(t, func(tb testing.TB) { requireLacks(tb, dev, bit) }), "%s: %s", board, r)
				}
			}
		}
		assert.NotZero(t, selected, "%s runs no row", board)
	}
}

func TestRequireLacksFailsARowWhosePeerReportsIt(t *testing.T) {
	dev := startFakeDevice(t, (&benchDevice{}).answer)
	msg := rowFailure(t, func(tb testing.TB) { requireLacks(tb, dev, tfw.FeatureAuthSession) })
	assert.Contains(t, msg, "without CAPS feature")
	assert.Contains(t, msg, "the simulator")
}

func TestRefusesAbsentFamilyPassesOnAnUnknownCommand(t *testing.T) {
	b := &benchDevice{noFeatures: true, refuse: map[tfw.Cmd]tfw.Status{tfw.CmdI2CXfer: tfw.StatusErrUnknownCmd}}
	dev := startFakeDevice(t, b.answer)
	assert.Empty(t, rowFailure(t, func(tb testing.TB) { refusesAbsentFamily(tb, dev, tfw.FeatureI2C, tfw.CmdI2CXfer) }))
}

// A peer without the family in CAPS that still answers its command fails
// the row, as does one that refuses it with another status.
func TestRefusesAbsentFamilyFailsOnAnyOtherAnswer(t *testing.T) {
	for name, b := range map[string]*benchDevice{
		"answered": {noFeatures: true},
		"refused":  {noFeatures: true, refuse: map[tfw.Cmd]tfw.Status{tfw.CmdI2CXfer: tfw.StatusErrBadArgc}},
	} {
		dev := startFakeDevice(t, b.answer)
		msg := rowFailure(t, func(tb testing.TB) { refusesAbsentFamily(tb, dev, tfw.FeatureI2C, tfw.CmdI2CXfer) })
		assert.NotEmpty(t, msg, name)
	}
}
