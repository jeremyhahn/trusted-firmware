// SPDX-License-Identifier: AGPL-3.0-or-later

package serialport_test

import (
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

// Linux ioctls the standard library does not name.
const (
	tiocgptn   = 0x80045430 // TIOCGPTN: the pty number behind a ptmx fd
	tiocsptlck = 0x40045431 // TIOCSPTLCK: lock or unlock the slave
	cbaud      = 0o10017    // CBAUD: the baud bits of c_cflag
)

func ioctl(t *testing.T, f *os.File, req uintptr, arg unsafe.Pointer) {
	t.Helper()
	rc, err := f.SyscallConn()
	require.NoError(t, err)
	var errno syscall.Errno
	require.NoError(t, rc.Control(func(fd uintptr) {
		_, _, errno = syscall.Syscall(syscall.SYS_IOCTL, fd, req, uintptr(arg))
	}))
	if errno != 0 {
		require.NoError(t, errno)
	}
}

// openPTY opens a pseudo-terminal pair: the master the test drives, and the
// slave's path, which stands in for /dev/ttyACM0. No real port is touched.
func openPTY(t *testing.T) (*os.File, string) {
	t.Helper()
	master, err := os.OpenFile("/dev/ptmx", os.O_RDWR|syscall.O_NOCTTY, 0)
	require.NoError(t, err)
	t.Cleanup(func() { _ = master.Close() })
	var unlock int32
	ioctl(t, master, tiocsptlck, unsafe.Pointer(&unlock))
	var n uint32
	ioctl(t, master, tiocgptn, unsafe.Pointer(&n))
	return master, fmt.Sprintf("/dev/pts/%d", n)
}

// termios reads the slave's line settings through the master: Open holds the
// slave exclusively (TIOCEXCL), and Linux applies a termios ioctl on a pty
// master to its slave.
func termios(t *testing.T, master *os.File) syscall.Termios {
	t.Helper()
	var tio syscall.Termios
	ioctl(t, master, syscall.TCGETS, unsafe.Pointer(&tio))
	return tio
}

func TestOpenConfiguresTheLineAsEightNOne(t *testing.T) {
	for _, tc := range []struct {
		name string
		baud int
		want uint32
	}{
		{"zero is DefaultBaud", 0, syscall.B115200},
		{"negative is DefaultBaud", -1, syscall.B115200},
		{"DefaultBaud", serialport.DefaultBaud, syscall.B115200},
		{"9600", 9600, syscall.B9600},
		{"57600", 57600, syscall.B57600},
	} {
		t.Run(tc.name, func(t *testing.T) {
			master, path := openPTY(t)
			p, err := serialport.Open(path, tc.baud)
			require.NoError(t, err)
			defer func() { _ = p.Close() }()

			tio := termios(t, master)
			assert.Equal(t, tc.want, tio.Cflag&cbaud, "baud")
			assert.Equal(t, uint32(syscall.CS8), tio.Cflag&syscall.CSIZE, "8 data bits")
			assert.Zero(t, tio.Cflag&syscall.PARENB, "no parity")
			assert.Zero(t, tio.Cflag&syscall.CSTOPB, "one stop bit")
			assert.Zero(t, tio.Lflag&(syscall.ICANON|syscall.ECHO), "raw: no line editing, no echo")
		})
	}
}

func TestOpenCarriesBytesBothWays(t *testing.T) {
	master, path := openPTY(t)
	p, err := serialport.Open(path, 0)
	require.NoError(t, err)
	defer func() { _ = p.Close() }()

	// Bytes the line discipline would rewrite in cooked mode: CR, NL, ^C, 0x00.
	frame := []byte{0xA5, 0x0D, 0x0A, 0x03, 0x00, 0xFF}
	_, err = master.Write(frame)
	require.NoError(t, err)
	got := make([]byte, 0, len(frame))
	buf := make([]byte, 16)
	for deadline := time.Now().Add(2 * time.Second); len(got) < len(frame) && time.Now().Before(deadline); {
		n, err := p.Read(buf)
		require.NoError(t, err)
		got = append(got, buf[:n]...)
	}
	assert.Equal(t, frame, got, "device to host")

	_, err = p.Write(frame)
	require.NoError(t, err)
	require.NoError(t, master.SetReadDeadline(time.Now().Add(2*time.Second)))
	back := make([]byte, len(frame))
	n, err := master.Read(back)
	for err == nil && n < len(frame) {
		var m int
		m, err = master.Read(back[n:])
		n += m
	}
	require.NoError(t, err)
	assert.Equal(t, frame, back, "host to device")
}

// The read timeout is the floor that turns a silent device into a returned
// read instead of a hung process.
func TestOpenReadReturnsOnASilentLine(t *testing.T) {
	_, path := openPTY(t)
	p, err := serialport.Open(path, 0)
	require.NoError(t, err)
	defer func() { _ = p.Close() }()

	done := make(chan int, 1)
	go func() {
		n, _ := p.Read(make([]byte, 8))
		done <- n
	}()
	select {
	case n := <-done:
		assert.Zero(t, n)
	case <-time.After(2 * time.Second):
		t.Fatal("Read on a silent line did not return")
	}
}

// Open purges what arrived before it: a stale backlog is not a reply.
func TestOpenDiscardsBytesQueuedBeforeIt(t *testing.T) {
	master, path := openPTY(t)
	_, err := master.Write([]byte("stale\n"))
	require.NoError(t, err)
	time.Sleep(50 * time.Millisecond)

	p, err := serialport.Open(path, 0)
	require.NoError(t, err)
	defer func() { _ = p.Close() }()
	n, err := p.Read(make([]byte, 16))
	require.NoError(t, err)
	assert.Zero(t, n, "the backlog was read after Open")
}

func TestOpenRefusesAMissingDevice(t *testing.T) {
	path := filepath.Join(t.TempDir(), "ttyACM9")
	p, err := serialport.Open(path, 0)
	assert.Nil(t, p)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "serialport: opening "+path)
}

func TestOpenRefusesAPathThatIsNotATTY(t *testing.T) {
	path := filepath.Join(t.TempDir(), "plain")
	require.NoError(t, os.WriteFile(path, []byte("not a tty"), 0o600))
	p, err := serialport.Open(path, 0)
	assert.Nil(t, p)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "serialport: opening "+path)
}

// List only enumerates; it opens nothing, so it is safe on any host.
func TestListEnumeratesWithoutError(t *testing.T) {
	_, err := serialport.List()
	require.NoError(t, err)
}
