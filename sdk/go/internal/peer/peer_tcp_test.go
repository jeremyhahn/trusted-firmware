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

// TestOpenTCPRedialsWhileTheFirmwareHoldsThePreviousHost reconnects while the
// previous socket's EOF is still unprocessed: the ESP32 wire (hal_uart.c
// accept_peer) closes the new host as a second one. Open dials again within
// its bound and returns the connection the firmware kept.
func TestOpenTCPRedialsWhileTheFirmwareHoldsThePreviousHost(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	got := make(chan []byte, 1)
	go func() {
		first, err := l.Accept()
		if err != nil {
			got <- nil
			return
		}
		_ = first.Close() // refused: the previous host moved a byte recently
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
	c, err := Open(TCPPrefix+l.Addr().String(), 2*time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer c.Close()
	if _, err := c.Write([]byte{1, 2, 3}); err != nil {
		t.Fatalf("Write: %v", err)
	}
	select {
	case b := <-got:
		if string(b) != "\x01\x02\x03" {
			t.Fatalf("the kept connection read %x, want 010203", b)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Open returned the connection the firmware closed: it never dialled again")
	}
}

// TestOpenTCPKeepsBytesTheBoardSentWhileSettling: a byte the board sends in
// the settle window is the first one the rows read.
func TestOpenTCPKeepsBytesTheBoardSentWhileSettling(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		_, _ = c.Write([]byte{7, 8})
		_, _ = io.Copy(io.Discard, c)
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), 2*time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer c.Close()
	b := make([]byte, 2)
	if _, err := io.ReadFull(c, b); err != nil || string(b) != "\x07\x08" {
		t.Fatalf("read %x, %v; want 0708", b, err)
	}
}

// TestOpenTCPGivesUpWhenEveryConnectionIsClosed: a wire that refuses every
// host fails Open within the bound, naming the endpoint.
func TestOpenTCPGivesUpWhenEveryConnectionIsClosed(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			_ = c.Close()
		}
	}()
	port := TCPPrefix + l.Addr().String()
	start := time.Now()
	_, err = Open(port, time.Second)
	if err == nil || !strings.Contains(err.Error(), port) {
		t.Fatalf("Open = %v, want an error naming %s", err, port)
	}
	if took := time.Since(start); took > 2*time.Second {
		t.Fatalf("Open took %v past its 1s bound", took)
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
