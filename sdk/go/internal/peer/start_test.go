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
// the simulator: it echoes stdin to stdout until stdin closes. Start is then
// tested choosing the simulator without the firmware being built.
const fakeEnv = "PEER_FAKE_ECHO"

func TestMain(m *testing.M) {
	if os.Getenv(fakeEnv) != "" {
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

	c := Start(t, simproc.Software)
	if _, err := c.Write([]byte("peer")); err != nil {
		t.Fatal(err)
	}
	got := make([]byte, len("peer"))
	done := make(chan error, 1)
	go func() {
		_, err := io.ReadFull(c, got)
		done <- err
	}()
	select {
	case err := <-done:
		if err != nil || string(got) != "peer" {
			t.Fatalf("read %q, %v; want the simulator's echo %q", got, err, "peer")
		}
	case <-time.After(time.Minute):
		t.Fatal("the simulator did not answer within a minute")
	}
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
