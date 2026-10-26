// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The HardFault row on a QEMU MPS2 board (docs/cortex-m/README.md, "The
// reset cause"): a fault in the running application, and the cause SYS_INFO
// reports after it.
//
// The fault is made from outside, through QEMU's gdbstub: the test stops the
// guest, sets its PC to 0xE0000000 and resumes it. QEMU v10.0.0's
// target/arm/ptw.c (m_is_system_region) makes 0xE0000000 and up execute-never,
// so the next fetch takes a MemManage fault (IACCVIOL); MemManage is not
// enabled in SHCSR, so it escalates to HardFault (hw/intc/armv7m_nvic.c,
// do_armv7m_nvic_set_pending). The HardFault
// vector is tfw_cm_fault, which writes the panic marker and sets
// AIRCR.SYSRESETREQ (src/hal/cortex-m/hal_early.c); tfw-boot's reset handler
// decodes the marker as PANIC (src/hal/cortex-m/hal_reset_decode.h), 6 in
// include/tfw/types.h. Before the reset, tfw_cm_fault kept the stacked
// frame's PC, LR and xPSR in the fault record (hal_fault_frame.h), which the
// test reads back through the gdbstub: the PC is 0xE0000000. The gdbstub is the test's lever only: the firmware
// never sees it, and the path from the fault on is the firmware's own.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle CYCLE_TEST=TestQEMUFaultPanic`).
func TestQEMUFaultPanic(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose base row starts this test", peer.ResetEnv)
	conn, dev := qemuBoot(t, target)
	ctx := context.Background()

	si, err := dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after QMP's system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "QMP's system_reset leaves no marker")

	gdb := target.QMP + ".fault.gdb"
	require.NoError(t, hmp(target.QMP, "gdbserver unix:"+gdb+",server=on,wait=off"))
	require.NoError(t, faultAt(gdb, 0xE0000000))

	// The same pty: the next bytes are the new boot's.
	rw, ok := conn.(io.ReadWriter)
	require.True(t, ok, "OpenQMP's conn reads and writes")
	dev = tfw.New(rw, tfw.WithTimeout(10*time.Second), tfw.WithBootWait(peer.StartBound))
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after the fault")
	require.Equal(t, tfw.ResetPanic, si.ResetCause, "a HardFault is the image's fatal error")

	// The fault record the reset left (src/hal/cortex-m/hal_fault_frame.h):
	// the faulting frame's PC, LR and xPSR, at the address make names.
	rec, err := readWords(gdb, faultFrameAt(t), 3)
	require.NoError(t, err)
	require.Equal(t, uint32(0xE0000000), rec[0], "the fault record keeps the faulting PC")
	require.NotEqual(t, uint32(0xFFFFFFFF), rec[2], "the fault record read the stacked frame")

	require.NoError(t, conn.Close())
	_, dev = qemuBoot(t, target)
	si, err = dev.SysInfo(ctx)
	require.NoError(t, err, "SYS_INFO after a second QMP system_reset")
	require.Equal(t, tfw.ResetPowerOn, si.ResetCause, "the reset handler cleared the marker it read")
}

// hmp runs one human-monitor command over the QMP socket.
func hmp(socket, line string) error {
	conn, err := net.DialTimeout("unix", socket, peer.StartBound)
	if err != nil {
		return fmt.Errorf("qmp %s: %w", socket, err)
	}
	defer func() { _ = conn.Close() }()
	if err := conn.SetDeadline(time.Now().Add(peer.StartBound)); err != nil {
		return err
	}
	dec := json.NewDecoder(conn)
	var greeting map[string]json.RawMessage
	if err := dec.Decode(&greeting); err != nil {
		return fmt.Errorf("qmp: reading the greeting: %w", err)
	}
	args, err := json.Marshal(map[string]string{"command-line": line})
	if err != nil {
		return err
	}
	for _, cmd := range []string{
		`{"execute":"qmp_capabilities"}`,
		`{"execute":"human-monitor-command","arguments":` + string(args) + `}`,
	} {
		if _, err := io.WriteString(conn, cmd+"\n"); err != nil {
			return err
		}
		for {
			var m struct {
				Return json.RawMessage `json:"return"`
				Error  *struct {
					Desc string `json:"desc"`
				} `json:"error"`
			}
			if err := dec.Decode(&m); err != nil {
				return fmt.Errorf("qmp %s: %w", cmd, err)
			}
			if m.Error != nil {
				return fmt.Errorf("qmp %s: %s", cmd, m.Error.Desc)
			}
			if m.Return != nil {
				break // an event otherwise
			}
		}
	}
	return nil
}

