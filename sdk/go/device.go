// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"errors"
	"fmt"
	"io"
	"strings"
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

// AlgorithmSet is the set of signature algorithms a device's crypto provider
// can actually perform, as CAPS reports it.
//
// A TYPE OF ITS OWN, and not the algorithm identifiers that travel in an
// attestation frame. The two number the same algorithms differently on
// purpose: the identifiers are sparse so that AG_ALG_HMAC_SHA256 sits at 0x10
// with room beneath it for further asymmetric algorithms, which would make a
// bitmap indexed by identifier seventeen bits wide to express three
// algorithms. Since the numbers do not agree, a set is a type rather than a
// uint16: the named bits below are the only way to build one, so an identifier
// read out of an attestation frame cannot drift into a capability test without
// a conversion that says what it is doing.
type AlgorithmSet uint16

// Mirrors AG_ALGBIT_* in include/ag/protocol.h. Bits extend and never
// renumber, exactly as the status codes do, so a host built before an
// algorithm existed sees an unknown bit and negotiates down rather than
// misreading it as one it knows.
const (
	AlgECDSAP256SHA256 AlgorithmSet = 1 << iota
	AlgEd25519
	AlgHMACSHA256
)

var algorithmNames = []struct {
	bit  AlgorithmSet
	name string
}{
	{AlgECDSAP256SHA256, "ecdsa-p256-sha256"},
	{AlgEd25519, "ed25519"},
	{AlgHMACSHA256, "hmac-sha256"},
}

// Can reports whether the device can perform every algorithm in want. A caller
// choosing what to ask for tests one bit; a caller checking a policy that
// demands several tests them together, and either reads the same.
func (s AlgorithmSet) Can(want AlgorithmSet) bool { return s&want == want }

func (s AlgorithmSet) String() string {
	if s == 0 {
		return "(none)"
	}
	var (
		named []string
		known AlgorithmSet
	)
	for _, a := range algorithmNames {
		known |= a.bit
		if s&a.bit != 0 {
			named = append(named, a.name)
		}
	}
	// Bits this build has no name for are printed as a number rather than
	// dropped. A device offering something newer than the host is the ordinary
	// state of a fleet mid-upgrade, and silently showing fewer algorithms than
	// were reported would hide it.
	if rest := s &^ known; rest != 0 {
		named = append(named, fmt.Sprintf("0x%04x", uint16(rest)))
	}
	return strings.Join(named, " ")
}

// Caps is what a device reports about itself.
//
// Fields are APPENDED to the reply and never reordered, so a host built
// against an older header stops reading where it always did instead of
// misparsing the tail. Algorithms arrived that way, which is why parsing it is
// conditional on the reply being long enough to hold it.
type Caps struct {
	ProtoMajor, ProtoMinor                      uint8
	FirmwareMajor, FirmwareMinor, FirmwarePatch uint8
	BoardID                                     uint8
	PinCount                                    uint8
	SchedSlots                                  uint8
	MaxPayload                                  uint8
	CryptoTier                                  CryptoTier
	Features                                    uint32
	Algorithms                                  AlgorithmSet
}

// Has reports whether a feature was compiled into this build. The bitmap
// describes what the firmware HAS, not what the part could do -- two boards
// with the same MCU can ship different capability sets, so inferring from the
// chip is wrong on the second one.
func (c Caps) Has(feature uint32) bool { return c.Features&feature != 0 }

// PinFlags are one pin's capability bits. Mirrors AG_PIN_* in
// include/ag/board.h, where the numbering has gaps reserved for bits not yet
// defined -- hence literals here rather than iota, which would silently close
// them.
type PinFlags uint8

const (
	// PinDigital says the pin has a port and can be read and driven.
	PinDigital PinFlags = 0x01
	// PinPWM says the board table gives the pin an output-compare unit.
	// Whether that unit is USABLE is the PWM field of PinInfo, not this bit.
	PinPWM PinFlags = 0x02
	// PinADC says the pin has an analog channel behind it.
	PinADC PinFlags = 0x04
	// PinReserved marks /RESET, or a pin claimed by a software peripheral.
	PinReserved PinFlags = 0x20
	// PinInUse marks a pin this build has taken -- the UART pins, say. Driving
	// one cuts the line the command arrived on, so the device refuses.
	PinInUse PinFlags = 0x40
	// PinLED marks the board's onboard LED.
	PinLED PinFlags = 0x80
)

// Has reports whether every flag in want is set.
func (f PinFlags) Has(want PinFlags) bool { return f&want == want }

var pinFlagNames = []struct {
	bit  PinFlags
	name string
}{
	{PinDigital, "digital"},
	{PinPWM, "pwm"},
	{PinADC, "adc"},
	{PinReserved, "reserved"},
	{PinInUse, "in-use"},
	{PinLED, "led"},
}

func (f PinFlags) String() string {
	if f == 0 {
		return "(none)"
	}
	var (
		named []string
		known PinFlags
	)
	for _, n := range pinFlagNames {
		known |= n.bit
		if f&n.bit != 0 {
			named = append(named, n.name)
		}
	}
	if rest := f &^ known; rest != 0 {
		named = append(named, fmt.Sprintf("0x%02x", uint8(rest)))
	}
	return strings.Join(named, " ")
}

// PWMSupport says what became of a pin's output-compare unit, and it has THREE
// states rather than two.
//
// "No compare unit here" and "this build spent its compare unit on the
// millisecond tick" both refuse a duty cycle, but only the second one can be
// changed, and it is changed by building the firmware differently rather than
// by moving the load to another pin. A host that collapsed them to a boolean
// would rewire a board to work around a build flag.
type PWMSupport uint8

const (
	// PWMNone: the pin has no output-compare unit at all.
	PWMNone PWMSupport = 0
	// PWMAvailable: the pin has one and this build will drive it.
	PWMAvailable PWMSupport = 1
	// PWMSystemTick: the pin has one, but its timer is the millisecond tick.
	PWMSystemTick PWMSupport = 2
)

func (p PWMSupport) String() string {
	switch p {
	case PWMNone:
		return "none"
	case PWMAvailable:
		return "available"
	case PWMSystemTick:
		return "spent on the millisecond tick"
	}
	return fmt.Sprintf("unrecognised pwm state %d", uint8(p))
}

// PinInfo is one pin's descriptor, as CAPS reports it a page at a time.
//
// Paged per pin because an ATmega2560's full table is 350 bytes and fits in no
// frame this protocol defines, let alone an ATtiny's.
type PinInfo struct {
	Pin   uint8
	Flags PinFlags
	// Port and Bit locate the pin in the board's port table. HasPort is false
	// for an ADC-only input, which has no digital port at all and can be read
	// but never written -- so Port and Bit are meaningless rather than zero,
	// and a caller must consult HasPort rather than compare them against zero.
	HasPort   bool
	Port, Bit uint8
	// HasADC and ADCChannel are the same arrangement for the analog side.
	HasADC     bool
	ADCChannel uint8
	PWM        PWMSupport
}

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
		return Frame{}, &LinkError{Op: "write", Err: err}
	}

	start := time.Now()
	deadline := start.Add(d.timeout)
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
			// The waiting actually done, not the deadline that was set: a read
			// that blocked past it is the interesting case, and the two
			// numbers differ exactly when something is wrong underneath.
			return Frame{}, &TimeoutError{Cmd: cmd, Elapsed: time.Since(start)}
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
			// The port went away. Wrapped rather than replaced, so a caller
			// still reaches io.EOF through errors.Is and can tell a closed
			// link -- reconnect -- from a stalled one.
			return &LinkError{Op: "read", Err: err}
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
