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

// The I2C row on a QEMU MPS2 board (docs/cortex-m/README.md): the image is the
// base row's built with I2C=1, so I2C_XFER reaches the shield 0 SBCon through
// tfwctl's i2c verb.
//
// The answers are literals, derived from QEMU v10.0.0's model and not from
// this firmware. hw/arm/mps2-tz.c (Shield0 at 0x4020c000) and hw/arm/mps2.c
// (Shield0 at 0x40029000) create the SBCon and attach nothing to its I2C bus.
// hw/i2c/arm_sbcon_i2c.c drives hw/i2c/bitbang_i2c.c with the two lines the
// guest writes; on the ninth clock of the address byte bitbang_i2c asks
// hw/i2c/core.c's i2c_start_transfer for a device at the address, which finds
// none and fails, so the model holds SDA high: a NACK, then STOPPED. The
// master reads the NACK and returns ERR_NACK after a STOP, whether the
// transfer starts with a write or a read, at any clock.
//
// 0x60 is the element's address (config.h TFW_ELEMENT_I2C_ADDR), refused as
// PROTECTED_REGION before the bus is touched; 0x80 is not a 7-bit address and
// is refused as RANGE.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUI2CXfer`).
func TestQEMUI2CXfer(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose I2C row starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeatureI2C), "CAPS: the I2C=1 image reports I2C")

	status := func(what string, args ...string) tfw.Status {
		t.Helper()
		err := run(ctx, dev, "i2c", args, false, &strings.Builder{})
		var st tfw.Status
		require.True(t, errorsAsStatus(err, &st), "%s: %v, want a device status", what, err)
		return st
	}

	require.Equal(t, tfw.StatusErrNACK, status("I2C_XFER write at boot's clock", "0x50", "a5", "0"),
		"I2C_XFER write: QEMU's SBCon has nothing on its bus")
	require.Equal(t, tfw.StatusErrNACK, status("I2C_XFER read at boot's clock", "0x50", "-", "4"),
		"I2C_XFER read: QEMU's SBCon has nothing on its bus")

	require.NoError(t, run(ctx, dev, "i2c-config", []string{"400000"}, false, &strings.Builder{}),
		"I2C_CONFIG 400 kHz")
	require.Equal(t, tfw.StatusErrNACK, status("I2C_XFER write and read at 400 kHz", "0x50", "00", "2"),
		"I2C_XFER at 400 kHz: the bus is still empty")

	require.Equal(t, tfw.StatusErrProtectedRegion, status("I2C_XFER at 0x60", "0x60", "-", "1"),
		"I2C_XFER at the element's address")
	require.Equal(t, tfw.StatusErrRange, status("I2C_XFER at 0x80", "0x80", "-", "1"),
		"I2C_XFER past the 7-bit range")
}
