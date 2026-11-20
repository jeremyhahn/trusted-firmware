// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"io"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// dialH5Wire plays the STM32H573I-DK's ETH_MAC=1 wire (hal_eth_wire_tcp.c)
// in front of board: the board dials the host at addr and listens on
// nothing, redialling while refused and after each connection ends; what it
// sends with no connection open is dropped.
func dialH5Wire(t *testing.T, addr string, board io.ReadWriter) {
	t.Helper()
	var mu sync.Mutex
	var cur net.Conn
	stop := make(chan struct{})
	t.Cleanup(func() {
		close(stop)
		mu.Lock()
		if cur != nil {
			_ = cur.Close()
		}
		mu.Unlock()
	})
	go func() {
		buf := make([]byte, 512)
		for {
			n, err := board.Read(buf)
			if err != nil {
				return
			}
			mu.Lock()
			c := cur
			mu.Unlock()
			if c != nil {
				_, _ = c.Write(buf[:n])
			}
		}
	}()
	go func() {
		for {
			if c, err := net.DialTimeout("tcp", addr, time.Second); err == nil {
				mu.Lock()
				cur = c
				mu.Unlock()
				_, _ = io.Copy(board, c)
				mu.Lock()
				cur = nil
				mu.Unlock()
				_ = c.Close()
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
}

// make hw-test BOARD=stm32h573i-dk PORT=tcp-listen:<host-port>: the host
// listens and the board dials it (#584, Codex round 5). A row the DK's TCP
// run selects passes over the device's own connection, twice, so a row after
// another finds the board's redial. peer.Open never dials: the fake board
// listens on nothing.
func TestTheDKRowsRunOverTheDevicesOutboundConnection(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	addr := l.Addr().String()
	require.NoError(t, l.Close())

	dialH5Wire(t, addr, simproc.Start(t, simproc.Software))
	t.Setenv(peer.PortEnv, peer.TCPListenPrefix+addr)
	t.Setenv(peer.BoardEnv, "stm32h573i-dk")

	img := runImage(boardImages["stm32h573i-dk"], peer.TCPListenPrefix+addr)
	require.Equal(t, partTCP|partEthAllowList, img.parts&(partTCP|partEthAllowList),
		"a tcp-listen: run of the DK is not a TCP run")
	// An allow-list row that asks nothing of the board's name: the host
	// simulator behind the fake wire is the 328P image.
	const row = "TestFirmwareAppIsAnUnknownCommandWithoutAnApplication"
	require.True(t, t.Run(row, TestFirmwareAppIsAnUnknownCommandWithoutAnApplication), "the row failed over the device's connection")
	require.True(t, t.Run(row, TestFirmwareAppIsAnUnknownCommandWithoutAnApplication), "the row failed after a row before it")
}
