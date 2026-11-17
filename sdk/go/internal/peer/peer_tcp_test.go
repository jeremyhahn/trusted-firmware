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
