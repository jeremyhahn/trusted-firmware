// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"errors"
	"io"
	"net"
	"strings"
	"testing"
	"time"
)

// A tcp: port dials the board's wire endpoint and hands back the stream as
// it is, with no reset.
func TestOpenTCPDialsTheWireEndpoint(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	got := make(chan []byte, 1)
	go func() {
		c, err := l.Accept()
		if err != nil {
			got <- nil
			return
		}
		defer func() { _ = c.Close() }()
		b := make([]byte, 3)
		_, _ = io.ReadFull(c, b)
		got <- b
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
	if _, err := c.Write([]byte{1, 2, 3}); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if b := <-got; string(b) != "\x01\x02\x03" {
		t.Fatalf("the endpoint read %x, want 010203", b)
	}
}

// A board on tcp: is found running, so no BOOT event comes, and a row that
// wraps the conn as a plain io.ReadWriter (the CAPS row's meddler) hides
// SetReadDeadline from tfw's read loop. The conn must still return from Read
// within a floor of its own, as serialport's does, or the row hangs until the
// test binary's timeout (#551). A byte the board sends later still arrives.
func TestOpenTCPReadReturnsBehindAWrapperThatHidesTheDeadline(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	send := make(chan byte)
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer func() { _ = c.Close() }()
		_, _ = c.Write([]byte{<-send})
		<-send
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
	defer close(send)
	hidden := struct{ io.Reader }{c}

	read := func() (byte, error) {
		done := make(chan error, 1)
		var b [1]byte
		go func() { _, err := io.ReadFull(hidden, b[:]); done <- err }()
		select {
		case err := <-done:
			return b[0], err
		case <-time.After(5 * time.Second):
			t.Fatal("Read on a silent tcp: board did not return: the row would hang")
			return 0, nil
		}
	}
	if _, err := read(); err == nil {
		t.Fatal("Read on a silent tcp: board returned no error, want a timeout")
	} else if ne, ok := err.(net.Error); !ok || !ne.Timeout() {
		t.Fatalf("Read on a silent tcp: board: %v, want a timeout", err)
	}
	send <- 0x5a
	b, err := read()
	if err != nil || b != 0x5a {
		t.Fatalf("after a timeout, Read = %#x, %v; want 0x5a", b, err)
	}
}

// A board that still holds the last row's socket closes the next row's at
// once (hal_uart.c refusing a second host): Open dials again and returns the
// connection the board kept.
func TestOpenTCPRedialsAConnectionTheBoardClosed(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	got := make(chan []byte, 1)
	go func() {
		first, err := l.Accept()
		if err != nil {
			got <- nil
			return
		}
		_ = first.Close()
		c, err := l.Accept()
		if err != nil {
			got <- nil
			return
		}
		defer func() { _ = c.Close() }()
		b := make([]byte, 3)
		_, _ = io.ReadFull(c, b)
		got <- b
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
	if _, err := c.Write([]byte{1, 2, 3}); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if b := <-got; string(b) != "\x01\x02\x03" {
		t.Fatalf("the kept connection read %x, want 010203", b)
	}
}

// A board that closes every connection is an error after a bounded number of
// dials, not a connection handed to the row.
func TestOpenTCPGivesUpOnABoardThatClosesEveryDial(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	dials := make(chan struct{}, 16)
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			dials <- struct{}{}
			_ = c.Close()
		}
	}()
	if c, err := Open(TCPPrefix+l.Addr().String(), time.Second); err == nil {
		_ = c.Close()
		t.Fatal("Open returned a connection the board closed")
	}
	if n := len(dials); n != tcpDials {
		t.Fatalf("the board saw %d dials, want %d", n, tcpDials)
	}
}

// A tcp: endpoint that does not answer is an error naming the port.
func TestOpenTCPNamesTheEndpointThatRefused(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := l.Addr().String()
	_ = l.Close()
	_, err = Open(TCPPrefix+addr, time.Second)
	if err == nil || !strings.Contains(err.Error(), TCPPrefix+addr) {
		t.Fatalf("Open of a closed endpoint: %v, want an error naming %s", err, TCPPrefix+addr)
	}
}

