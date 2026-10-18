// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package serialport opens a physical port as a byte stream for the SDK.
//
// Deliberately its own package. The root package is stdlib-only, so a consumer
// that only needs the codec -- go-xkms verifying evidence, say -- links no
// serial library at all. Only something that actually opens a device pays for
// one.
package serialport

import (
	"fmt"
	"io"
	"os"
	"sync"
	"time"

	"go.bug.st/serial"
)

// openPort and listPorts are the library's open and enumeration calls. They
// are variables only so a test can make each fail; nothing else assigns them.
var (
	openPort  = serial.Open
	listPorts = serial.GetPortsList
)

// errClosed is what every method of a port Open returned answers once that
// port is closed.
var errClosed = fmt.Errorf("serialport: port is closed: %w", os.ErrClosed)

// DefaultBaud matches TFW_UART_BAUD in the firmware's config.h.
const DefaultBaud = 115200

// Open opens a serial port in 8N1 at the given baud.
//
// The port's own receive backlog is purged once, immediately, before Open
// returns: a closed port keeps receiving, so the OS holds whatever the
// device sent while nothing was reading, and the next open would otherwise
// see it immediately -- on a stock ATmega2560 (STK500v2 bootloader), a BOOT
// event left over from a session that had already closed was fully readable
// 0.3ms after the next open, long before this open's own DTR reset could
// have produced a new one. Purging a SECOND time after a short wait was
// tried and reverted: how fast a GENUINE event can arrive is a property of
// the board, not a constant this package can assume, and the ATtiny85's own
// BOOT event -- no bootloader, answers over software UART at 19200 baud --
// is fully on the wire within single-digit milliseconds of its own reset,
// inside any wait long enough to be a meaningful second purge. A purge is a
// point in time, not a duration: it cannot discard a byte that has not
// arrived yet, so the single purge here carries none of that risk. What it
// does not close is a byte still in flight at the exact instant it runs, a
// gap so narrow (microseconds between Open returning and its caller's next
// call) that a second purge run independently by tfw.Device (see
// awaitBoot, whose doc comment on flushStaleBacklog says what this gap is
// and is not -- it is not fully closable with what this package and
// go.bug.st/serial expose) closes essentially all of it -- and for a conn
// that did not come from here at all, Device's purge is the only one that
// runs.
//
// The port returned is a go.bug.st/serial Port that, once closed, answers every
// call with an error wrapping os.ErrClosed rather than touching the descriptor.
func Open(path string, baud int) (io.ReadWriteCloser, error) {
	if baud <= 0 {
		baud = DefaultBaud
	}
	p, err := openPort(path, &serial.Mode{
		BaudRate: baud,
		DataBits: 8,
		Parity:   serial.NoParity,
		StopBits: serial.OneStopBit,
	})
	if err != nil {
		return nil, fmt.Errorf("serialport: opening %s: %w", path, err)
	}
	// A read that blocks forever turns a missing device into a hung process.
	// The SDK applies its own deadline on top; this is the floor that makes
	// the read return at all.
	if err := p.SetReadTimeout(200 * time.Millisecond); err != nil {
		_ = p.Close()
		return nil, fmt.Errorf("serialport: setting read timeout on %s: %w", path, err)
	}
	if err := p.ResetInputBuffer(); err != nil {
		_ = p.Close()
		return nil, fmt.Errorf("serialport: purging %s's receive buffer: %w", path, err)
	}
	return &port{p: p}, nil
}

// List returns the serial ports the system can see.
func List() ([]string, error) {
	ports, err := listPorts()
	if err != nil {
		return nil, fmt.Errorf("serialport: enumerating ports: %w", err)
	}
	return ports, nil
}

// port is the serial.Port Open hands out. The library's own Write, and its
// ioctls, act on the raw descriptor even after Close, so once the number is
// reused they reach whatever file now holds it. port refuses every call after
// Close instead, and closes the library's port once.
//
// Every method but Read holds mu for reading across its call, and Close takes
// it for writing, so no call is in flight on the descriptor when it closes. A
// Write the line cannot drain therefore holds Close until it can. Read only
// checks: a Read blocked on a silent line must not hold Close up, and the
// library's own Read is woken by, and waited for by, its Close.
type port struct {
	p      serial.Port
	mu     sync.RWMutex
	closed bool
}

var _ serial.Port = (*port)(nil)

// do runs f on the open port, or refuses once the port is closed.
func (c *port) do(f func() error) error {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.closed {
		return errClosed
	}
	return f()
}

func (c *port) isClosed() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.closed
}

func (c *port) Read(b []byte) (int, error) {
	if c.isClosed() {
		return 0, errClosed
	}
	n, err := c.p.Read(b)
	if err != nil && c.isClosed() {
		return n, errClosed
	}
	return n, err
}

func (c *port) Write(b []byte) (n int, err error) {
	err = c.do(func() (err error) {
		n, err = c.p.Write(b)
		return err
	})
	return n, err
}

// Close closes the library's port the first time and returns nil after.
func (c *port) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed {
		return nil
	}
	c.closed = true
	return c.p.Close()
}

func (c *port) SetMode(mode *serial.Mode) error {
	return c.do(func() error { return c.p.SetMode(mode) })
}

func (c *port) Drain() error { return c.do(c.p.Drain) }

func (c *port) ResetInputBuffer() error { return c.do(c.p.ResetInputBuffer) }

func (c *port) ResetOutputBuffer() error { return c.do(c.p.ResetOutputBuffer) }

func (c *port) SetDTR(dtr bool) error {
	return c.do(func() error { return c.p.SetDTR(dtr) })
}

func (c *port) SetRTS(rts bool) error {
	return c.do(func() error { return c.p.SetRTS(rts) })
}

func (c *port) GetModemStatusBits() (bits *serial.ModemStatusBits, err error) {
	err = c.do(func() (err error) {
		bits, err = c.p.GetModemStatusBits()
		return err
	})
	return bits, err
}

func (c *port) SetReadTimeout(t time.Duration) error {
	return c.do(func() error { return c.p.SetReadTimeout(t) })
}

func (c *port) Break(d time.Duration) error {
	return c.do(func() error { return c.p.Break(d) })
}
