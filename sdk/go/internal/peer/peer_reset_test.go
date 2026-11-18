// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"errors"
	"fmt"
	"io"
	"net"
	"runtime"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// A cmd: reset is a TCP board's hook; a serial board is reset by DTR, so a
// hook named for one is refused, as is a cmd: that names no command.
func TestFromEnvReadsAResetHookForATCPBoard(t *testing.T) {
	for _, tc := range []struct {
		port, reset string
		hook, err   string
	}{
		{port: TCPPrefix + "board:21574", reset: "cmd:bench-reset p4", hook: "bench-reset p4"},
		{port: TCPPrefix + "board:21574", reset: ""},
		{port: "ttyBOARD", reset: "cmd:bench-reset p4", err: "reset by DTR"},
		{port: TCPPrefix + "board:21574", reset: "cmd:  ", err: ResetCmdPrefix + "<command>"},
	} {
		t.Setenv(PortEnv, tc.port)
		t.Setenv(BoardEnv, "esp32p4-poe")
		t.Setenv(ResetEnv, tc.reset)
		got, err := FromEnv()
		if tc.err != "" {
			if err == nil || !strings.Contains(err.Error(), tc.err) {
				t.Errorf("%s=%q on %q: err = %v, want one naming %q", ResetEnv, tc.reset, tc.port, err, tc.err)
			}
			continue
		}
		if err != nil || got.Hook != tc.hook || got.QMP != "" {
			t.Errorf("%s=%q: target %+v, %v; want hook %q", ResetEnv, tc.reset, got, err, tc.hook)
		}
	}
}

// skipTB records a Skipf or Fatal and ends the goroutine, as testing.T does.
type skipTB struct {
	testing.TB
	skipped, fatal string
}

func (s *skipTB) Helper() {}
func (s *skipTB) Skipf(format string, args ...any) {
	s.skipped = fmt.Sprintf(format, args...)
	runtime.Goexit()
}

func (s *skipTB) Fatal(args ...any) {
	s.fatal = fmt.Sprint(args...)
	runtime.Goexit()
}

// startBootedIn runs StartBooted with tb on its own goroutine.
func startBootedIn(t *testing.T, tb testing.TB) io.ReadWriteCloser {
	t.Helper()
	var c io.ReadWriteCloser
	done := make(chan struct{})
	go func() {
		defer close(done)
		c = StartBooted(tb, simproc.Software)
	}()
	select {
	case <-done:
	case <-time.After(time.Minute):
		t.Fatal("StartBooted did not return within a minute")
	}
	return c
}

// noHook fails the test if a reset hook runs.
func noHook(t *testing.T) {
	saved := runHook
	t.Cleanup(func() { runHook = saved })
	runHook = func(hook string, _ time.Duration) error {
		t.Errorf("the reset hook %q ran", hook)
		return nil
	}
}

// A serial board is reset by DTR as Start opens it; no hook runs.
func TestStartBootedResetsASerialBoardByDTR(t *testing.T) {
	noHook(t)
	saved := openPort
	t.Cleanup(func() { openPort = saved })
	openPort = func(string, int) (io.ReadWriteCloser, error) {
		return &bootAfterPurge{&resettablePort{}, []byte{0xA5, 0x01}}, nil
	}
	t.Setenv(PortEnv, "ttyBOARD")
	t.Setenv(BoardEnv, "atmega2560")
	t.Setenv(ResetEnv, "")
	c := StartBooted(t, simproc.Software)
	got := make([]byte, 8)
	if n, err := c.Read(got); err != nil || string(got[:n]) != "\xa5\x01" {
		t.Fatalf("first read = %x, %v; want the BOOT event a501", got[:n], err)
	}
}

