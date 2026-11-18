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
	c, err := openReset(strings.TrimPrefix(t.Port, TCPPrefix), t.Hook, StartBound)
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
	out, err := exec.CommandContext(ctx, argv[0], argv[1:]...).CombinedOutput() // #nosec G204 -- the bench operator names its own reset hook
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
	deadline := time.Now().Add(bound)
	if err := runHook(hook, bound); err != nil {
		return nil, fmt.Errorf("%s=%s%s: %w", ResetEnv, ResetCmdPrefix, hook, err)
	}
	var c io.ReadWriteCloser
	for {
		var err error
		c, err = dialTCP("tcp", addr, time.Until(deadline))
		if err == nil {
			break
		}
		if time.Now().Add(tcpRedial).After(deadline) {
			return nil, fmt.Errorf("the board's wire did not come back after its reset: %w", err)
		}
		time.Sleep(tcpRedial)
	}
	nc, ok := c.(net.Conn)
	if ok {
		if err := nc.SetReadDeadline(deadline); err != nil {
			_ = c.Close()
			return nil, err
		}
	}
	s, err := awaitStart(c, time.Until(deadline))
	if err != nil {
		_ = c.Close()
		var ne net.Error
		if errors.As(err, &ne) && ne.Timeout() {
			return nil, fmt.Errorf("%w (waited %s)", ErrSilent, bound)
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
