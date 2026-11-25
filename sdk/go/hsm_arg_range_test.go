package tfw

import (
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Every verdict past HSMVerdictCompromised is refused before the wire, not
// only the first one, and the refusal reports the range 0..HSMVerdictCompromised.
func TestHSMVerdictRefusesEveryVerdictPastCompromised(t *testing.T) {
	for _, v := range []byte{2, 3, 0x7F, 0xFF} {
		link := &captureLink{}
		_, err := New(link).HSMVerdict(context.Background(), v)
		var ae *ArgRangeError
		require.True(t, errors.As(err, &ae), "verdict %d: %v", v, err)
		assert.Equal(t, CmdHSMVerdict, ae.Cmd)
		assert.Equal(t, "verdict", ae.Arg)
		assert.Equal(t, int64(v), ae.Value)
		assert.Equal(t, int64(0), ae.Min)
		assert.Equal(t, int64(HSMVerdictCompromised), ae.Max)
		assert.Equal(t, "", ae.Unit)
		assert.Empty(t, link.sent, "verdict %d reached the wire", v)
	}
}

// The signal refusal names the two signals the A55 reports as its range.
func TestHSMReportRefusalNamesTheSignalsTheA55Reports(t *testing.T) {
	for _, sig := range []byte{0, 3, 6, 0xFF} {
		link := &captureLink{}
		_, err := New(link).HSMReport(context.Background(), sig)
		var ae *ArgRangeError
		require.True(t, errors.As(err, &ae), "signal %d: %v", sig, err)
		assert.Equal(t, CmdHSMReport, ae.Cmd)
		assert.Equal(t, "signal", ae.Arg)
		assert.Equal(t, int64(sig), ae.Value)
		assert.Equal(t, int64(HSMSignalIMAViolation), ae.Min)
		assert.Equal(t, int64(HSMSignalHotplug), ae.Max)
		assert.Equal(t, "", ae.Unit)
		assert.Empty(t, link.sent, "signal %d reached the wire", sig)
	}
}
