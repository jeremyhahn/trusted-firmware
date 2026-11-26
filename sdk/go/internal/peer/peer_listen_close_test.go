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

// listenFreeNow checks that addr can be listened on again at once: the
// tcp-listen: port a row used is free for the next row's listener
// (docs/boards/stm32h573i-dk.md, "each row opens a new listener").
func listenFreeNow(t *testing.T, addr string) {
	t.Helper()
	l, err := net.Listen("tcp", addr)
	if err != nil {
		t.Fatalf("listening on %s again: %v (the port was not let go)", addr, err)
	}
	_ = l.Close()
}

// TestATCPListenPortIsFreeAfterEachOutcome: Open and OpenCmd close the
// tcp-listen: listener on every return, so the next row's listener binds the
// same port whether the board stayed silent, the reset failed or the board's
// dial was taken, and the board's later dials are refused (#599).
func TestATCPListenPortIsFreeAfterEachOutcome(t *testing.T) {
	addr := freeAddr(t)
	port := TCPListenPrefix + addr

	t.Run("no dial", func(t *testing.T) {
		c, err := Open(port, 200*time.Millisecond)
		if err == nil {
			_ = c.Close()
			t.Fatal("Open with no board dialling succeeded")
		}
		listenFreeNow(t, addr)
	})

	t.Run("failed reset", func(t *testing.T) {
		start := time.Now()
		c, err := OpenCmd(port, []string{"false"}, 5*time.Second)
		took := time.Since(start)
		if err == nil {
			_ = c.Close()
			t.Fatal("OpenCmd with a failing reset succeeded")
		}
		msg := err.Error()
		if !strings.Contains(msg, "resetting the board") || !strings.Contains(msg, port) {
			t.Fatalf("error %q does not name %q and the failed reset", msg, port)
		}
		if took >= 2*time.Second {
			t.Fatalf("OpenCmd took %v after a failed reset; it must not wait for a dial", took)
		}
		listenFreeNow(t, addr)
	})

	t.Run("dial taken", func(t *testing.T) {
		stop := make(chan struct{})
		defer close(stop)
		got := dialUntil(addr, stop)
		c, err := Open(port, 5*time.Second)
		if err != nil {
			t.Fatalf("Open: %v", err)
		}
		var board net.Conn
		select {
		case board = <-got:
		case <-time.After(5 * time.Second):
			_ = c.Close()
			t.Fatal("the board's dial never connected")
		}
		second, err := net.DialTimeout("tcp", addr, time.Second)
		if err == nil {
			_ = second.Close()
			_ = board.Close()
			_ = c.Close()
			t.Fatal("a second dial was accepted after Open took the board's connection")
		}
		_ = board.Close()
		_ = c.Close()
		listenFreeNow(t, addr)
	})
}

// TestOpenOnABusyTCPListenPortFailsAtOnceNamingIt: a tcp-listen: port another
// listener holds fails before any wait, naming the port, and the holder keeps
// its listener (#599).
func TestOpenOnABusyTCPListenPortFailsAtOnceNamingIt(t *testing.T) {
	held, err := net.Listen("tcp", freeAddr(t))
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = held.Close() }()
	port := TCPListenPrefix + held.Addr().String()

	start := time.Now()
	c, err := Open(port, 5*time.Second)
	took := time.Since(start)
	if err == nil {
		_ = c.Close()
		t.Fatal("Open on a held port succeeded")
	}
	if !strings.Contains(err.Error(), port) {
		t.Fatalf("error %q does not name %q", err, port)
	}
	if took >= time.Second {
		t.Fatalf("Open on a held port took %v; it must fail at once", took)
	}

	d, err := net.DialTimeout("tcp", held.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("dialling the held port afterwards: %v", err)
	}
	defer func() { _ = d.Close() }()
	if err := held.(*net.TCPListener).SetDeadline(time.Now().Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	a, err := held.Accept()
	if err != nil {
		t.Fatalf("the holder no longer accepts: %v", err)
	}
	_ = a.Close()
}

// nonLoopbackIP returns the host's first non-loopback global unicast or
// private address, IPv4 first, or "" when it has only loopback.
func nonLoopbackIP() string {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return ""
	}
	var v6 string
	for _, a := range addrs {
		n, ok := a.(*net.IPNet)
		if !ok || n.IP.IsLoopback() || (!n.IP.IsGlobalUnicast() && !n.IP.IsPrivate()) {
			continue
		}
		if n.IP.To4() != nil {
			return n.IP.String()
		}
		if v6 == "" {
			v6 = n.IP.String()
		}
	}
	return v6
}

// TestABareTCPListenPortBindsEveryInterface: tcp-listen:<port> with no host
// listens on every interface (docs/boards/stm32h573i-dk.md): listenTCP's bound
// address is unspecified, and where the host has a non-loopback address a
// board dialling it reaches Open and its bytes arrive (#599).
func TestABareTCPListenPortBindsEveryInterface(t *testing.T) {
	_, p, err := net.SplitHostPort(freeAddr(t))
	if err != nil {
		t.Fatal(err)
	}
	l, err := listenTCP(p)
	if err != nil {
		t.Fatalf("listenTCP(%q): %v", p, err)
	}
	bound := l.Addr().(*net.TCPAddr)
	_ = l.Close()
	if !bound.IP.IsUnspecified() {
		t.Fatalf("listenTCP(%q) bound %s; want every interface (an unspecified IP)", p, bound)
	}

	host := nonLoopbackIP()
	if host == "" {
		host = "localhost"
		t.Log("the host has only loopback; the dial exercises loopback only")
	}
	stop := make(chan struct{})
	defer close(stop)
	got := dialUntil(net.JoinHostPort(host, p), stop)

	c, err := Open(TCPListenPrefix+p, 5*time.Second)
	if err != nil {
		t.Fatalf("Open(%q): %v", TCPListenPrefix+p, err)
	}
	defer func() { _ = c.Close() }()
	var board net.Conn
	select {
	case board = <-got:
	case <-time.After(5 * time.Second):
		t.Fatal("the board's dial never connected")
	}
	defer func() { _ = board.Close() }()

	if _, err := board.Write([]byte{0x5a}); err != nil {
		t.Fatal(err)
	}
	if nc, ok := c.(net.Conn); ok {
		_ = nc.SetReadDeadline(time.Now().Add(5 * time.Second))
	}
	b := make([]byte, 1)
	if _, err := io.ReadFull(c, b); err != nil {
		t.Fatalf("reading the board's byte: %v", err)
	}
	if b[0] != 0x5a {
		t.Fatalf("read %#x, want 0x5a", b[0])
	}
}