// A TCP board with no hook cannot be made to send BOOT: the row is skipped,
// with the reason, and nothing is dialled.
func TestStartBootedSkipsATCPBoardWithNoHook(t *testing.T) {
	noHook(t)
	saved := dialTCP
	t.Cleanup(func() { dialTCP = saved })
	dialTCP = func(_, addr string, _ time.Duration) (io.ReadWriteCloser, error) {
		t.Errorf("dialled %s", addr)
		return nil, errors.New("no dial")
	}
	t.Setenv(PortEnv, TCPPrefix+"board:21574")
	t.Setenv(BoardEnv, "esp32p4-poe")
	t.Setenv(ResetEnv, "")
	tb := &skipTB{TB: t}
	startBootedIn(t, tb)
	if !strings.HasPrefix(tb.skipped, NoResetOnOpen) || tb.fatal != "" {
		t.Fatalf("skip %q, fatal %q; want a skip for %q", tb.skipped, tb.fatal, NoResetOnOpen)
	}
}

// A TCP board with a hook is reset through it, dialled after it, and its
// BOOT kept for the first Read.
func TestStartBootedResetsATCPBoardThroughItsHook(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	var hooked atomic.Bool
	saved := runHook
	t.Cleanup(func() { runHook = saved })
	runHook = func(hook string, _ time.Duration) error {
		if hook != "bench-reset p4" {
			t.Errorf("hook %q ran, want %q", hook, "bench-reset p4")
		}
		hooked.Store(true)
		return nil
	}
	dialledAfter := make(chan bool, 1)
	go func() {
		c, err := l.Accept()
		if err != nil {
			dialledAfter <- false
			return
		}
		defer func() { _ = c.Close() }()
		dialledAfter <- hooked.Load()
		_, _ = c.Write([]byte{0xA5, 0x01})
		_, _ = io.Copy(io.Discard, c)
	}()
	t.Setenv(PortEnv, TCPPrefix+l.Addr().String())
	t.Setenv(BoardEnv, "esp32p4-poe")
	t.Setenv(ResetEnv, "cmd:bench-reset p4")
	c := StartBooted(t, simproc.Software)
	got := make([]byte, 8)
	if n, err := c.Read(got); err != nil || string(got[:n]) != "\xa5\x01" {
		t.Fatalf("first read = %x, %v; want the BOOT event a501", got[:n], err)
	}
	if !<-dialledAfter {
		t.Fatal("the board was dialled before its reset hook ran")
	}
}

// A failed hook or a board that never comes back fails StartBooted.
func TestStartBootedFailsWhenTheResetFails(t *testing.T) {
	t.Setenv(PortEnv, TCPPrefix+"127.0.0.1:1")
	t.Setenv(BoardEnv, "esp32p4-poe")
	t.Setenv(ResetEnv, "cmd:false")
	tb := &skipTB{TB: t}
	startBootedIn(t, tb)
	if !strings.Contains(tb.fatal, ResetEnv+"="+ResetCmdPrefix+"false") {
		t.Fatalf("fatal %q, want it to name the hook", tb.fatal)
	}
}

// The real hook runs the command and reports its exit and output.
func TestRunHookRunsTheCommand(t *testing.T) {
	if err := runHook("true", time.Minute); err != nil {
		t.Fatalf("true: %v", err)
	}
	if err := runHook("false", time.Minute); err == nil {
		t.Fatal("false: no error")
	}
	if err := runHook("ls /nonexistent-tfw-reset", time.Minute); err == nil || !strings.Contains(err.Error(), "nonexistent-tfw-reset") {
		t.Fatalf("ls: %v, want the command's output in the error", err)
	}
}

