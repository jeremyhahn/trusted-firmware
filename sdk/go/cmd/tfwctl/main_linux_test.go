// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"fmt"
	"os"
	"strings"
	"syscall"
	"testing"
	"unsafe"
)

// Linux ioctls the standard library does not name.
const (
	tiocgptn   = 0x80045430 // TIOCGPTN: the pty number behind a ptmx fd
	tiocsptlck = 0x40045431 // TIOCSPTLCK: lock or unlock the slave
)

func ioctl(t *testing.T, f *os.File, req uintptr, arg unsafe.Pointer) {
	t.Helper()
	rc, err := f.SyscallConn()
	if err != nil {
		t.Fatal(err)
	}
	var errno syscall.Errno
	if err := rc.Control(func(fd uintptr) {
		_, _, errno = syscall.Syscall(syscall.SYS_IOCTL, fd, req, uintptr(arg))
	}); err != nil {
		t.Fatal(err)
	}
	if errno != 0 {
		t.Fatal(errno)
	}
}

// openPTY opens a pseudo-terminal pair: the slave's path stands in for a
// board's serial port, and the master is the board's side of the wire.
func openPTY(t *testing.T) (*os.File, string) {
	t.Helper()
	master, err := os.OpenFile("/dev/ptmx", os.O_RDWR|syscall.O_NOCTTY, 0)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = master.Close() })
	var unlock int32
	ioctl(t, master, tiocsptlck, unsafe.Pointer(&unlock))
	var n uint32
	ioctl(t, master, tiocgptn, unsafe.Pointer(&n))
	return master, fmt.Sprintf("/dev/pts/%d", n)
}

// Over a serial port main opens the port and runs the verb on what answers
// there, and reports a boot event seen during the exchange.
func TestMainRunsAVerbOverASerialPort(t *testing.T) {
	master, path := openPTY(t)
	go serveFrames(t, master, master, (&benchDevice{}).answer, bootEvent(t))
	stdout, stderr := callMain(t, "-boot-wait=0", "-timeout=10s", path, "ping", "hi")
	if !strings.Contains(stdout, `pong: "hi"`) {
		t.Fatalf("stdout %q, want the device's pong", stdout)
	}
	if !strings.Contains(stderr, "boot event") {
		t.Fatalf("stderr %q, want the boot event noted", stderr)
	}
}
