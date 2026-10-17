package tfw

import (
	"errors"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// A negative timeout under one microsecond truncates to 0 µs, a different
// value the device accepts. docs/host-sdk/engineering.md refuses that
// conversion locally, as it refuses -1 µs.
func TestPulseTimeoutNegativeUnderAMicrosecondIsRefused(t *testing.T) {
	for _, timeout := range []time.Duration{-500 * time.Nanosecond, -time.Nanosecond} {
		for _, cmd := range []Cmd{CmdPulseIn, CmdPulseCapture} {
			_, err := pulseTimeout(cmd, timeout)
			var re *ArgRangeError
			require.True(t, errors.As(err, &re), "pulseTimeout(%v, %v): %v", cmd, timeout, err)
			assert.Equal(t, cmd, re.Cmd)
			assert.Equal(t, "timeout", re.Arg)
		}
	}
}
