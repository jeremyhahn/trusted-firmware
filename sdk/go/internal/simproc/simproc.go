// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package simproc launches the host builds of the firmware (tfwsim and
// tfwsim-atecc608a) as subprocesses and speaks to them over their stdio.
//
// It is the one launcher: the SDK's own cross-implementation tests and the
// public sdk/go/tfwsim package for downstream tests both go through it, so there
// is one answer to "where is the binary" and one answer to "what happens when
// it is not there".
package simproc

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
)

// DirEnv names the environment variable that points at the directory holding
// the built simulators, for a caller whose checkout of this repository is not
// the one this package was compiled from -- a downstream module, a -trimpath
// build, a CI job that built the binaries elsewhere.
const DirEnv = "TFWSIM_DIR"

// Binary names one simulator and the make invocation that builds it.
type Binary struct {
	Name string // file name in the build directory
	Make string // what to run when it is missing
}

var (
	// Software is the software-tier simulator.
	Software = Binary{Name: "tfwsim", Make: "make tfwsim"}
	// Element is the ATECC608A-tier simulator.
	Element = Binary{Name: "tfwsim-atecc608a", Make: "make tfwsim-atecc608a"}
	// Auto is the Mega's default CRYPTO=auto tier: the element when one
	// answers, else the soft root. It takes the element tier's options, and
	// --no-element takes the part off the bus.
	Auto = Binary{Name: "tfwsim-auto", Make: "make tfwsim-auto"}
	// App is the software-tier simulator with test/app/example linked, the
	// only image that answers APP_INFO.
	App = Binary{Name: "tfwsim-app-heartbeat", Make: "make tfwsim-app-example"}
)

// NotBuiltError reports a simulator binary that is not where it should be.
//
// It is a failure and never a reason to skip: a skipped cross-implementation
// test reports success while checking nothing.
type NotBuiltError struct {
	Binary Binary
	Path   string
	Err    error
}

func (e *NotBuiltError) Error() string {
	return fmt.Sprintf("%s not built at %s -- run `%s` first (or set %s to the directory holding it): %v",
		e.Binary.Name, e.Path, e.Binary.Make, DirEnv, e.Err)
}

func (e *NotBuiltError) Unwrap() error { return e.Err }

// Dir is the directory the simulators are looked up in: $TFWSIM_DIR if set,
// otherwise build/host in the checkout this source file lives in.
func Dir() string {
	if d := os.Getenv(DirEnv); d != "" {
		return d
	}
	_, file, _, _ := runtime.Caller(0)
	// <root>/sdk/go/internal/simproc/simproc.go
	return filepath.Join(filepath.Dir(file), "..", "..", "..", "..", "build", "host")
}

// Locate returns the path of b, or a *NotBuiltError.
func Locate(b Binary) (string, error) {
	path := filepath.Join(Dir(), b.Name)
	if _, err := os.Stat(path); err != nil {
		return "", &NotBuiltError{Binary: b, Path: path, Err: err}
	}
	return path, nil
}

// Conn is a running simulator. Reads and writes go to its stdout and stdin.
type Conn struct {
	cmd *exec.Cmd
	in  io.WriteCloser
	out io.ReadCloser
}

func (c *Conn) Read(p []byte) (int, error)  { return c.out.Read(p) }
func (c *Conn) Write(p []byte) (int, error) { return c.in.Write(p) }

// Close stops the simulator and reaps it.
func (c *Conn) Close() error {
	_ = c.in.Close()
	_ = c.cmd.Process.Kill()
	_, _ = c.cmd.Process.Wait()
	return nil
}

// Start launches b with args and stops it when the test ends. A missing binary
// fails the test.
func Start(tb testing.TB, b Binary, args ...string) *Conn {
	tb.Helper()

	path, err := Locate(b)
	if err != nil {
		tb.Fatal(err)
	}
	cmd := exec.Command(path, args...) // #nosec G204 -- path is Locate's answer, the tfwsim binary this repo's own make target built, and args are the test's literal flags; no shell, no outside input
	cmd.Stderr = os.Stderr
	in, err := cmd.StdinPipe()
	if err != nil {
		tb.Fatal(err)
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		tb.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		tb.Fatal(err)
	}
	c := &Conn{cmd: cmd, in: in, out: out}
	tb.Cleanup(func() { _ = c.Close() })
	return c
}
