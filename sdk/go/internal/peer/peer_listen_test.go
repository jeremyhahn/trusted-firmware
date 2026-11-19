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
	board := dialUntil(addr, stop)
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
