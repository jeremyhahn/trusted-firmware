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

// The tone pin on both MPS2 boards: pin 1, LED1, the FPGA I/O block's LED
// bit 1 (TFW_TONE_PIN in boards/mps2-an505/board_profile.h and
// boards/mps2-an500/board_profile.h).
const qemuTonePin = "1"

// The tone row on a QEMU MPS2 board: the image is the base row's built with
// TONE=1, so TONE and NOTONE reach src/hal/cortex-m/mps2/hal_tone.c, which
// runs the CMSDK timer TIMER0 and toggles the pin from its interrupt.
//
// QEMU does not measure the pin, so the row checks the answers only: a tone in
// range and its stop are OK, and a frequency above the driver's bound (a half
// period under 500 core cycles, 20 kHz at the AN505's 20 MHz and 25 kHz at the
// AN500's 25 MHz) is refused with StatusErrRange.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-tone-wire`).
func TestQEMUTone(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make cortex-m-qemu-tone-wire`, which starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeatureTone), "CAPS: the TONE=1 image reports TONE")

	verb := func(name string, args ...string) (string, error) {
		t.Helper()
		var out strings.Builder
		err := run(ctx, dev, name, args, false, &out)
		return out.String(), err
	}

	got, err := verb("tone", qemuTonePin, "1000")
	require.NoError(t, err, "TONE 1 kHz")
	require.Equal(t, "pin 1 <- 1000 Hz\n", got)

	got, err = verb("tone", qemuTonePin, "440")
	require.NoError(t, err, "TONE 440 Hz: a second start retargets")
	require.Equal(t, "pin 1 <- 440 Hz\n", got)

	got, err = verb("notone", qemuTonePin)
	require.NoError(t, err, "NOTONE")
	require.Equal(t, "pin 1 tone off\n", got)

	_, err = verb("tone", qemuTonePin, "65535")
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "TONE 65535 Hz: %v, want a device status", err)
	require.Equal(t, tfw.StatusErrRange, st, "TONE 65535 Hz: above the driver's bound")
}
