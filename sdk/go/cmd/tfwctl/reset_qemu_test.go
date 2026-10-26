// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"context"
	"io"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The reset-cause row on a QEMU MPS2 board (docs/cortex-m/README.md, "The
// reset cause"): RESET through tfwctl's reset verb, and the cause SYS_INFO
// reports after it.
//
// The causes are literals, include/tfw/types.h's wire values. RESET mode 0
// ends in tfw_cm_system_reset, which writes the reset-request marker and
// sets AIRCR.SYSRESETREQ (src/hal/cortex-m/hal_early.c); QEMU v10.0.0's
// armv7m NVIC (hw/intc/armv7m_nvic.c) answers that with a system reset
// request. The marker word is in the internal SRAM, which QEMU's reset does
// not reload (hw/core/loader.c's rom_reset copies back only the images it
// loaded), so tfw-boot's reset handler reads it, decodes it as SOFTWARE
// (src/hal/cortex-m/hal_reset_decode.h) and hands the cause to the
// application. QMP's system_reset writes no marker, so a boot after it reads
// POWER_ON, and a marker outlives one reset only.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUResetCause`).
func TestQEMUResetCause(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose base row starts this test", peer.ResetEnv)
	conn, dev := qemuBoot(t, target)
	ctx := context.Background()

	si, err := dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after QMP's system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "QMP's system_reset leaves no marker")

	require.NoError(t, run(ctx, dev, "reset", nil, false, &strings.Builder{}), "RESET mode 0")
	// The same pty: the next bytes are the new boot's. New waits for its
	// BOOT event, bounded by the seam's start bound.
	rw, ok := conn.(io.ReadWriter)
	require.True(t, ok, "OpenQMP's conn reads and writes")
	dev = tfw.New(rw, tfw.WithTimeout(10*time.Second), tfw.WithBootWait(peer.StartBound))
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after RESET")
	require.Equal(t, tfw.ResetSoftware, si.ResetCause, "RESET is AIRCR.SYSRESETREQ: the firmware asked")

	require.NoError(t, conn.Close())
	_, dev = qemuBoot(t, target)
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after a second QMP system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "the reset handler cleared the marker it read")
}
