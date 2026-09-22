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
