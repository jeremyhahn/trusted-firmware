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
// spins, so the HardFault breakpoint is never hit: the PANIC is the MPU's.
//
// qemu_wire_test.sh's base row runs it on mps2-an500 only: mps2-an505's
// ARMv8-M MPU is not armed (out of scope for row 482).
func TestQEMUMPUNullRead(t *testing.T) {
	mpuFaultRow(t, func(s *gdbSession, at uint32) (uint32, error) {
		return 0, readGadget(s, at, 0)
	}, nil)
}

// TestQEMUMPUBootRead is the tfw-boot region's row: a load from 0x100, inside
// tfw-boot's flash region (TFW_BOOT_REGION_BASE/SIZE, mps2-an500's
// board_profile.h) and past the 32-byte null guard, faults. Only region 2
// denies it, so an image that programmed the null guard alone would read
// tfw-boot's code there and spin.
func TestQEMUMPUBootRead(t *testing.T) {
	const bootAddr = 0x100
	mpuFaultRow(t, func(s *gdbSession, at uint32) (uint32, error) {
		return bootAddr, readGadget(s, at, bootAddr)
	}, nil)
}

// TestQEMUMPUStackGuard is the stack guard's row: a `push {r0}; b .-2` loop
// with the SP 64 bytes above the guard (region 1, read back through RNR,
// RBAR and RASR) overflows the stack downwards until its store hits the
// guard's top word. Sentinels planted in the 16 bytes below the guard, the
// first application data an unguarded overflow would reach, are read at the
// fault (a breakpoint on the HardFault handler, before the PANIC reset) and
// must be intact; MMFAR must name the guard's top word.
func TestQEMUMPUStackGuard(t *testing.T) {
	sentinel := []uint32{0x5afe0001, 0x5afe0002, 0x5afe0003, 0x5afe0004}
	var guard uint32
	mpuFaultRow(t, func(s *gdbSession, at uint32) (uint32, error) {
		if err := s.ok(fmt.Sprintf("M%x,4:%s", mpuRNR, le32(1))); err != nil {
			return 0, err
		}
		reg, err := s.words(mpuRBAR, 2)
		if err != nil {
			return 0, err
		}
		rbar, rasr := reg[0], reg[1]
		if rasr&1 == 0 || (rasr>>1)&0x1f != 10 || (rasr>>24)&7 != 0 {
			return 0, fmt.Errorf("MPU region 1: RASR %#x, want an enabled 2 KiB no-access region", rasr)
		}
		guard = rbar &^ 0x1f
		fill := fmt.Sprintf("M%x,%x:", guard-16, 4*len(sentinel))
		for _, w := range sentinel {
			fill += le32(w)
		}
		top := guard + 2048
		return top - 4, s.ok(
			fill,
			fmt.Sprintf("M%x,4:01b4fde7", at), // push {r0}; b .-2
			"P0="+le32(0xdeadbeef),
			"Pd="+le32(top+64),
			"Pf="+le32(at),
		)
	}, func(s *gdbSession) error {
		got, err := s.words(guard-16, len(sentinel))
		if err != nil {
			return err
		}
		for i := range sentinel {
			if got[i] != sentinel[i] {
				return fmt.Errorf("application data below the guard at %#x: %#x, want %#x",
					guard-16+uint32(4*i), got[i], sentinel[i])
			}
		}
		return nil
	})
}

// The ARMv7-M system registers the rows read through the gdbstub.
const (
	scbVTOR  = 0xE000ED08
	scbCFSR  = 0xE000ED28
	scbMMFAR = 0xE000ED34
	mpuRNR   = 0xE000ED98
	mpuRBAR  = 0xE000ED9C

	cfsrDACCVIOL  = 1 << 1
	cfsrMMARVALID = 1 << 7
)

