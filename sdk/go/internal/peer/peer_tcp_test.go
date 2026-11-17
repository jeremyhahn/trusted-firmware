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

// A tcp: port dials the board's wire endpoint and hands back the stream as
// it is, with no reset.
func TestOpenTCPDialsTheWireEndpoint(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	got := make(chan []byte, 1)
	go func() {
		c, err := l.Accept()
		if err != nil {
			got <- nil
			return
		}
		defer c.Close()
		b := make([]byte, 3)
		_, _ = io.ReadFull(c, b)
		got <- b
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer c.Close()
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
	defer l.Close()
	send := make(chan byte)
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		_, _ = c.Write([]byte{<-send})
		<-send
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer c.Close()
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
