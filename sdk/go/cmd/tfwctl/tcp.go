// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"fmt"
	"net"
	"strconv"
	"strings"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// tcpScheme marks a device address that is not a serial port. An Ethernet
// device dials its endpoint and the host listens, never the reverse
// (sdk/go/tcp.go, #55), so tcp://host:port is where tfwctl LISTENS: the
// endpoint in the device's network block.
const tcpScheme = "tcp://"

// tcpAddr reports whether port is a tcp:// address, and returns the
// host:port to listen on.
func tcpAddr(port string) (addr string, ok bool, err error) {
	addr, ok = strings.CutPrefix(port, tcpScheme)
	if !ok {
		return "", false, nil
	}
	_, p, err := net.SplitHostPort(addr)
	if err != nil {
		return "", true, fmt.Errorf("device address %q: want %shost:port: %w", port, tcpScheme, err)
	}
	// The device's network block refuses port 0, and a named port or an empty
	// one would have tfwctl listen on a port the block never names.
	if n, perr := strconv.ParseUint(p, 10, 16); perr != nil || n == 0 {
		return "", true, fmt.Errorf("device address %q: want %shost:port: port %q must be a number 1-65535", port, tcpScheme, p)
	}
	return addr, true, nil
}

// listenForDevice listens on addr for as long as it takes one device to
// connect, at most wait, and stops listening once it has.
func listenForDevice(addr string, wait time.Duration, opts ...tfw.Option) (*tfw.TCPDevice, error) {
	a, err := net.ResolveTCPAddr("tcp", addr)
	if err != nil {
		return nil, err
	}
	l, err := net.ListenTCP("tcp", a)
	if err != nil {
		return nil, err
	}
	defer func() { _ = l.Close() }()
	return acceptDevice(l, wait, opts...)
}

// acceptDevice waits up to wait for one device to dial l. Closing the device
// closes its connection; l stays the caller's to close.
func acceptDevice(l *net.TCPListener, wait time.Duration, opts ...tfw.Option) (*tfw.TCPDevice, error) {
	if err := l.SetDeadline(time.Now().Add(wait)); err != nil {
		return nil, err
	}
	d, err := tfw.NewListener(l, opts...).Accept()
	if err != nil {
		return nil, fmt.Errorf("no device connected to %s within %s: %w", l.Addr(), wait, err)
	}
	return d, nil
}
