// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os/exec"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// tcpRedial is how long openReset waits between dials of a board whose
// server is not back up after its reset.
const tcpRedial = 100 * time.Millisecond

// TCPResetBound bounds a tcp: board's reset through its hook, its reconnect
// and its first bytes. It is longer than StartBound because a board on the
// network (the ESP32-P4) takes a new DHCP lease after every reset, and the
// lease alone can outlast StartBound (#583). A serial board keeps StartBound.
const TCPResetBound = 30 * time.Second

// hookWaitDelay bounds how long runHook still waits for the hook's output
// once its bound has passed and the hook was killed: a child the hook left
// behind may hold the output pipe open, and without this bound the wait
// would last as long as that child does.
const hookWaitDelay = 2 * time.Second

// resetNow and resetPause are openReset's clock, so a test can run a
// reset that takes seconds without waiting for them.
var (
	resetNow   = time.Now
	resetPause = time.Sleep
)

// StartBooted is Start for a row that needs the peer's BOOT event: the
// simulator and a serial board are reset as Start opens them. A board whose
// wire is TCP is not (Resets), so StartBooted runs the target's Hook,
// dials the board until its server is back, and returns once it transmits;
// with no Hook the row is skipped with NoResetOnOpen, as nothing can make
// the board send BOOT.
func StartBooted(tb testing.TB, sim simproc.Binary, args ...string) io.ReadWriteCloser {
	tb.Helper()
	t, err := FromEnv()
	if err != nil {
		tb.Fatal(err)
	}
	if t.Resets() {
		return Start(tb, sim, args...)
	}
	if t.Hook == "" {
		tb.Skipf("%s: %s=%s, and %s names no %s<command> to reset the board", NoResetOnOpen, PortEnv, t.Port, ResetEnv, ResetCmdPrefix)
	}
	c, err := openReset(strings.TrimPrefix(t.Port, TCPPrefix), t.Hook, TCPResetBound)
	if err != nil {
		tb.Fatal(fmt.Errorf("%s: %w", t.Port, err))
	}
	tb.Cleanup(func() { _ = c.Close() })
	return c
}

// runHook runs a reset hook, the command line split on spaces, bounded by
// bound; a variable so a test can see the hook run without a board.
var runHook = func(hook string, bound time.Duration) error {
	ctx, cancel := context.WithTimeout(context.Background(), bound)
	defer cancel()
	argv := strings.Fields(hook)
	cmd := exec.CommandContext(ctx, argv[0], argv[1:]...) // #nosec G204 -- the bench operator names its own reset hook
	killHookGroup(cmd)
	cmd.WaitDelay = hookWaitDelay
	out, err := cmd.CombinedOutput()
	endHookGroup(cmd)
	if err != nil {
		if msg := bytes.TrimSpace(out); len(msg) > 0 {
			return fmt.Errorf("%w: %s", err, msg)
		}
		return err
	}
	return nil
}

// openReset resets the board at addr through hook, dials it until its wire
// server answers, and waits for its first transmission, all within bound.
// The connection is dialled after the hook, so what it carries is the new
// application's: its BOOT event first.
func openReset(addr, hook string, bound time.Duration) (io.ReadWriteCloser, error) {
	start := resetNow()
	deadline := start.Add(bound)
	if err := runHook(hook, bound); err != nil {
		return nil, fmt.Errorf("%s=%s%s: %w", ResetEnv, ResetCmdPrefix, hook, err)
	}
	var c io.ReadWriteCloser
	for {
		var err error
		c, err = dialTCP("tcp", addr, deadline.Sub(resetNow()))
		if err == nil {
			break
		}
		if resetNow().Add(tcpRedial).After(deadline) {
			return nil, fmt.Errorf("the board's wire did not come back after its reset: %w", err)
		}
		resetPause(tcpRedial)
	}
	nc, ok := c.(net.Conn)
	if ok {
		if err := nc.SetReadDeadline(deadline); err != nil {
			_ = c.Close()
			return nil, err
		}
	}
	// A tcp: wire is a floorConn, whose every Read times out within its
	// 200ms floor: a board that accepts before it sends BOOT (the ESP32-P4
	// listens before its init ends) is still starting, so read on until
	// the reset's own deadline passes.
	var s io.ReadWriteCloser
	var err error
	var ne net.Error
	for {
		s, err = awaitStart(c, deadline.Sub(resetNow()))
		if err == nil || !errors.As(err, &ne) || !ne.Timeout() || !resetNow().Before(deadline) {
			break
		}
	}
	if err != nil {
		_ = c.Close()
		if errors.As(err, &ne) && ne.Timeout() {
			return nil, fmt.Errorf("%w (waited %s)", ErrSilent, resetNow().Sub(start).Round(time.Millisecond))
		}
		return nil, err
	}
	if ok {
		if err := nc.SetReadDeadline(time.Time{}); err != nil {
			_ = c.Close()
			return nil, err
		}
	}
	return s, nil
}
