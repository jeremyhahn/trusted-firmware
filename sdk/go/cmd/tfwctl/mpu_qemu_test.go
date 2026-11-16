// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// TestQEMUMPUNullRead is mps2-an500's MPU row (row 482,
// src/hal/cortex-m/hal_mpu.c): a load from address 0 in the running image
// hits the null guard, and the MemManage fault, escalated to HardFault, ends
// in the PANIC path with the fault record's PC at the load.
//
// Through the gdbstub, as TestQEMUFaultPanic: a two-instruction gadget below
// the stack, `ldr r0, [r0]` then `b .`, with r0 = 0 and the PC at the gadget.
// Without the guard the load reads tfw-boot's vector table and the gadget
// spins, so SYS_INFO never answers: the PANIC is the MPU's.
//
// qemu_wire_test.sh's base row runs it on mps2-an500 only: mps2-an505's
// ARMv8-M MPU is not armed (out of scope for row 482).
func TestQEMUMPUNullRead(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose base row starts this test", peer.ResetEnv)
	conn, dev := qemuBoot(t, target)
	ctx := context.Background()

	si, err := dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after QMP's system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "QMP's system_reset leaves no marker")

	gdb := target.QMP + ".mpu.gdb"
	require.NoError(t, hmp(target.QMP, "gdbserver unix:"+gdb+",server=on,wait=off"))
	at, err := nullReadAt(gdb)
	require.NoError(t, err)

	rw, ok := conn.(io.ReadWriter)
	require.True(t, ok, "OpenQMP's conn reads and writes")
	dev = tfw.New(rw, tfw.WithTimeout(10*time.Second), tfw.WithBootWait(peer.StartBound))
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after the null read: no answer means the read did not fault")
	require.Equal(t, tfw.ResetPanic, si.ResetCause, "the null guard's fault is the image's fatal error")

	rec, err := readWords(gdb, faultFrameAt(t), 3)
	require.NoError(t, err)
	require.Equal(t, at, rec[0], "the fault record keeps the PC of the null read")

	require.NoError(t, conn.Close())
	_, dev = qemuBoot(t, target)
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after a second QMP system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "the reset handler cleared the marker it read")
}

// nullReadAt stops the guest, writes the gadget 256 bytes below its SP, sets
// r0 = 0 and the PC to the gadget, and detaches. It returns the gadget's
// address, the PC the fault record must keep.
func nullReadAt(socket string) (uint32, error) {
	conn, err := net.DialTimeout("unix", socket, peer.StartBound)
	if err != nil {
		return 0, fmt.Errorf("gdbstub %s: %w", socket, err)
	}
	defer func() { _ = conn.Close() }()
	if err := conn.SetDeadline(time.Now().Add(peer.StartBound)); err != nil {
		return 0, err
	}
	r := bufio.NewReader(conn)
	if _, err := rsp(conn, r, "?"); err != nil {
		return 0, err
	}
	reply, err := rsp(conn, r, "pd") // r13, the SP
	if err != nil {
		return 0, err
	}
	raw, err := hex.DecodeString(reply)
	if err != nil || len(raw) != 4 {
		return 0, fmt.Errorf("gdbstub pd: got %q", reply)
	}
	at := (binary.LittleEndian.Uint32(raw) - 256) &^ 3
	le := func(v uint32) string {
		var b [4]byte
		binary.LittleEndian.PutUint32(b[:], v)
		return hex.EncodeToString(b[:])
	}
	for _, step := range []string{
		fmt.Sprintf("M%x,4:0068fee7", at), // ldr r0, [r0]; b .
		"P0=00000000",
		"Pf=" + le(at),
		"D",
	} {
		got, err := rsp(conn, r, step)
		if err != nil {
			return 0, err
		}
		if got != "OK" {
			return 0, fmt.Errorf("gdbstub %s: got %q, want OK", step, got)
		}
	}
	return at, nil
}
