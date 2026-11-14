// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"context"
	"strings"
	"testing"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The PWM pins on both MPS2 boards in the PWM=1 row: pin 0, LED0, the first
// row of the PWM table (boards/mps2-an505/board.c, boards/mps2-an500/board.c).
// Pin 2, the shield 0 SPI select, has no PWM row.
const (
	qemuPWMPin   = "0"
	qemuPWMNoPin = "2"
)

// The PWM row on a QEMU MPS2 board: the image is the base row's built with
// PWM=1, so PWM_WRITE reaches the software PWM on the CMSDK dual timer
// (src/hal/cortex-m/mps2/hal_pwm.c) through tfwctl's pwm verb. QEMU is not
// asked to measure the pin: the LED is an FPGA I/O register bit with no
// timing visible from here, so the test checks the answers only, at 0 and 255
// (the pin driven, the timer left alone) and 128 (the timer running).
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUPWMWrite`).
func TestQEMUPWMWrite(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make cortex-m-qemu-pwm-wire`, which starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeaturePWM), "CAPS: the PWM=1 image reports PWM")

	pwm := func(pin, duty string) error {
		t.Helper()
		return run(ctx, dev, "pwm", []string{pin, duty}, false, &strings.Builder{})
	}

	for _, duty := range []string{"0", "128", "255", "128", "0"} {
		require.NoError(t, pwm(qemuPWMPin, duty), "PWM_WRITE pin %s duty %s", qemuPWMPin, duty)
	}

	err = pwm(qemuPWMNoPin, "128")
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "PWM_WRITE on pin %s: %v, want a device status", qemuPWMNoPin, err)
	require.Equal(t, tfw.StatusErrUnsupported, st, "PWM_WRITE on pin %s: no PWM row", qemuPWMNoPin)
}
