// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"fmt"
	"os"
	"strings"
	"syscall"
	"testing"
	"time"
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

// openPTY opens a pseudo-terminal pair and returns the slave's path, which
// stands in for a board's port. No real port is touched.
func openPTY(t *testing.T) string {
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
	return fmt.Sprintf("/dev/pts/%d", n)
}

// A port that opens but has no modem lines -- a pty has none -- cannot reset
// the board, so Open fails, names the port, and closes it again: the same
// path reopens at once.
func TestOpenFailsOnAPortThatCannotDriveDTR(t *testing.T) {
	path := openPTY(t)
	c, err := Open(path, time.Second)
	if err == nil {
		_ = c.Close()
		t.Fatal("a port with no DTR line was opened as a board")
	}
	if !strings.Contains(err.Error(), path) || !strings.Contains(err.Error(), "DTR") {
		t.Fatalf("err = %v, want it to name %s and the DTR failure", err, path)
	}
	// Open holds the port exclusively; had it leaked the port, this would
	// fail with EBUSY.
	if _, err := Open(path, time.Second); err == nil || !strings.Contains(err.Error(), "DTR") {
		t.Fatalf("reopening: err = %v, want the same DTR failure, not a busy port", err)
	}
}
