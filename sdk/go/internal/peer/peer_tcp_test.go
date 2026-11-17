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

// TestOpenTCPRedialsWhileTheFirmwareHoldsThePreviousHost reconnects while the
// previous socket's EOF is still unprocessed: the ESP32 wire (hal_uart.c
// accept_peer) closes the new host as a second one. Open dials again within
// its bound and returns the connection the firmware kept.
func TestOpenTCPRedialsWhileTheFirmwareHoldsThePreviousHost(t *testing.T) {
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
		_ = first.Close() // refused: the previous host moved a byte recently
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
	c, err := Open(TCPPrefix+l.Addr().String(), 2*time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
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
	defer func() { _ = l.Close() }()
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer func() { _ = c.Close() }()
		_, _ = c.Write([]byte{7, 8})
		_, _ = io.Copy(io.Discard, c)
	}()
	c, err := Open(TCPPrefix+l.Addr().String(), 2*time.Second)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer func() { _ = c.Close() }()
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
	defer func() { _ = l.Close() }()
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

// Connecting to a TCP wire does not reset the board, so no BOOT follows; the
// simulator and a serial board are reset by Start.
func TestResetsIsFalseOnlyForATCPWire(t *testing.T) {
	for port, want := range map[string]bool{
		"":                       true,
		"serial-port":            true,
		TCPPrefix + "board:3333": false,
	} {
		if got := (Target{Port: port, Board: "b"}).Resets(); got != want {
			t.Errorf("Target{Port: %q}.Resets() = %v, want %v", port, got, want)
		}
	}
}
