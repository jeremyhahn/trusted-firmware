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

func TestLocateFindsABinaryInTheDirectoryTheEnvironmentNames(t *testing.T) {
	dir := t.TempDir()
	want := filepath.Join(dir, Element.Name)
	if err := os.WriteFile(want, nil, 0o700); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)

	got, err := Locate(Element)
	if err != nil {
		t.Fatal(err)
	}
	if got != want {
		t.Errorf("Locate = %s, want %s", got, want)
	}
}

// A missing binary is an error that says how to build it, and is never a skip.
func TestLocateReportsAMissingBinaryWithHowToBuildIt(t *testing.T) {
	t.Setenv(DirEnv, t.TempDir())

	_, err := Locate(Element)
	var nb *NotBuiltError
	if !errors.As(err, &nb) {
		t.Fatalf("err = %v, want *NotBuiltError", err)
	}
	if !errors.Is(err, os.ErrNotExist) {
		t.Errorf("err does not unwrap to the stat failure: %v", err)
	}
	for _, want := range []string{"tfwsim-atecc608a", "make tfwsim-atecc608a", DirEnv} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q does not mention %q", err, want)
		}
	}

	// The provisioning jig image says how to build itself, not the element tier.
	_, err = Locate(Prov)
	if !errors.As(err, &nb) || !strings.Contains(err.Error(), "run `make tfwsim-prov`") {
		t.Errorf("Locate(Prov) = %v, want a NotBuiltError naming make tfwsim-prov", err)
	}
}

func TestDirDefaultsToTheCheckoutsBuildDirectory(t *testing.T) {
	t.Setenv(DirEnv, "")
	got := Dir()
	if !strings.HasSuffix(filepath.ToSlash(got), "/build/host") {
		t.Errorf("Dir = %s, want <checkout>/build/host", got)
	}
	self := filepath.Join(got, "..", "..", "sdk", "go", "internal", "simproc", "simproc.go")
	if _, err := os.Stat(self); err != nil {
		t.Fatalf("Dir = %s is not build/host at the root holding this package: %v", got, err)
	}
}

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