// mpuFaultRow runs one MPU row. On a freshly reset image it stops the guest
// through a gdbstub, lets plant set up a gadget at at (256 bytes below the SP)
// and return the data address the gadget's access must fault on, breaks on
// the HardFault handler, and resumes. At the break it checks that CFSR
// records a data access violation at that address (MMFAR) and runs atFault,
// if any; then it detaches, and SYS_INFO must read PANIC with the fault
// record's PC at the gadget. A second reset must clear the marker.
func mpuFaultRow(t *testing.T, plant func(s *gdbSession, at uint32) (uint32, error), atFault func(s *gdbSession) error) {
	t.Helper()
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
	s, err := gdbDial(gdb)
	require.NoError(t, err)
	sp, err := s.reg(0xd)
	require.NoError(t, err)
	at := (sp - 256) &^ 3
	vtor, err := s.words(scbVTOR, 1)
	require.NoError(t, err)
	hf, err := s.words(vtor[0]+12, 1)
	require.NoError(t, err)
	want, err := plant(s, at)
	require.NoError(t, err)

	bp := fmt.Sprintf("Z0,%x,2", hf[0]&^1)
	require.NoError(t, s.ok(bp))
	stop, err := rsp(s.conn, s.r, "c")
	require.NoError(t, err)
	require.True(t, len(stop) > 0 && (stop[0] == 'T' || stop[0] == 'S'),
		"gdbstub c: got %q, want a stop at the HardFault handler: the access did not fault", stop)
	fsr, err := s.words(scbCFSR, 1)
	require.NoError(t, err)
	require.Equal(t, uint32(cfsrDACCVIOL|cfsrMMARVALID), fsr[0]&(cfsrDACCVIOL|cfsrMMARVALID),
		"CFSR %#x: the fault is not the MPU's data access violation", fsr[0])
	mmfar, err := s.words(scbMMFAR, 1)
	require.NoError(t, err)
	require.Equal(t, want, mmfar[0], "MMFAR names the faulting data address")
	if atFault != nil {
		require.NoError(t, atFault(s))
	}
	require.NoError(t, s.ok("z"+bp[1:], "D"))
	s.close()

	rw, ok := conn.(io.ReadWriter)
	require.True(t, ok, "OpenQMP's conn reads and writes")
	dev = tfw.New(rw, tfw.WithTimeout(10*time.Second), tfw.WithBootWait(peer.StartBound))
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after the fault: no answer means the image did not reach PANIC")
	require.Equal(t, tfw.ResetPanic, si.ResetCause, "the MPU's fault is the image's fatal error")

	rec, err := readWords(gdb, faultFrameAt(t), 3)
	require.NoError(t, err)
	require.Equal(t, at, rec[0], "the fault record keeps the PC of the faulting access")

	require.NoError(t, conn.Close())
	_, dev = qemuBoot(t, target)
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after a second QMP system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "the reset handler cleared the marker it read")
}

// readGadget writes `ldr r0, [r0]; b .` at at, sets r0 = addr and the PC to
// the gadget.
func readGadget(s *gdbSession, at, addr uint32) error {
	return s.ok(
		fmt.Sprintf("M%x,4:0068fee7", at), // ldr r0, [r0]; b .
		"P0="+le32(addr),
		"Pf="+le32(at),
	)
}

// gdbSession is a stopped guest on a gdbstub socket.
type gdbSession struct {
	conn net.Conn
	r    *bufio.Reader
}

// gdbDial connects to the gdbstub on socket and stops the guest.
func gdbDial(socket string) (*gdbSession, error) {
	conn, err := net.DialTimeout("unix", socket, peer.StartBound)
	if err != nil {
		return nil, fmt.Errorf("gdbstub %s: %w", socket, err)
	}
	if err := conn.SetDeadline(time.Now().Add(2 * peer.StartBound)); err != nil {
		_ = conn.Close()
		return nil, err
	}
	s := &gdbSession{conn: conn, r: bufio.NewReader(conn)}
	if _, err := rsp(conn, s.r, "?"); err != nil {
		s.close()
		return nil, err
	}
	return s, nil
}

func (s *gdbSession) close() { _ = s.conn.Close() }

// ok sends each step in turn; each must answer OK.
func (s *gdbSession) ok(steps ...string) error {
	for _, step := range steps {
		got, err := rsp(s.conn, s.r, step)
		if err != nil {
			return err
		}
		if got != "OK" {
			return fmt.Errorf("gdbstub %s: got %q, want OK", step, got)
		}
	}
	return nil
}

// reg reads core register n.
func (s *gdbSession) reg(n int) (uint32, error) {
	cmd := fmt.Sprintf("p%x", n)
	reply, err := rsp(s.conn, s.r, cmd)
	if err != nil {
		return 0, err
	}
	raw, err := hex.DecodeString(reply)
	if err != nil || len(raw) != 4 {
		return 0, fmt.Errorf("gdbstub %s: got %q", cmd, reply)
	}
	return binary.LittleEndian.Uint32(raw), nil
}

// words reads n little-endian words at addr.
func (s *gdbSession) words(addr uint32, n int) ([]uint32, error) {
	cmd := fmt.Sprintf("m%x,%x", addr, 4*n)
	got, err := rsp(s.conn, s.r, cmd)
	if err != nil {
		return nil, err
	}
	raw, err := hex.DecodeString(got)
	if err != nil || len(raw) != 4*n {
		return nil, fmt.Errorf("gdbstub %s: got %q, want %d bytes of hex", cmd, got, 4*n)
	}
	w := make([]uint32, n)
	for i := range w {
		w[i] = binary.LittleEndian.Uint32(raw[4*i:])
	}
	return w, nil
}

// le32 is v as the gdbstub's little-endian hex.
func le32(v uint32) string {
	var b [4]byte
	binary.LittleEndian.PutUint32(b[:], v)
	return hex.EncodeToString(b[:])
}
