// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package simproc

import (
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"
)

// Mutation tests (#163): each one kills a fault the other tests let through.

// scriptDir puts a shell script under b's name in a fresh simulator directory
// and points DirEnv at it: a stand-in whose behaviour the echo fake cannot
// show, such as ignoring its input or writing to stderr.
func scriptDir(t *testing.T, b Binary, body string) {
	t.Helper()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, b.Name), []byte("#!/bin/sh\n"+body+"\n"), 0o700); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)
}

// hang is a stand-in that ignores its input and runs until it is killed.
const hang = "exec tail -f /dev/null"

// reaped reports whether c's process has been waited for. A killed process
// that was not waited for is a zombie, which still takes a signal.
func reaped(c *Conn) bool {
	return errors.Is(c.cmd.Process.Signal(syscall.Signal(0)), os.ErrProcessDone)
}

// Dir is build/host at the root of this checkout, not at some other level of
// it: the root is where this source file sits four directories down.
func TestDirIsBuildHostAtTheCheckoutRoot(t *testing.T) {
	t.Setenv(DirEnv, "")
	self := filepath.Join(Dir(), "..", "..", "sdk", "go", "internal", "simproc", "simproc.go")
	if _, err := os.Stat(self); err != nil {
		t.Fatalf("Dir = %s is not build/host at the root holding this package: %v", Dir(), err)
	}
}

// Start hands the process its arguments, and the parent's copy of the write
// end of stdout is closed, so the process exiting is an EOF.
func TestStartPassesItsArgumentsAndEndsAtTheProcesssExit(t *testing.T) {
	scriptDir(t, Software, `echo "$@"`)
	c := Start(t, Software, "--provision-element", "x")
	var got []byte
	var err error
	within(t, 30*time.Second, "reading to the process's exit", func() { got, err = io.ReadAll(c) })
	if err != nil || string(got) != "--provision-element x\n" {
		t.Fatalf("read %q, %v; want the arguments echoed, then EOF", got, err)
	}
}

// Close kills a process that does not exit on its own, and reaps it.
func TestCloseKillsAndReapsAProcessThatIgnoresItsInput(t *testing.T) {
	scriptDir(t, Software, hang)
	c := Start(t, Software)
	within(t, 30*time.Second, "Close", func() { _ = c.Close() })
	if !reaped(c) {
		t.Fatal("Close returned without reaping the process")
	}
}

// Close closes both ends the parent holds: a write and a read after it fail
// as on a closed file, not as on a pipe whose peer has gone.
func TestCloseClosesBothPipes(t *testing.T) {
	fakeDir(t, Software)
	c := Start(t, Software)
	if err := c.Close(); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Write([]byte{1}); !errors.Is(err, os.ErrClosed) {
		t.Errorf("write after Close: %v, want %v", err, os.ErrClosed)
	}
	if _, err := c.Read(make([]byte, 1)); !errors.Is(err, os.ErrClosed) {
		t.Errorf("read after Close: %v, want %v", err, os.ErrClosed)
	}
}

// The process's stderr is the test's: a simulator's diagnostics reach the
// test log instead of being dropped.
func TestStartPassesTheProcesssStderrThrough(t *testing.T) {
	scriptDir(t, Software, "echo marker >&2")
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = r.Close() }()
	stderr := os.Stderr
	os.Stderr = w
	c := Start(t, Software)
	os.Stderr = stderr
	_ = w.Close() // the child holds its own copy
	var got []byte
	within(t, 30*time.Second, "reading the process's stderr", func() { got, err = io.ReadAll(r) })
	if err != nil || !strings.Contains(string(got), "marker") {
		t.Fatalf("stderr = %q, %v; want the process's marker", got, err)
	}
	_ = c.Close()
}

// The process is stopped when the test that started it ends.
func TestStartStopsTheProcessWhenItsTestEnds(t *testing.T) {
	scriptDir(t, Software, hang)
	var c *Conn
	t.Run("start", func(t *testing.T) { c = Start(t, Software) })
	if c == nil {
		t.Fatal("Start returned no conn")
	}
	if !reaped(c) {
		_ = c.Close()
		t.Fatal("the process outlived the test that started it")
	}
}
