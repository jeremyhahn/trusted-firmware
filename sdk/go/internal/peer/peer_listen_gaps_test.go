// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"io"
	"net"
	"strings"
	"testing"
	"time"
)

// TestOpenOnATCPListenPortThatIsAlreadyBoundFailsAtOnce: a bind failure is
// returned before any accept wait, naming the port, not as a timeout.
func TestOpenOnATCPListenPortThatIsAlreadyBoundFailsAtOnce(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = l.Close() })
	addr := l.Addr().String()

	begin := time.Now()
	c, err := Open(TCPListenPrefix+addr, 5*time.Second)
	took := time.Since(begin)
	if err == nil {
		_ = c.Close()
		t.Fatalf("Open on a bound %s succeeded", addr)
	}
	if !strings.Contains(err.Error(), TCPListenPrefix+addr) {
		t.Fatalf("Open on a bound address: %v, want an error naming %s", err, TCPListenPrefix+addr)
	}
	if strings.Contains(err.Error(), "no connection from the board") {
		t.Fatalf("Open on a bound address: %v, want the bind failure, not the accept timeout", err)
	}
	if took >= time.Second {
		t.Fatalf("Open on a bound address took %v, want it to fail at once", took)
	}
}

// TestOpenOnABarePortTCPListenPortListensOnEveryInterface: tcp-listen:<port>
// binds every interface, so the board's dial to loopback is accepted.
func TestOpenOnABarePortTCPListenPortListensOnEveryInterface(t *testing.T) {
	addr := freeAddr(t)
	_, p, err := net.SplitHostPort(addr)
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	defer close(stop)
	board := dialUntil("127.0.0.1:"+p, stop)

	c, err := Open(TCPListenPrefix+p, 5*time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
	b := <-board
	defer func() { _ = b.Close() }()

	if _, err := b.Write([]byte("hi")); err != nil {
		t.Fatal(err)
	}
	var got [2]byte
	if _, err := io.ReadFull(c, got[:]); err != nil || string(got[:]) != "hi" {
		t.Fatalf("read %q, %v; want the board's bytes", got, err)
	}
	if _, err := c.Write([]byte("ok")); err != nil {
		t.Fatal(err)
	}
	if _, err := io.ReadFull(b, got[:]); err != nil || string(got[:]) != "ok" {
		t.Fatalf("board read %q, %v; want the host's bytes", got, err)
	}
}

// TestOpenCmdOnATCPListenPortReleasesTheListenerWhenTheResetFails: a failed
// reset names the port and closes the listener, so the port is free again.
func TestOpenCmdOnATCPListenPortReleasesTheListenerWhenTheResetFails(t *testing.T) {
	addr := freeAddr(t)
	c, err := OpenCmd(TCPListenPrefix+addr, []string{"false"}, time.Second)
	if err == nil {
		_ = c.Close()
		t.Fatal("OpenCmd with a failing reset succeeded")
	}
	if !strings.Contains(err.Error(), "resetting the board") || !strings.Contains(err.Error(), TCPListenPrefix+addr) {
		t.Fatalf("OpenCmd with a failing reset: %v, want \"resetting the board\" and %s", err, TCPListenPrefix+addr)
	}
	l, err := net.Listen("tcp", addr)
	if err != nil {
		t.Fatalf("listen on %s after the failed reset: %v, want the listener released", addr, err)
	}
	_ = l.Close()
}
