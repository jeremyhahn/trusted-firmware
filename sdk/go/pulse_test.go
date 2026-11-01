// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"math"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// A timeout the wire's uint32 of microseconds cannot carry is refused before
// anything is sent; cast, it would wrap into a timeout nobody asked for. The
// Device is nil: reaching the transport would panic, so a pass also proves
// nothing was sent.
func TestPulseTimeoutOutsideTheWireFieldIsRefused(t *testing.T) {
	var d *Device
	for _, timeout := range []time.Duration{-time.Microsecond, (math.MaxUint32 + 1) * time.Microsecond} {
		_, err := d.PulseIn(context.Background(), 2, true, timeout)
		var re *ArgRangeError
		require.True(t, errors.As(err, &re), "PulseIn(%v): %v", timeout, err)
		assert.Equal(t, CmdPulseIn, re.Cmd)
		assert.Equal(t, "timeout", re.Arg)
	}
}

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