// faultAt stops the guest through the gdbstub at socket, sets its PC to pc and
// detaches, which resumes it. Register 15 is the PC in QEMU's Arm gdbstub;
// the value is target-endian hex.
func faultAt(socket string, pc uint32) error {
	conn, err := net.DialTimeout("unix", socket, peer.StartBound)
	if err != nil {
		return fmt.Errorf("gdbstub %s: %w", socket, err)
	}
	defer func() { _ = conn.Close() }()
	if err := conn.SetDeadline(time.Now().Add(peer.StartBound)); err != nil {
		return err
	}
	r := bufio.NewReader(conn)
	le := fmt.Sprintf("%02x%02x%02x%02x", byte(pc), byte(pc>>8), byte(pc>>16), byte(pc>>24))
	for _, step := range []struct{ cmd, want string }{
		{"?", ""}, // the stop reply, any signal
		{"Pf=" + le, "OK"},
		{"D", "OK"},
	} {
		got, err := rsp(conn, r, step.cmd)
		if err != nil {
			return err
		}
		if step.want != "" && got != step.want {
			return fmt.Errorf("gdbstub %s: got %q, want %q", step.cmd, got, step.want)
		}
	}
	return nil
}

// faultFrameAt is the fault record's address, mk/arch/cortex-m.mk's
// CM_FAULTFRAME_AT for the board, which `make cortex-m-qemu-cycle` passes.
func faultFrameAt(t *testing.T) uint32 {
	t.Helper()
	s := os.Getenv(faultFrameEnv)
	require.NotEmpty(t, s,
		"%s is not set: run `make integration-test-cortex-m-wire`, whose base row starts this test", faultFrameEnv)
	at, err := strconv.ParseUint(s, 0, 32)
	require.NoError(t, err, "%s=%q", faultFrameEnv, s)
	return uint32(at)
}

const faultFrameEnv = "TFW_QEMU_FAULTFRAME"

// readWords reads n little-endian words at addr through the gdbstub on
// socket: it stops the guest, reads the memory and detaches, which resumes it.
func readWords(socket string, addr uint32, n int) ([]uint32, error) {
	conn, err := net.DialTimeout("unix", socket, peer.StartBound)
	if err != nil {
		return nil, fmt.Errorf("gdbstub %s: %w", socket, err)
	}
	defer func() { _ = conn.Close() }()
	if err := conn.SetDeadline(time.Now().Add(peer.StartBound)); err != nil {
		return nil, err
	}
	r := bufio.NewReader(conn)
	if _, err := rsp(conn, r, "?"); err != nil {
		return nil, err
	}
	cmd := fmt.Sprintf("m%x,%x", addr, 4*n)
	got, err := rsp(conn, r, cmd)
	if err != nil {
		return nil, err
	}
	raw, err := hex.DecodeString(got)
	if err != nil || len(raw) != 4*n {
		return nil, fmt.Errorf("gdbstub %s: got %q, want %d bytes of hex", cmd, got, 4*n)
	}
	detached, err := rsp(conn, r, "D")
	if err != nil {
		return nil, err
	}
	if detached != "OK" {
		return nil, fmt.Errorf("gdbstub D: got %q, want \"OK\"", detached)
	}
	words := make([]uint32, n)
	for i := range words {
		words[i] = binary.LittleEndian.Uint32(raw[4*i:])
	}
	return words, nil
}

// rsp sends one GDB remote-serial-protocol packet and returns the reply's data.
func rsp(w io.Writer, r *bufio.Reader, data string) (string, error) {
	var sum byte
	for i := 0; i < len(data); i++ {
		sum += data[i]
	}
	if _, err := fmt.Fprintf(w, "$%s#%02x", data, sum); err != nil {
		return "", err
	}
	for {
		c, err := r.ReadByte()
		if err != nil {
			return "", fmt.Errorf("gdbstub %s: %w", data, err)
		}
		if c == '$' {
			break // '+' acks ours; anything else before the reply is skipped
		}
	}
	body, err := r.ReadString('#')
	if err != nil {
		return "", fmt.Errorf("gdbstub %s: %w", data, err)
	}
	if _, err := r.Discard(2); err != nil {
		return "", fmt.Errorf("gdbstub %s: %w", data, err)
	}
	if _, err := io.WriteString(w, "+"); err != nil {
		return "", err
	}
	return body[:len(body)-1], nil
}
