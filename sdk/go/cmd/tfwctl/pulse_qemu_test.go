// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The PULSE row on a QEMU MPS2 board (docs/cortex-m/README.md): the image is
// the base row's built with PULSE=1, so PULSE_IN and PULSE_CAPTURE run the
// Cortex-M port's SysTick timer (src/hal/cortex-m/hal_pulse.c).
//
// QEMU drives no pin: nothing outside the guest changes the FPGA I/O block's
// inputs, so the line never moves and every measurement times out. What the
// row proves is that the timer runs on the part's SysTick and gives up: each
// call answers TIMEOUT, and answers it inside its timeout plus the link's own
// round trip, not never. The pulse widths themselves are host-tested against
// a model SysTick (test/unit/test_cm_pulse.c).
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUPulseTimeout`).
func TestQEMUPulseTimeout(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose PULSE row starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeaturePulse), "CAPS: the PULSE=1 image reports PULSE")

	const timeout = 20 * time.Millisecond
	// Generous: QEMU's SysTick runs on its virtual clock, and the link adds
	// its own latency. A timer that never gave up would hit the link's
	// deadline instead.
	const bound = 2 * time.Second

	for _, high := range []bool{true, false} {
		start := time.Now()
		_, err := dev.PulseIn(ctx, 0, high, timeout)
		took := time.Since(start)
		var st tfw.Status
		require.True(t, errorsAsStatus(err, &st), "PULSE_IN high=%v: %v, want a device status", high, err)
		require.Equal(t, tfw.StatusErrTimeout, st, "PULSE_IN high=%v on an undriven pin", high)
		require.Less(t, took, bound, "PULSE_IN high=%v answered in %v", high, took)
	}

	// The line sits at one level, so capture lines up on it at once or never;
	// either way no edge follows and the capture is empty.
	for _, high := range []bool{true, false} {
		start := time.Now()
		_, err := dev.PulseCapture(ctx, 0, high, 8, timeout)
		took := time.Since(start)
		var st tfw.Status
		require.True(t, errorsAsStatus(err, &st), "PULSE_CAPTURE high=%v: %v, want a device status", high, err)
		require.Equal(t, tfw.StatusErrTimeout, st, "PULSE_CAPTURE high=%v on an undriven pin", high)
		require.Less(t, took, bound, "PULSE_CAPTURE high=%v answered in %v", high, took)
	}
}
