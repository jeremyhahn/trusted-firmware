// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// fakeEnv, set in a child's environment, makes this test binary stand in for
// the simulator: it writes its arguments, space-separated, then echoes stdin to
// stdout until stdin closes. Start is then tested choosing the simulator, and
// handing it its flags, without the firmware being built.
const fakeEnv = "PEER_FAKE_ECHO"

func TestMain(m *testing.M) {
	if os.Getenv(fakeEnv) != "" {
		if _, err := io.WriteString(os.Stdout, strings.Join(os.Args[1:], " ")); err != nil {
			os.Exit(1)
		}
		if _, err := io.Copy(os.Stdout, os.Stdin); err != nil {
			os.Exit(1)
		}
		os.Exit(0)
	}
	os.Exit(m.Run())
}

// fatalTB records Fatal instead of failing the test. Goexit mirrors what the
// real Fatal does: the calling goroutine does not return.
type fatalTB struct {
	testing.TB
	msg string
}

func (f *fatalTB) Helper() {}
func (f *fatalTB) Fatal(args ...any) {
	f.msg = fmt.Sprint(args...)
	runtime.Goexit()
}

// startFatal runs Start with a TB that records its failure.
func startFatal(t *testing.T) string {
	t.Helper()
	f := &fatalTB{TB: t}
	done := make(chan struct{})
	go func() {
		defer close(done)
		Start(f, simproc.Software)
	}()
	select {
	case <-done:
	case <-time.After(time.Minute):
		t.Fatal("Start did not return within a minute")
	}
	return f.msg
}

// With no port named, Start runs the simulator it is given and hands back its
// link.
func TestStartRunsTheSimulatorWhenNoPortIsNamed(t *testing.T) {
	c := startFake(t)
	if _, err := c.Write([]byte("peer")); err != nil {
		t.Fatal(err)
	}
	if got := readWithin(t, c, len("peer")); got != "peer" {
		t.Fatalf("read %q, want the simulator's echo %q", got, "peer")
	}
}

// The Ethernet rows boot the simulator with its network block; the flags reach
// it as given.
func TestStartHandsTheSimulatorItsFlags(t *testing.T) {
	const want = "--eth --eeprom-bin eeprom.bin"
	c := startFake(t, strings.Fields(want)...)
	if got := readWithin(t, c, len(want)); got != want {
		t.Fatalf("the simulator was started with %q, want %q", got, want)
	}
}

// startFake runs Start with no port named, so that it starts this test binary
// as the simulator, with args.
func startFake(t *testing.T, args ...string) io.ReadWriteCloser {
	t.Helper()
	self, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	if err := os.Symlink(self, filepath.Join(dir, simproc.Software.Name)); err != nil {
		t.Fatal(err)
	}
	t.Setenv(simproc.DirEnv, dir)
	t.Setenv(fakeEnv, "1")
	t.Setenv(PortEnv, "")
	t.Setenv(BoardEnv, "")
	t.Setenv(EthEnv, "")
	return Start(t, simproc.Software, args...)
}

// readWithin reads n bytes from c, failing the test if they take a minute.
func readWithin(t *testing.T, c io.Reader, n int) string {
	t.Helper()
	got := make([]byte, n)
	done := make(chan error, 1)
	go func() {
		_, err := io.ReadFull(c, got)
		done <- err
	}()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("read %q: %v", got, err)
		}
	case <-time.After(time.Minute):
		t.Fatal("the simulator did not answer within a minute")
	}
	return string(got)
}

// Half a target fails the test rather than guessing which peer was meant.
func TestStartFailsOnHalfATarget(t *testing.T) {
	t.Setenv(PortEnv, "")
	t.Setenv(BoardEnv, "atmega2560")
	if msg := startFatal(t); !strings.Contains(msg, PortEnv) {
		t.Fatalf("Start failed with %q, want it to name %s", msg, PortEnv)
	}
}

// A named port that is not there fails the test; it never falls back to the
// simulator.
func TestStartFailsOnAMissingPort(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "ttyNONE")
	t.Setenv(PortEnv, missing)
	t.Setenv(BoardEnv, "atmega2560")
	if msg := startFatal(t); !strings.Contains(msg, missing) {
		t.Fatalf("Start failed with %q, want it to name %s", msg, missing)
	}
}

// A named port is opened, the board on it reset and its BOOT kept for the
// first Read; the test's cleanup closes the port.
func TestStartOpensTheNamedPortAndResetsTheBoard(t *testing.T) {
	const port = "ttyBOARD"
	r := &resettablePort{}
	var opened string
	restore := openPort
	t.Cleanup(func() { openPort = restore })
	openPort = func(path string, _ int) (io.ReadWriteCloser, error) {
		opened = path
		return &bootAfterPurge{r, []byte{0xA5, 0x01}}, nil
	}
	t.Setenv(PortEnv, port)
	t.Setenv(BoardEnv, "atmega2560")

	t.Run("start", func(t *testing.T) {
		c := Start(t, simproc.Software)
		got := make([]byte, 8)
		n, err := c.Read(got)
		if err != nil || string(got[:n]) != "\xa5\x01" {
			t.Fatalf("first read = %x, %v; want the BOOT event a501", got[:n], err)
		}
		if r.closed {
			t.Fatal("the port closed before the test ended")
		}
	})
	if opened != port {
		t.Fatalf("opened %q, want %s's %q", opened, PortEnv, port)
	}
	if !r.closed {
		t.Fatal("the test's cleanup left the port open")
	}
}

// Once the kept bytes are read, reads go to the port itself.
func TestStartedReadsThePortOnceThePendingBytesAreGone(t *testing.T) {
	boom := errors.New("boom")
	f := &fakePort{data: []byte{0xA5}}
	c, err := awaitStart(f, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 4)
	if n, err := c.Read(buf); n != 1 || err != nil {
		t.Fatalf("first read = %d, %v; want the kept byte", n, err)
	}
	f.err = boom
	if _, err := c.Read(buf); !errors.Is(err, boom) {
		t.Fatalf("second read err = %v, want the port's %v", err, boom)
	}
}
