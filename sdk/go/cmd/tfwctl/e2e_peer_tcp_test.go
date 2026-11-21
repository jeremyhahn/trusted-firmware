// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"io"
	"net"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// A board whose wire is TCP is not reset by connecting, so no BOOT comes.
// A row that wraps that wire (meddler) must still open at once and fail a
// request within its deadline, not block until go test's alarm (PR #555).
func TestOpenPeerOverASilentTCPWireFailsWithinItsDeadline(t *testing.T) {
	l, err := net.Listen("tcp", ":0")
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })
	accepted := make(chan net.Conn, 1)
	go func() {
		if c, err := l.Accept(); err == nil {
			accepted <- c
		}
	}()
	t.Cleanup(func() {
		select {
		case c := <-accepted:
			_ = c.Close()
		default:
		}
	})
	t.Setenv(peer.PortEnv, peer.TCPPrefix+l.Addr().String())
	t.Setenv(peer.BoardEnv, "esp32p4-poe")

	begin := time.Now()
	p := openPeer(t, func(c io.ReadWriter) io.ReadWriter { return &meddler{conn: c} }, simproc.Software)
	require.Less(t, time.Since(begin), tfw.DefaultBootWait, "opening waited for a BOOT the connection cannot cause")

	done := make(chan error, 1)
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
		defer cancel()
		_, err := p.serial.Ping(ctx, []byte("x"))
		done <- err
	}()
	select {
	case err := <-done:
		require.Error(t, err)
	case <-time.After(15 * time.Second):
		t.Fatal("a request over the wrapped TCP wire blocked past its deadline")
	}
}
