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
	"time"

	"go.bug.st/serial"
)

// DefaultBaud matches AG_UART_BAUD in the firmware's config.h.
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
// call) that a second purge run independently by aggeneric.Device (see
// awaitBoot, whose doc comment on flushStaleBacklog says what this gap is
// and is not -- it is not fully closable with what this package and
// go.bug.st/serial expose) closes essentially all of it -- and for a conn
// that did not come from here at all, Device's purge is the only one that
// runs.
func Open(path string, baud int) (io.ReadWriteCloser, error) {
	if baud <= 0 {
		baud = DefaultBaud
	}
	p, err := serial.Open(path, &serial.Mode{
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
	return p, nil
}

// List returns the serial ports the system can see.
func List() ([]string, error) {
	ports, err := serial.GetPortsList()
	if err != nil {
		return nil, fmt.Errorf("serialport: enumerating ports: %w", err)
	}
	return ports, nil
}
