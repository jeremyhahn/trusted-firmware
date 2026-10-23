// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"
)

// Mutation tests for tcp.go: each test kills a fault the other tests let
// through. Faults applied (killed/total in the commit): drop Listen's error
// check, drop Accept's error check, return a device beside Accept's error,
// drop the listener's options in Accept, put WithBootWait(0) before them, drop
// WithBootWait(0), make Listener.Close a no-op, RemoteAddr returning the local
// end, TCPDevice.Close swallowing its error, Addr returning a zero address.

// dialSilent connects to addr and holds the connection open without ever
// answering, until the test ends.
func dialSilent(t *testing.T, addr net.Addr) {
	t.Helper()
	c, err := net.Dial("tcp", addr.String())
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	t.Cleanup(func() { _ = c.Close() }) // the far end only goes away
}

// Accept must hand the listener's own options to every device it makes:
// a short WithTimeout on the listener is what times out a silent device.
func TestMutateAcceptAppliesTheListenersOptions(t *testing.T) {
	l := listen(t, WithTimeout(20*time.Millisecond))
	dialSilent(t, l.Addr())
	d, err := l.Accept()
	if err != nil {
		t.Fatalf("Accept: %v", err)
	}
	defer func() { _ = d.Close() }() // the backstop; this test does not check Close

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	_, err = d.Do(ctx, CmdPing, nil)
	if !errors.Is(err, ErrTimeout) {
		t.Fatalf("Do on a silent device = %v, want ErrTimeout from the listener's WithTimeout", err)
	}
	if ctx.Err() != nil {
		t.Fatal("the caller's context expired first: the listener's WithTimeout was not applied")
	}
}

// A second Close of an accepted device reports the closed connection, so
// Close returns the connection's own error rather than swallowing it.
func TestMutateTCPDeviceCloseReportsTheConnectionsError(t *testing.T) {
	l := listen(t, WithTimeout(time.Second))
	dialSilent(t, l.Addr())
	d, err := l.Accept()
	if err != nil {
		t.Fatalf("Accept: %v", err)
	}
	if err := d.Close(); err != nil {
		t.Fatalf("first Close: %v", err)
	}
	if err := d.Close(); !errors.Is(err, net.ErrClosed) {
		t.Fatalf("second Close = %v, want net.ErrClosed", err)
	}
}

// Listen on an address that cannot be bound fails and returns no listener.
func TestMutateListenRejectsABadAddress(t *testing.T) {
	l, err := Listen("127.0.0.1:not-a-port")
	if err == nil {
		t.Fatal("Listen on a bad address succeeded")
	}
	if l != nil {
		t.Fatalf("Listen returned a listener beside its error: %+v", l)
	}
}

// Accept on a closed listener returns no device beside its error.
func TestMutateAcceptReturnsNoDeviceOnError(t *testing.T) {
	l := listen(t)
	if err := l.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	d, err := l.Accept()
	if err == nil {
		t.Fatal("Accept on a closed listener succeeded")
	}
	if d != nil {
		t.Fatalf("Accept returned a device beside its error: %+v", d)
	}
}
