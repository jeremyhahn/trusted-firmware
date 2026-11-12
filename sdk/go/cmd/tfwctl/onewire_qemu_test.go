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

// The ONEWIRE=1 row's line: pin 3, GPIO block 0 bit 0 (boards/mps2-*/board.c).
const qemuOneWirePin = "3"

// TestQEMUOneWire runs the 1-Wire verbs against the ONEWIRE=1 image on QEMU,
// whose machine leaves the CMSDK GPIO block unimplemented: every read of it
// returns 0, so the released line always reads low. The master's reset samples
// the line 70 us after it lets go, and a line held low there is a device's
// presence pulse, so QEMU's undriven value is "presence". The same reads make
// every read slot a 0 bit, so 1wire-read gives 0x00 bytes. Neither is a device;
// the test is that each verb runs its slots and comes back with OK.
func TestQEMUOneWire(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make cortex-m-qemu-onewire-wire`, which starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeatureOneWire), "CAPS: the ONEWIRE=1 image reports 1-Wire")

	verb := func(name string, args ...string) (string, error) {
		t.Helper()
		var out strings.Builder
		err := run(ctx, dev, name, args, false, &out)
		return out.String(), err
	}

	got, err := verb("1wire-reset", qemuOneWirePin)
	require.NoError(t, err, "1wire-reset")
	require.Equal(t, "presence\n", got, "1wire-reset: QEMU's unimplemented GPIO block reads the line low")

	got, err = verb("1wire-write", qemuOneWirePin, "cc44")
	require.NoError(t, err, "1wire-write")
	require.Equal(t, "wrote 2 bytes\n", got)

	got, err = verb("1wire-read", qemuOneWirePin, "9")
	require.NoError(t, err, "1wire-read")
	require.Equal(t, "000000000000000000\n", got, "1wire-read: every slot samples the line low")
}
