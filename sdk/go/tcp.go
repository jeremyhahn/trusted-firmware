// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import "net"

// Listener accepts the TCP connections Ethernet devices open -- issue #55.
//
// The device dials and the host listens, never the reverse: a device behind
// NAT or a firewall can still reach its endpoint, and a device that never
// listens has no open port to scan. Which device connected is not something
// the source address can say; ask it, with Attest and the evidence calls, and
// verify what it answers.
//
// Over Ethernet the device dispatches only CAPS, ATTEST, ATTEST_CERT and
// ATTEST_CSR. Every other command is answered with StatusErrUnknownCmd, which
// is the same status an older device gives for a command it was built
// without.
type Listener struct {
	l    net.Listener
	opts []Option
}

// Listen opens a TCP listener on addr ("host:port"; port 0 picks one) for
// devices to connect to. The options apply to every accepted Device.
func Listen(addr string, opts ...Option) (*Listener, error) {
	l, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	return NewListener(l, opts...), nil
}

// NewListener wraps a listener the caller already owns -- one with TLS in
// front of it, say. Closing the Listener closes it.
func NewListener(l net.Listener, opts ...Option) *Listener {
	return &Listener{l: l, opts: opts}
}

// Accept waits for the next device to connect.
//
// It does not wait for a BOOT event the way New does by default: the device
// sends no events over Ethernet, and a device that connects is already
// running. So WithBootWait given to Listen has no effect here.
func (l *Listener) Accept() (*TCPDevice, error) {
	c, err := l.l.Accept()
	if err != nil {
		return nil, err
	}
	opts := append(append([]Option(nil), l.opts...), WithBootWait(0))
	return &TCPDevice{Device: New(c, opts...), conn: c}, nil
}

// Addr is the address the listener is bound to: what goes in the device's
// network block as its endpoint.
func (l *Listener) Addr() net.Addr { return l.l.Addr() }

// Close stops accepting. Devices already accepted stay connected.
func (l *Listener) Close() error { return l.l.Close() }

// TCPDevice is a Device on an accepted connection.
type TCPDevice struct {
	*Device
	conn net.Conn
}

// RemoteAddr is where the connection came from. It identifies a route, not a
// device.
func (d *TCPDevice) RemoteAddr() net.Addr { return d.conn.RemoteAddr() }

// Close drops the connection. The device notices and dials again after its
// backoff.
func (d *TCPDevice) Close() error { return d.conn.Close() }
