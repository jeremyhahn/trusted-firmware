// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package simproc

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
)

// fakeEnv, set in a child's environment, makes this test binary stand in for
// a simulator: it echoes stdin to stdout until stdin closes. Start launches
// it exactly as it launches tfwsim, so the launcher is tested without the
// firmware; what the firmware answers is the tfwsim-tagged suite's job.
const fakeEnv = "SIMPROC_FAKE_ECHO"

func TestMain(m *testing.M) {
	if os.Getenv(fakeEnv) != "" {
		if _, err := io.Copy(os.Stdout, os.Stdin); err != nil {
			os.Exit(1)
		}
		os.Exit(0)
	}
	os.Exit(m.Run())
}

// fakeDir puts this test binary in a fresh simulator directory under b's
// name and points DirEnv at it.
func fakeDir(t *testing.T, b Binary) {
	t.Helper()
	self, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	if err := os.Symlink(self, filepath.Join(dir, b.Name)); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)
	t.Setenv(fakeEnv, "1")
}

// within runs f and fails the test if it has not returned by the bound.
func within(t *testing.T, bound time.Duration, what string, f func()) {
	t.Helper()
	done := make(chan struct{})
	go func() {
		defer close(done)
		f()
	}()
	select {
	case <-done:
	case <-time.After(bound):
		t.Fatalf("%s did not finish within %v", what, bound)
	}
}

func TestStartSpeaksToTheProcessOverItsStdio(t *testing.T) {
	fakeDir(t, Software)
	c := Start(t, Software)

	if _, err := c.Write([]byte("simproc")); err != nil {
		t.Fatal(err)
	}
	got := make([]byte, len("simproc"))
	var err error
	within(t, time.Minute, "reading the echo", func() { _, err = io.ReadFull(c, got) })
	if err != nil || string(got) != "simproc" {
		t.Fatalf("read %q, %v; want the echo %q", got, err, "simproc")
	}
}

// Close stops the process: its stdout ends, so a reader sees EOF instead of
// waiting on a process that is gone.
func TestCloseEndsTheProcess(t *testing.T) {
	fakeDir(t, Element)
	c := Start(t, Element, "--provision-element")
	if err := c.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	var rest []byte
	var err error
	within(t, time.Minute, "reading after Close", func() { rest, err = io.ReadAll(c) })
	if err != nil && !errors.Is(err, os.ErrClosed) {
		t.Fatalf("read after Close: %v", err)
	}
	if len(rest) != 0 {
		t.Fatalf("read %q after Close, want nothing", rest)
	}
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

func startFatal(t *testing.T, b Binary) string {
	t.Helper()
	f := &fatalTB{TB: t}
	within(t, time.Minute, "Start", func() { Start(f, b) })
	return f.msg
}

// A missing binary fails the test, and the failure says how to build it.
func TestStartFailsOnAMissingBinary(t *testing.T) {
	t.Setenv(DirEnv, t.TempDir())
	msg := startFatal(t, App)
	if !strings.Contains(msg, App.Make) {
		t.Fatalf("Start failed with %q, want it to name %q", msg, App.Make)
	}
}

// A file that is there but cannot be run fails the test too, never a skip.
func TestStartFailsOnABinaryThatCannotRun(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, Auto.Name), []byte("not a program"), 0o600); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)
	if msg := startFatal(t, Auto); msg == "" {
		t.Fatal("Start accepted a binary it could not run")
	}
}
