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

// dialEcho is a device in miniature: it dials the listener, as an Ethernet
// device does, and answers every request by echoing its payload. It sends no
// BOOT event, as the device does not on Ethernet. The returned channel yields
// the device side's local address, then closes when the host hangs up.
func dialEcho(t *testing.T, addr net.Addr) <-chan net.Addr {
	t.Helper()
	out := make(chan net.Addr, 1)
	go func() {
		defer close(out)
		c, err := net.Dial("tcp", addr.String())
		if err != nil {
			t.Errorf("dial: %v", err)
			return
		}
		defer c.Close()
		out <- c.LocalAddr()
		var buf []byte
		chunk := make([]byte, 256)
		for {
			n, err := c.Read(chunk)
			if err != nil {
				return
			}
			buf = append(buf, chunk[:n]...)
			for {
				f, used, err := Decode(buf)
				if errors.Is(err, ErrShortFrame) {
					break
				}
				if err != nil {
					t.Errorf("device decode: %v", err)
					return
				}
				buf = buf[used:]
				resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, f.Payload)
				if err != nil {
					t.Errorf("device encode: %v", err)
					return
				}
				if _, err := c.Write(resp); err != nil {
					return
				}
			}
		}
	}()
	return out
}

func listen(t *testing.T, opts ...Option) *Listener {
	t.Helper()
	l, err := Listen("127.0.0.1:0", opts...)
	if err != nil {
		t.Fatalf("Listen: %v", err)
	}
	t.Cleanup(func() { l.Close() })
	return l
}

// Accept hands back a working Device at once. Given a boot wait, it still
// does not wait for a BOOT event the device never sends on Ethernet.
func TestAcceptReturnsAWorkingDeviceWithoutWaitingForBoot(t *testing.T) {
	l := listen(t, WithBootWait(time.Minute), WithTimeout(5*time.Second))
	dev := dialEcho(t, l.Addr())

	before := time.Now()
	d, err := l.Accept()
	if err != nil {
		t.Fatalf("Accept: %v", err)
	}
	defer d.Close()
	if waited := time.Since(before); waited >= DefaultBootWait {
		t.Fatalf("Accept took %s; it must not wait for BOOT", waited)
	}

	got, err := d.Do(context.Background(), CmdPing, []byte("over tcp"))
	if err != nil {
		t.Fatalf("Do: %v", err)
	}
	if string(got.Payload) != "over tcp" {
		t.Fatalf("payload = %q, want %q", got.Payload, "over tcp")
	}
	if local := <-dev; d.RemoteAddr().String() != local.String() {
		t.Fatalf("RemoteAddr = %s, want the device's %s", d.RemoteAddr(), local)
	}
}

// Close drops the connection, which is the device's cue to dial again.
func TestTCPDeviceCloseDropsTheConnection(t *testing.T) {
	l := listen(t, WithTimeout(time.Second))
	dev := dialEcho(t, l.Addr())
	d, err := l.Accept()
	if err != nil {
		t.Fatalf("Accept: %v", err)
	}
	<-dev

	if err := d.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	select {
	case <-dev:
	case <-time.After(5 * time.Second):
		t.Fatal("the device never saw the connection close")
	}
	if _, err := d.Do(context.Background(), CmdPing, nil); err == nil {
		t.Fatal("Do succeeded on a closed connection")
	}
}

// A closed Listener refuses to accept, and NewListener's Close is the
// wrapped listener's.
func TestClosedListenerStopsAccepting(t *testing.T) {
	raw, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("net.Listen: %v", err)
	}
	l := NewListener(raw)
	if l.Addr().String() != raw.Addr().String() {
		t.Fatalf("Addr = %s, want %s", l.Addr(), raw.Addr())
	}
	if err := l.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	if _, err := l.Accept(); !errors.Is(err, net.ErrClosed) {
		t.Fatalf("Accept after Close = %v, want net.ErrClosed", err)
	}
	if _, err := raw.Accept(); !errors.Is(err, net.ErrClosed) {
		t.Fatalf("the wrapped listener is still open: %v", err)
	}
}
