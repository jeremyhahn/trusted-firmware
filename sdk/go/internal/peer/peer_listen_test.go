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

// freeAddr is a loopback address no one listens on yet.
func freeAddr(t *testing.T) string {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := l.Addr().String()
	_ = l.Close()
	return addr
}

// dialUntil plays the STM32H573I-DK's ETH_MAC=1 wire (hal_eth_wire_tcp.c):
// the board dials the host and listens on nothing, redialling while refused.
func dialUntil(addr string, stop <-chan struct{}) <-chan net.Conn {
	got := make(chan net.Conn, 1)
	go func() {
		for {
			if c, err := net.DialTimeout("tcp", addr, time.Second); err == nil {
				got <- c
				return
			}
			wait := time.NewTimer(10 * time.Millisecond)
			select {
			case <-stop:
				wait.Stop()
				return
			case <-wait.C:
			}
		}
	}()
	return got
}

func TestOpenAcceptsTheDevicesDialOnATCPListenPort(t *testing.T) {
	addr := freeAddr(t)
	stop := make(chan struct{})
	defer close(stop)
	board := dialUntil(addr, stop)

	c, err := Open(TCPListenPrefix+addr, 5*time.Second)
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

func TestOpenOnATCPListenPortFailsWithinItsBoundWhenNoDeviceDials(t *testing.T) {
	addr := freeAddr(t)
	begin := time.Now()
	_, err := Open(TCPListenPrefix+addr, 200*time.Millisecond)
	if err == nil || !strings.Contains(err.Error(), TCPListenPrefix+addr) {
		t.Fatalf("Open with no device: %v, want an error naming %s", err, TCPListenPrefix+addr)
	}
	if took := time.Since(begin); took > 3*time.Second {
		t.Fatalf("Open with no device took %v, bound 200ms", took)
	}
}

func TestOpenCmdResetsThenAcceptsTheRebootedBoardsDial(t *testing.T) {
	addr := freeAddr(t)
	stop := make(chan struct{})
	defer close(stop)
	// The rebooted board dials once its reset is done: the hook runs for
	// real, then the board starts dialling.
	var board <-chan net.Conn
	saved := runHook
	t.Cleanup(func() { runHook = saved })
	runHook = func(hook string, bound time.Duration) error {
		if err := saved(hook, bound); err != nil {
			return err
		}
		board = dialUntil(addr, stop)
		return nil
	}
	c, err := OpenCmd(TCPListenPrefix+addr, []string{"true"}, 5*time.Second)
	if err != nil {
		t.Fatalf("OpenCmd: %v", err)
	}
	b := <-board
	_ = b.Close()
	_ = c.Close()
	if _, err := OpenCmd(TCPListenPrefix+addr, []string{"false"}, time.Second); err == nil {
		t.Fatal("OpenCmd with a failing reset succeeded")
	}
}

// TestOpenCmdDropsTheBoardsPreResetConnection: on a tcp-listen: port the
// listener is up before the reset, so the board's pre-reset socket can be
// queued on it. OpenCmd closes every connection that arrived before the
// reset returned and takes the rebooted board's fresh dial (#584, Codex
// round 7).
func TestOpenCmdDropsTheBoardsPreResetConnection(t *testing.T) {
	addr := freeAddr(t)
	fresh := make(chan net.Conn, 1)
	saved := runHook
	t.Cleanup(func() { runHook = saved })
	runHook = func(string, time.Duration) error {
		// The board's socket from before the reset, already queued.
		stale, err := net.DialTimeout("tcp", addr, time.Second)
		if err != nil {
			return err
		}
		_, _ = stale.Write([]byte("stale"))
		// The reset drops it, and the rebooted board dials again.
		_ = stale.Close()
		go func() {
			time.Sleep(100 * time.Millisecond)
			c, err := net.DialTimeout("tcp", addr, time.Second)
			if err != nil {
				fresh <- nil
				return
			}
			_, _ = c.Write([]byte("fresh"))
			fresh <- c
		}()
		return nil
	}
	c, err := OpenCmd(TCPListenPrefix+addr, []string{"reset"}, 5*time.Second)
	if err != nil {
		t.Fatalf("OpenCmd: %v", err)
	}
	defer func() { _ = c.Close() }()
	b := <-fresh
	if b == nil {
		t.Fatal("the rebooted board's dial failed")
	}
	defer func() { _ = b.Close() }()
	nc, ok := c.(net.Conn)
	if !ok {
		t.Fatalf("OpenCmd returned %T, want a net.Conn", c)
	}
	_ = nc.SetReadDeadline(time.Now().Add(2 * time.Second))
	got := make([]byte, 5)
	if _, err := io.ReadFull(nc, got); err != nil || string(got) != "fresh" {
		t.Fatalf("OpenCmd's connection read %q (%v), want the rebooted board's \"fresh\"", got, err)
	}
}

func TestATCPListenPortIsNotReset(t *testing.T) {
	for port, resets := range map[string]bool{
		"serial-port":               false,
		TCPPrefix + "board:21574":   true,
		TCPListenPrefix + ":21574":  true,
		TCPListenPrefix + "x:21574": true,
	} {
		if got := IsTCP(port); got != resets {
			t.Errorf("IsTCP(%q) = %v, want %v", port, got, resets)
		}
		if got := (Target{Port: port}).Resets(); got == resets {
			t.Errorf("Target{Port: %q}.Resets() = %v", port, got)
		}
	}
}