func TestOpenResetPaths(t *testing.T) {
	saved, savedHook := dialTCP, runHook
	t.Cleanup(func() { dialTCP, runHook = saved, savedHook })
	runHook = func(string, time.Duration) error { return nil }
	boot := func(c net.Conn) { _, _ = c.Write([]byte{0xA5}) }
	for _, tc := range []struct {
		name string
		// dial answers the n-th dial (from 0); nil refuses every one.
		dial func(n int) (io.ReadWriteCloser, error)
		want string // "" wants the wire back
	}{{
		name: "redials a board whose server is not back yet",
		dial: func(n int) (io.ReadWriteCloser, error) {
			if n < 2 {
				return nil, errors.New("connection refused")
			}
			a, b := net.Pipe()
			go boot(b)
			return a, nil
		},
	}, {
		name: "a wire that is not a net.Conn",
		dial: func(int) (io.ReadWriteCloser, error) {
			a, b := net.Pipe()
			go boot(b)
			return pipeEnd{a}, nil
		},
	}, {
		name: "the wire never comes back",
		want: "did not come back",
	}, {
		name: "a silent board",
		dial: func(int) (io.ReadWriteCloser, error) {
			a, b := net.Pipe()
			t.Cleanup(func() { _ = b.Close() })
			return a, nil
		},
		want: "sent nothing",
	}, {
		name: "the read deadline is refused",
		dial: func(int) (io.ReadWriteCloser, error) {
			a, b := net.Pipe()
			t.Cleanup(func() { _ = b.Close() })
			return &deadlineConn{Conn: a, fail: 1}, nil
		},
		want: "deadline refused",
	}, {
		name: "clearing the read deadline is refused",
		dial: func(int) (io.ReadWriteCloser, error) {
			a, b := net.Pipe()
			go boot(b)
			return &deadlineConn{Conn: a, fail: 2}, nil
		},
		want: "deadline refused",
	}, {
		name: "the board closes the wire",
		dial: func(int) (io.ReadWriteCloser, error) {
			a, b := net.Pipe()
			_ = b.Close()
			return pipeEnd{a}, nil
		},
		want: "waiting for the board to start",
	}} {
		t.Run(tc.name, func(t *testing.T) {
			n := 0
			dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
				defer func() { n++ }()
				if tc.dial == nil {
					return nil, errors.New("connection refused")
				}
				return tc.dial(n)
			}
			c, err := openReset("board:21574", "bench-reset", 500*time.Millisecond)
			if tc.want == "" {
				if err != nil {
					t.Fatalf("openReset: %v", err)
				}
				_ = c.Close()
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("openReset error %v, want one naming %q", err, tc.want)
			}
		})
	}
}

// TestOpenResetWireKeepsReadDeadlines: a board that sends its BOOT and then
// goes silent fails a read by the deadline the Device sets on the wire, not at
// the test's timeout (Codex P1 on #568).
func TestOpenResetWireKeepsReadDeadlines(t *testing.T) {
	saved, savedHook := dialTCP, runHook
	t.Cleanup(func() { dialTCP, runHook = saved, savedHook })
	runHook = func(string, time.Duration) error { return nil }
	dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
		a, b := net.Pipe()
		t.Cleanup(func() { _ = b.Close() })
		go func() { _, _ = b.Write([]byte{0xA5}) }()
		return a, nil
	}
	c, err := openReset("board:21574", "bench-reset", 500*time.Millisecond)
	if err != nil {
		t.Fatalf("openReset: %v", err)
	}
	t.Cleanup(func() { _ = c.Close() })
	dc, ok := c.(interface {
		SetReadDeadline(time.Time) error
		SetDeadline(time.Time) error
	})
	if !ok {
		t.Fatalf("openReset's wire %T hides SetReadDeadline", c)
	}
	buf := make([]byte, 8)
	if n, err := c.Read(buf); err != nil || n != 1 || buf[0] != 0xA5 {
		t.Fatalf("first read = %d %v %x, want the BOOT byte", n, err, buf[:n])
	}
	for _, set := range []func(time.Time) error{dc.SetReadDeadline, dc.SetDeadline} {
		if err := set(time.Now().Add(50 * time.Millisecond)); err != nil {
			t.Fatalf("set deadline: %v", err)
		}
		done := make(chan error, 1)
		go func() { _, err := c.Read(buf); done <- err }()
		select {
		case err := <-done:
			var ne net.Error
			if !errors.As(err, &ne) || !ne.Timeout() {
				t.Fatalf("read on a stalled board = %v, want a timeout", err)
			}
		case <-time.After(2 * time.Second):
			t.Fatal("read on a stalled board ignored its deadline")
		}
	}
}

// TestStartedWithoutDeadlinesKeepsNone: a wire with no deadlines takes the
// call as a no-op.
func TestStartedWithoutDeadlinesKeepsNone(t *testing.T) {
	a, b := net.Pipe()
	t.Cleanup(func() { _ = a.Close(); _ = b.Close() })
	s := &started{rwc: pipeEnd{a}}
	if err := s.SetDeadline(time.Now()); err != nil {
		t.Fatalf("SetDeadline: %v", err)
	}
}
