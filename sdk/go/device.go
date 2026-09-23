// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"errors"
	"fmt"
	"io"
	"time"
)

// Commands. Mirrors include/ag/protocol.h.
const (
	CmdCaps         Cmd = 0x00
	CmdPing         Cmd = 0x01
	CmdReset        Cmd = 0x02
	CmdSysInfo      Cmd = 0x03
	CmdPinMode      Cmd = 0x10
	CmdDigitalRead  Cmd = 0x11
	CmdDigitalWrite Cmd = 0x12
	CmdTimerSet     Cmd = 0x14
	CmdTimerCancel  Cmd = 0x15
	CmdTimerQuery   Cmd = 0x16
	CmdAnalogRead   Cmd = 0x20
	CmdADCRef       Cmd = 0x21
	CmdEEPROMRead   Cmd = 0x40
	CmdEEPROMWrite  Cmd = 0x41
	CmdPWMWrite     Cmd = 0x22
	CmdTone         Cmd = 0x23
	CmdNoTone       Cmd = 0x24
	CmdPulseIn      Cmd = 0x30
	CmdPulseCapture Cmd = 0x34
	CmdI2CXfer      Cmd = 0x50
	CmdSPIXfer      Cmd = 0x51
	CmdOneWireReset Cmd = 0x52
	CmdOneWireWrite Cmd = 0x53
	CmdOneWireRead  Cmd = 0x54
	CmdAttest       Cmd = 0x60
)

// Events the device may send unsolicited.
const (
	EventBoot        = 0x01
	EventDeadman     = 0x02
	EventCounterWrap = 0x03
)

// CryptoTier says how strong the device's root of trust is. The host learns
// this from CAPS before trusting anything the device says.
type CryptoTier uint8

const (
	CryptoNone          CryptoTier = 0
	CryptoSoftware      CryptoTier = 1
	CryptoSecureElement CryptoTier = 2
	CryptoTPM2          CryptoTier = 3
)

func (c CryptoTier) String() string {
	switch c {
	case CryptoNone:
		return "none"
	case CryptoSoftware:
		return "software"
	case CryptoSecureElement:
		return "secure-element"
	case CryptoTPM2:
		return "tpm2-discrete"
	}
	return fmt.Sprintf("unrecognised tier %d", uint8(c))
}

// Feature bits, mirroring AG_FEATURE_BITMAP.
const (
	FeatureGPIO uint32 = 1 << iota
	FeatureSched
	FeatureAnalog
	FeaturePWM
	FeatureTone
	FeaturePulse
	FeatureEEPROM
	FeatureI2C
	FeatureSPI
	FeatureOneWire
	FeatureAttest
	FeatureDeadman
)

// Caps is what a device reports about itself.
type Caps struct {
	ProtoMajor, ProtoMinor                      uint8
	FirmwareMajor, FirmwareMinor, FirmwarePatch uint8
	BoardID                                     uint8
	PinCount                                    uint8
	SchedSlots                                  uint8
	MaxPayload                                  uint8
	CryptoTier                                  CryptoTier
	Features                                    uint32
}

// Has reports whether a feature was compiled into this build. The bitmap
// describes what the firmware HAS, not what the part could do -- two boards
// with the same MCU can ship different capability sets, so inferring from the
// chip is wrong on the second one.
func (c Caps) Has(feature uint32) bool { return c.Features&feature != 0 }

// SysInfo is the device's runtime state.
type SysInfo struct {
	Uptime     time.Duration
	FreeRAM    uint16
	ResetCause ResetCause
}

// ResetCause says why the part last restarted -- the difference between a
// commanded reboot and a device that is failing.
type ResetCause uint8

const (
	ResetUnknown  ResetCause = 0
	ResetPowerOn  ResetCause = 1
	ResetExternal ResetCause = 2
	ResetBrownout ResetCause = 3
	ResetWatchdog ResetCause = 4
)

func (r ResetCause) String() string {
	switch r {
	case ResetPowerOn:
		return "power-on"
	case ResetExternal:
		return "external"
	case ResetBrownout:
		return "brownout"
	case ResetWatchdog:
		return "watchdog"
	}
	return "unknown"
}

