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

		_, err = d.PulseCapture(context.Background(), 2, true, 8, timeout)
		require.True(t, errors.As(err, &re), "PulseCapture(%v): %v", timeout, err)
		assert.Equal(t, CmdPulseCapture, re.Cmd)
	}
}

func TestPulseTimeoutAtTheWireFieldsBoundsIsCarried(t *testing.T) {
	for _, tc := range []struct {
		timeout time.Duration
		want    uint32
	}{
		{0, 0},
		{time.Second, 1_000_000},
		{math.MaxUint32 * time.Microsecond, math.MaxUint32},
	} {
		got, err := pulseTimeout(CmdPulseIn, tc.timeout)
		require.NoError(t, err)
		assert.Equal(t, tc.want, got)
	}
}
