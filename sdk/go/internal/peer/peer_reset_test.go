// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
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

// TestRunHookBoundsAHookWhoseChildHoldsItsOutput runs a hook that starts a
// child holding the hook's output open and then either blocks or exits at
// once: runHook returns within its bound plus hookWaitDelay, and the child is
// gone after it returns (Codex on #568).
func TestRunHookBoundsAHookWhoseChildHoldsItsOutput(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("reads the child's state from /proc")
	}
	for _, tc := range []struct {
		name, last string
		want       func(error) bool
	}{
		{name: "the hook waits on its child", last: "wait", want: func(err error) bool {
			return err != nil && strings.Contains(err.Error(), "killed")
		}},
		{name: "the hook exits first", last: "exit 0", want: func(err error) bool {
			return errors.Is(err, exec.ErrWaitDelay)
		}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			script := filepath.Join(dir, "hook.sh")
			pidFile := filepath.Join(dir, "child.pid")
			body := "sleep 600 &\necho $! > \"$1.tmp\"\nmv \"$1.tmp\" \"$1\"\n" + tc.last + "\n"
			if err := os.WriteFile(script, []byte(body), 0o600); err != nil {
				t.Fatal(err)
			}
			const bound = time.Second
			start := time.Now()
			err := runHook("sh "+script+" "+pidFile, bound)
			took := time.Since(start)
			if !tc.want(err) {
				t.Fatalf("runHook: %v", err)
			}
			if limit := bound + hookWaitDelay + 5*time.Second; took > limit {
				t.Fatalf("runHook took %v, want under %v", took, limit)
			}
			raw, err := os.ReadFile(pidFile)
			if err != nil {
				t.Fatalf("the hook's child pid: %v", err)
			}
			pid, err := strconv.Atoi(strings.TrimSpace(string(raw)))
			if err != nil {
				t.Fatal(err)
			}
			if !childGone(t, pid, 10*time.Second) {
				t.Fatalf("the hook's child %d still runs after runHook returned", pid)
			}
		})
	}
}

// childGone reports whether pid has exited (gone from /proc, or a zombie
// left for its reaper) before within passes, checking on a ticker.
func childGone(t *testing.T, pid int, within time.Duration) bool {
	t.Helper()
	stat := filepath.Join("/proc", strconv.Itoa(pid), "stat")
	gone := func() bool {
		raw, err := os.ReadFile(stat)
		if err != nil {
			return true
		}
		// The state follows the parenthesised command name.
		i := strings.LastIndexByte(string(raw), ')')
		return i < 0 || strings.HasPrefix(string(raw[i+1:]), " Z")
	}
	deadline := time.NewTimer(within)
	defer deadline.Stop()
	tick := time.NewTicker(20 * time.Millisecond)
	defer tick.Stop()
	for !gone() {
		select {
		case <-deadline.C:
			return gone()
		case <-tick.C:
		}
	}
	return true
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

// TestOpenResetWaitsOutADHCPReLease runs openReset on a fake clock: the
// board's server comes back 6 s after the reset (a new DHCP lease), which
// TCPResetBound waits out and StartBound does not (#583).
func TestOpenResetWaitsOutADHCPReLease(t *testing.T) {
	saved, savedHook, savedNow, savedPause := dialTCP, runHook, resetNow, resetPause
	t.Cleanup(func() { dialTCP, runHook, resetNow, resetPause = saved, savedHook, savedNow, savedPause })
	runHook = func(string, time.Duration) error { return nil }
	for _, tc := range []struct {
		name  string
		back  time.Duration // 0: the board never comes back
		bound time.Duration
		want  string // "" wants the wire back
	}{
		{name: "back 6s after the reset", back: 6 * time.Second, bound: TCPResetBound},
		{name: "never back within the bound", bound: TCPResetBound, want: "did not come back"},
		{name: "the serial bound is too short for a re-lease", back: 6 * time.Second, bound: StartBound, want: "did not come back"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			clock := time.Now()
			reset := clock
			resetNow = func() time.Time { return clock }
			resetPause = func(d time.Duration) { clock = clock.Add(d) }
			dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
				if tc.back == 0 || clock.Sub(reset) < tc.back {
					return nil, errors.New("connection refused")
				}
				a, b := net.Pipe()
				go func() { _, _ = b.Write([]byte{0xA5}) }()
				return a, nil
			}
			c, err := openReset("board:21574", "bench-reset", tc.bound)
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

// TestOpenResetWaitsPastTheTCPReadFloor: a tcp: wire is a floorConn, whose
// every Read times out within tcpReadFloor. A board that accepts and sends
// BOOT later than that, inside the bound, has started; one that sends
// nothing within the bound is silent, after the whole bound.
// timeoutSignalConn closes timedOut the first time a Read ends at its
// deadline, so a test can act once the reader has waited past one floor.
type timeoutSignalConn struct {
	net.Conn
	timedOut chan struct{}
	once     sync.Once
}

func (c *timeoutSignalConn) Read(p []byte) (int, error) {
	n, err := c.Conn.Read(p)
	if errors.Is(err, os.ErrDeadlineExceeded) {
		c.once.Do(func() { close(c.timedOut) })
	}
	return n, err
}

func TestOpenResetWaitsPastTheTCPReadFloor(t *testing.T) {
	saved, savedHook := dialTCP, runHook
	t.Cleanup(func() { dialTCP, runHook = saved, savedHook })
	runHook = func(string, time.Duration) error { return nil }
	for _, tc := range []struct {
		name  string
		boot  bool // the board sends BOOT once a read has timed out at the floor
		bound time.Duration
	}{
		{name: "BOOT after a read timed out at the floor", boot: true, bound: StartBound},
		{name: "no BOOT within the bound", bound: 3 * tcpReadFloor},
	} {
		t.Run(tc.name, func(t *testing.T) {
			dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
				a, b := net.Pipe()
				t.Cleanup(func() { _ = b.Close() })
				timedOut := make(chan struct{})
				if tc.boot {
					go func() {
						<-timedOut
						_, _ = b.Write([]byte{0xA5})
					}()
				}
				return &floorConn{Conn: &timeoutSignalConn{Conn: a, timedOut: timedOut}, floor: tcpReadFloor}, nil
			}
			began := time.Now()
			c, err := openReset("board:21574", "bench-reset", tc.bound)
			if tc.boot {
				if err != nil {
					t.Fatalf("openReset: %v", err)
				}
				_ = c.Close()
				return
			}
			if !errors.Is(err, ErrSilent) {
				t.Fatalf("openReset error %v, want ErrSilent", err)
			}
			if waited := time.Since(began); waited < tc.bound {
				t.Fatalf("openReset gave up after %s, before its bound %s", waited, tc.bound)
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