// ErrTimeout is returned when a device does not answer within the deadline.
var ErrTimeout = errors.New("aggeneric: device did not respond")

// Device is one attached board.
//
// Strict request/response, because the firmware is: it holds one response
// buffer and answers one command at a time. Nothing here pipelines, and the
// sequence number exists to catch a stale reply rather than to overlap
// requests.
type Device struct {
	conn    io.ReadWriter
	seq     Seq
	timeout time.Duration
	rx      []byte
	// Events arriving while a response is awaited are collected rather than
	// discarded: a boot event means the device reset underneath us, which the
	// caller needs to know even though it did not ask.
	events []Frame
}

// Option configures a Device.
type Option func(*Device)

// WithTimeout sets how long to wait for a reply.
//
// The default is generous on purpose. A device deriving an Alias key in
// software spends SECONDS inside one command on a 16 MHz part, and a host that
// gave up after the usual serial timeout would declare a working device dead
// during provisioning.
func WithTimeout(d time.Duration) Option { return func(x *Device) { x.timeout = d } }

// New wraps a byte stream. Any io.ReadWriter will do: a serial port, a pty, or
// an agsim subprocess over a pipe -- which is how this package is tested
// without hardware.
func New(conn io.ReadWriter, opts ...Option) *Device {
	d := &Device{conn: conn, timeout: 10 * time.Second}
	for _, o := range opts {
		o(d)
	}
	return d
}

// Events returns and clears any unsolicited events seen so far.
func (d *Device) Events() []Frame {
	e := d.events
	d.events = nil
	return e
}

func (d *Device) nextSeq() Seq {
	d.seq++
	if d.seq == SeqNone { // 0 means "uncorrelated"; never allocate it
		d.seq = 1
	}
	return d.seq
}

// Do sends one command and returns its response frame.
func (d *Device) Do(ctx context.Context, cmd Cmd, payload []byte) (Frame, error) {
	seq := d.nextSeq()
	req, err := EncodeRequest(seq, cmd, payload)
	if err != nil {
		return Frame{}, err
	}
	if _, err := d.conn.Write(req); err != nil {
		return Frame{}, fmt.Errorf("aggeneric: write: %w", err)
	}

	deadline := time.Now().Add(d.timeout)
	for {
		if err := ctx.Err(); err != nil {
			return Frame{}, err
		}
		// Anything already buffered may hold the reply.
		for {
			f, n, err := Decode(d.rx)
			if errors.Is(err, ErrShortFrame) {
				break
			}
			if err != nil {
				// Resynchronise on the next SOF rather than discarding the
				// whole buffer: a genuine frame may begin inside it.
				if i := indexAfter(d.rx, SOF, 1); i > 0 {
					d.rx = d.rx[i:]
					continue
				}
				d.rx = nil
				break
			}
			d.rx = d.rx[n:]
			switch {
			case f.IsEvent():
				d.events = append(d.events, f)
			case f.Seq != seq:
				// A reply to a request that already timed out. Dropping it is
				// the only safe move: returning it would answer this caller
				// with someone else's result.
				continue
			default:
				return f, f.Status.AsError()
			}
		}
		if time.Now().After(deadline) {
			return Frame{}, fmt.Errorf("%w after %s (command 0x%02x)", ErrTimeout, d.timeout, byte(cmd))
		}
		if err := d.fill(deadline); err != nil {
			return Frame{}, err
		}
	}
}

func (d *Device) fill(deadline time.Time) error {
	if c, ok := d.conn.(interface{ SetReadDeadline(time.Time) error }); ok {
		_ = c.SetReadDeadline(deadline)
	}
	buf := make([]byte, 256)
	n, err := d.conn.Read(buf)
	if n > 0 {
		d.rx = append(d.rx, buf[:n]...)
	}
	if err != nil && n == 0 {
		if errors.Is(err, io.EOF) {
			return fmt.Errorf("aggeneric: link closed: %w", err)
		}
		// A read timeout is not fatal; the caller's own deadline governs.
		return nil
	}
	return nil
}

func indexAfter(b []byte, c byte, from int) int {
	for i := from; i < len(b); i++ {
		if b[i] == c {
			return i
		}
	}
	return -1
}
