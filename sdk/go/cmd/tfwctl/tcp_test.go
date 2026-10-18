// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"net"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestTCPAddrParsesTheDeviceAddress(t *testing.T) {
	for _, tc := range []struct {
		port   string
		addr   string
		isTCP  bool
		errMsg string
	}{
		{"/dev/ttyACM0", "", false, ""},
		{"COM3", "", false, ""},
		{"tcp://127.0.0.1:9000", "127.0.0.1:9000", true, ""},
		{"tcp://:9000", ":9000", true, ""},
		{"tcp://[::1]:9000", "[::1]:9000", true, ""},
		{"tcp://127.0.0.1", "", true, "want tcp://host:port"},
		{"tcp://", "", true, "want tcp://host:port"},
		{"TCP://127.0.0.1:9000", "", false, ""}, // a serial port name, not the scheme
	} {
		t.Run(tc.port, func(t *testing.T) {
			addr, isTCP, err := tcpAddr(tc.port)
			assert.Equal(t, tc.isTCP, isTCP)
			assert.Equal(t, tc.addr, addr)
			if tc.errMsg == "" {
				assert.NoError(t, err)
			} else {
				require.Error(t, err)
				assert.Contains(t, err.Error(), tc.errMsg)
			}
		})
	}
}

func TestAcceptDeviceTakesTheFirstConnection(t *testing.T) {
	l, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })

	c, err := net.Dial("tcp", l.Addr().String())
	require.NoError(t, err)
	t.Cleanup(func() { _ = c.Close() })

	d, err := acceptDevice(l, time.Minute)
	require.NoError(t, err)
	t.Cleanup(func() { _ = d.Close() })
	assert.Equal(t, c.LocalAddr().String(), d.RemoteAddr().String())
}

// A device that never dials is a bounded failure naming the address, not a
// hang. The deadline is already past, so nothing waits.
func TestAcceptDeviceGivesUpWhenNoDeviceConnects(t *testing.T) {
	l, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })

	_, err = acceptDevice(l, -time.Second)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "no device connected to "+l.Addr().String())
}

func TestListenForDeviceRefusesAnAddressItCannotBind(t *testing.T) {
	_, err := listenForDevice("no-such-host.invalid:1", time.Second)
	assert.Error(t, err)

	l, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })
	_, err = listenForDevice(l.Addr().String(), time.Second)
	assert.Error(t, err, "an address already bound")
}

func TestListenForDeviceAcceptsADevice(t *testing.T) {
	// Port 0 cannot be dialled, so bind one, free it and listen there: the
	// port is ours unless another process takes it in between.
	probe, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	require.NoError(t, err)
	addr := probe.Addr().String()
	require.NoError(t, probe.Close())

	type result struct {
		err error
	}
	done := make(chan result, 1)
	go func() {
		d, err := listenForDevice(addr, time.Minute)
		if err == nil {
			_ = d.Close()
		}
		done <- result{err}
	}()
	// The device's side: dial until the listener is up, as a device's
	// backoff does, for as long as the listener's own bound and no longer
	// than it is still listening.
	var c net.Conn
	for deadline := time.Now().Add(time.Minute); c == nil; c, _ = net.Dial("tcp", addr) {
		select {
		case r := <-done:
			t.Fatalf("listenForDevice returned before a device dialled: %v", r.err)
		default:
		}
		require.True(t, time.Now().Before(deadline), "the listener never came up")
	}
	t.Cleanup(func() { _ = c.Close() })
	assert.NoError(t, (<-done).err)
}
