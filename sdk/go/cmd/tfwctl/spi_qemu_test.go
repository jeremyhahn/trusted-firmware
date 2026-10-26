// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"context"
	"fmt"
	"strings"
	"testing"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The shield 0 SPI select on both MPS2 boards: pin 2, the FPGA I/O block's
// MISC bit 8 (boards/mps2-an505/board.c, boards/mps2-an500/board.c). Pin 0 is
// an LED, which SPI_XFER refuses as a select.
const (
	qemuSPISelect = "2"
	qemuSPILED    = "0"
)

// The SPI row on a QEMU MPS2 board (docs/cortex-m/README.md): the image is the
// base row's built with SPI=1, so SPI_XFER reaches the shield 0 PL022 through
// tfwctl's spi verb.
//
// The bytes read back are literals, derived from QEMU v10.0.0's model and not
// from this firmware. hw/arm/mps2-tz.c (spi3 at 0x4020a000) and hw/arm/mps2.c
// (Shield0 at 0x40026000) create the PL022 and attach nothing to its SSI bus.
// hw/ssi/ssi.c ssi_transfer starts its answer at 0 and ORs in each
// peripheral's, of which there are none; hw/ssi/pl022.c pl022_xfer stores that
// 0, masked to the 8-bit frame SSPCR0 sets, in the receive FIFO for every frame
// written. The image never sets SSPCR1's loopback bit, which would echo the
// bytes instead. So every byte is 0x00, in either bit order and every mode.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUSPIXfer`).
func TestQEMUSPIXfer(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose SPI row starts this test", peer.ResetEnv)
	_, dev := qemuBoot(t, target)
	ctx := context.Background()

	caps, err := dev.Caps(ctx)
	require.NoError(t, err, "CAPS")
	require.True(t, caps.Has(tfw.FeatureSPI), "CAPS: the SPI=1 image reports SPI")

	xfer := func(cs, hexOut string) (string, error) {
		t.Helper()
		var out strings.Builder
		err := run(ctx, dev, "spi", []string{cs, hexOut}, false, &out)
		return out.String(), err
	}

	got, err := xfer(qemuSPISelect, "9fa55aff")
	require.NoError(t, err, "SPI_XFER at boot's configuration")
	require.Equal(t, "00000000\n", got, "SPI_XFER: QEMU's PL022 has nothing on its bus")

	for _, order := range []string{"msb", "lsb"} {
		mode3, div16 := fmt.Sprint(uint8(tfw.SPIMode3)), fmt.Sprint(uint8(tfw.SPIClockDiv16))
		require.NoError(t, run(ctx, dev, "spi-config", []string{mode3, div16, order}, false, &strings.Builder{}),
			"SPI_CONFIG mode 3, %s first", order)
		got, err = xfer(qemuSPISelect, "01020304050607080900")
		require.NoError(t, err, "SPI_XFER, %s first", order)
		require.Equal(t, "00000000000000000000\n", got, "SPI_XFER, %s first: ten frames, past the 8-frame FIFO", order)
	}

	_, err = xfer(qemuSPILED, "9f")
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "SPI_XFER on pin 0: %v, want a device status", err)
	require.Equal(t, tfw.StatusErrBadPin, st, "SPI_XFER on pin 0: not the shield's select")
}