// A board that speaks inside tcpSettle has kept the connection: the byte
// openTCP read to find that out is the first the caller reads, not lost.
func TestOpenTCPKeepsTheByteABoardSentAtOnce(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer func() { _ = c.Close() }()
		_, _ = c.Write([]byte{7, 8, 9})
		_, _ = io.Copy(io.Discard, c)
	}()
	c, err := openTCP(l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("openTCP: %v", err)
	}
	defer func() { _ = c.Close() }()
	if _, ok := c.(*primed); !ok {
		t.Fatalf("openTCP returned %T, want *primed", c)
	}
	if n, err := c.Read(nil); n != 0 || err != nil {
		t.Fatalf("Read(nil) = %d, %v, want 0, nil", n, err)
	}
	b := make([]byte, 3)
	if _, err := io.ReadFull(c, b); err != nil {
		t.Fatalf("ReadFull: %v", err)
	}
	if string(b) != "\x07\x08\x09" {
		t.Fatalf("read %x, want 070809", b)
	}
}

// pipeEnd is a wire that is not a net.Conn, as a test's dialTCP may return.
type pipeEnd struct{ io.ReadWriteCloser }

// deadlineConn is a net.Conn whose fail-th SetReadDeadline call fails.
type deadlineConn struct {
	net.Conn
	fail, calls int
	closed      bool
}

func (d *deadlineConn) SetReadDeadline(t time.Time) error {
	d.calls++
	if d.calls == d.fail {
		return errors.New("deadline refused")
	}
	return d.Conn.SetReadDeadline(t)
}

func (d *deadlineConn) Close() error {
	d.closed = true
	return d.Conn.Close()
}

func TestOpenTCPErrorPaths(t *testing.T) {
	refused := func() string {
		l, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		addr := l.Addr().String()
		_ = l.Close()
		return addr
	}()
	for _, tc := range []struct {
		name string
		// dial replaces dialTCP; nil keeps net.DialTimeout.
		dial func() (io.ReadWriteCloser, *deadlineConn)
		addr string
		// want is a substring of the error; "" wants the wire back.
		want string
	}{{
		name: "dial failure",
		addr: refused,
		want: "refused",
	}, {
		name: "a wire that is not a net.Conn is returned as it is",
		dial: func() (io.ReadWriteCloser, *deadlineConn) {
			a, b := net.Pipe()
			_ = b.Close()
			return pipeEnd{a}, nil
		},
	}, {
		name: "the settle deadline is refused",
		dial: func() (io.ReadWriteCloser, *deadlineConn) {
			a, b := net.Pipe()
			_ = b.Close()
			d := &deadlineConn{Conn: a, fail: 1}
			return d, d
		},
		want: "deadline refused",
	}, {
		name: "clearing the deadline on a silent wire is refused",
		dial: func() (io.ReadWriteCloser, *deadlineConn) {
			a, b := net.Pipe()
			t.Cleanup(func() { _ = b.Close() })
			d := &deadlineConn{Conn: a, fail: 2}
			return d, d
		},
		want: "deadline refused",
	}} {
		t.Run(tc.name, func(t *testing.T) {
			var d *deadlineConn
			if tc.dial != nil {
				saved := dialTCP
				t.Cleanup(func() { dialTCP = saved })
				dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
					var c io.ReadWriteCloser
					c, d = tc.dial()
					return c, nil
				}
			}
			c, err := openTCP(tc.addr, time.Second)
			if tc.want == "" {
				if err != nil {
					t.Fatalf("openTCP: %v", err)
				}
				_ = c.Close()
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("openTCP error %v, want one naming %q", err, tc.want)
			}
			if c != nil {
				t.Fatalf("openTCP returned a wire with its error")
			}
			if d != nil && !d.closed {
				t.Fatalf("openTCP left the refused connection open")
			}
		})
	}
}
