// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"fmt"
	"io"
	"strings"
	"time"
)

// Commands. Mirrors include/tfw/protocol.h.
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
	CmdI2CConfig    Cmd = 0x55
	CmdSPIConfig    Cmd = 0x56
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

// Feature bits, mirroring TFW_FEATURE_BITMAP.
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
	FeatureProvision
	FeatureAliasCert
	// FeatureAuthSession is issue #4's AUTH_CHALLENGE/AUTH_RESPONSE session,
	// bit 14 -- element tier only. See AuthChallenge/AuthResponse.
	FeatureAuthSession
)

// AlgorithmSet is the set of signature algorithms a device's crypto provider
// can actually perform, as CAPS reports it.
//
// A TYPE OF ITS OWN, and not the algorithm identifiers that travel in an
// attestation frame. The two number the same algorithms differently on
// purpose: the identifiers are sparse so that TFW_ALG_HMAC_SHA256 sits at 0x10
// with room beneath it for further asymmetric algorithms, which would make a
// bitmap indexed by identifier seventeen bits wide to express three
// algorithms. Since the numbers do not agree, a set is a type rather than a
// uint16: the named bits below are the only way to build one, so an identifier
// read out of an attestation frame cannot drift into a capability test without
// a conversion that says what it is doing.
type AlgorithmSet uint16

// Mirrors TFW_ALGBIT_* in include/tfw/protocol.h. Bits extend and never
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
	BoardID                                     BoardID
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

// PinFlags are one pin's capability bits. Mirrors TFW_PIN_* in
// include/tfw/board.h, where the numbering has gaps reserved for bits not yet
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
	// Port and Bit locate the pin in the board's port table, a whole byte each
	// on the wire (#12) so a part with 32-bit ports fits. HasPort is false
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
//
// ErrCRC and ErrFormat mirror tfw_framer_t's sticky counters (#5): a device
// older than this SDK does not report them, and both read zero rather than
// "no errors seen" -- there is no way to tell the two apart from the wire
// alone, and reporting zero rather than failing the whole reply is what lets
// a fleet running mixed firmware read this at all.
type SysInfo struct {
	Uptime time.Duration
	// FreeRAM is a u32 (#12). Firmware older than that sends only a u16, which
	// saturates at 0xFFFF; the exact value arrives in the appended tail, and
	// SysInfo prefers it when present.
	FreeRAM    uint32
	ResetCause ResetCause
	ErrCRC     uint16
	ErrFormat  uint16
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
	conn     io.ReadWriter
	seq      Seq
	timeout  time.Duration
	bootWait time.Duration
	rx       []byte
	// Events arriving while a response is awaited are collected rather than
	// discarded: a boot event means the device reset underneath us, which the
	// caller needs to know even though it did not ask. The one BOOT event New
	// itself waits for and consumes is deliberately NOT collected here -- it is
	// the greeting we asked for, not a reset we need to report.
	events []Frame
}

// Option configures a Device.
type Option func(*Device)

// WithTimeout sets how long to wait for a reply.
//
// The default is generous on purpose. On the element tier, ATTEST and
// ATTEST_CERT wait out the ATECC608A's datasheet maximum execution time for a
// signature, and ATTEST_CERT also hashes the whole certificate body in
// software on a 16 MHz part. A host that gave up after the usual serial
// timeout would declare a working device dead during provisioning.
func WithTimeout(d time.Duration) Option { return func(x *Device) { x.timeout = d } }

// DefaultBootWait is how long New waits for the firmware's BOOT event before
// giving up and proceeding anyway.
//
// Every classic Arduino auto-resets when a serial port raises DTR on open, and
// the ATmega16U2 pulses RESET into the bootloader's window right as this
// package's first frame would otherwise land in it. The firmware announces
// itself with an EVENT_BOOT the moment the application starts, so this is a
// real readiness signal rather than a guessed settle time -- but bounded,
// because an adapter that does not reset the board (a USB-serial cable with
// DTR wired off, tfwsim's pipes) will never send one, and that case must still
// work, paying only the bound.
//
// Sized for a bootloader that runs briefly and jumps -- the stock Arduino
// bootloader on the boards this project targets does. It is NOT sized for
// tfw-boot built with BOOT_MEASURE=atecc608a: that measures the application
// through the element at up to 36 ms per 64-byte block, the datasheet's own
// maximum for one SHA command (boot/measure/measure_atecc608a.c), which for
// anything but a small image outruns this default several times over. A host
// talking to such a build should pass a larger WithBootWait sized to the
// image it flashed, rather than rely on the default.
const DefaultBootWait = 2 * time.Second

// WithBootWait overrides how long New waits for the BOOT event. Zero or
// negative disables the wait entirely -- New returns as soon as the port is
// usable, exactly as it did before this existed, for a caller that knows its
// adapter never resets the board and would rather not pay any bound at all.
//
// The bound is enforced the same way Do's own WithTimeout already is: by the
// conn returning from Read within it, whether because it implements
// SetReadDeadline or because it carries its own floor the way
// serialport.Open's 200ms read timeout does. A conn with neither -- a bare
// io.Pipe, say -- can block New past the bound exactly as it could already
// block Do past its timeout. See awaitBoot for why that is not solved by
// racing the read on a second goroutine instead.
func WithBootWait(d time.Duration) Option { return func(x *Device) { x.bootWait = d } }

// New wraps a byte stream. Any io.ReadWriter will do: a serial port, a pty, or
// an tfwsim subprocess over a pipe -- which is how this package is tested
// without hardware.
//
// Unless disabled with WithBootWait, New waits up to the bound for the
// firmware's BOOT event before returning, so the first frame Do sends lands on
// a running application rather than into a bootloader that will not answer it.
func New(conn io.ReadWriter, opts ...Option) *Device {
	d := &Device{conn: conn, timeout: 10 * time.Second, bootWait: DefaultBootWait}
	for _, o := range opts {
		o(d)
	}
	d.awaitBoot(d.bootWait)
	return d
}

// awaitBoot blocks until the firmware's BOOT event arrives, bound elapses, or
// the link reports trouble of its own -- whichever comes first. It is best
// effort by design: a link error here is not this call's to report, since the
// very next Do against the same conn will hit the same failure and report it
// properly. The BOOT event this loop is looking for is consumed rather than
// recorded in d.events; any OTHER event seen while waiting (a device that was
// already up and immediately raises DEADMAN, say) is recorded exactly as Do
// would record it.
//
// This reads through fill, the same call Do's own read loop uses, and NOT
// through a second goroutine racing a timer against the conn. That was tried:
// a read abandoned when the timer wins still runs against d.conn after this
// function returns, and Do is then free to start its OWN read against the
// same conn the moment New hands the Device back -- two goroutines reading
// one stream at once, which can split the incoming bytes between them
// unpredictably. That is a worse failure than the one being fixed, and this
// package's contract is precisely that nothing here overlaps a read with
// another. So the bound here is enforced exactly as Do's own timeout is: the
// conn must return from Read within it, whether via SetReadDeadline (which
// fill applies when the conn implements it) or a floor the conn keeps for
// itself, the way serialport.Open's 200ms read timeout does. A conn with
// neither can block this call past bound exactly as it could already block Do
// past its timeout -- an existing, accepted property of this package's
// single-reader read loop, not a new one.
//
// flushStaleBacklog runs first: a BOOT event left behind by a session that
// closed before reading it is otherwise indistinguishable from a genuine
// one, and accepting it would repeat exactly the bug this whole wait exists
// to fix -- the reply Do sends next would land in the window of the reset
// THIS open just caused, not the one the stale event actually announced.
func (d *Device) awaitBoot(bound time.Duration) {
	if bound <= 0 {
		return
	}
	if !d.flushStaleBacklog() {
		// The purge itself failed, so nothing here can tell stale backlog
		// from a genuine event -- trusting either risks exactly the bug this
		// wait exists to prevent. Fall through the same way an adapter that
		// never sends a BOOT at all does, rather than gamble on what is left
		// in the buffer.
		return
	}
	deadline := time.Now().Add(bound)
	for {
		for {
			f, ok := d.decodeNext()
			if !ok {
				break
			}
			if f.IsEvent() && len(f.Payload) > 0 && f.Payload[0] == EventBoot {
				return
			}
			if f.IsEvent() {
				d.events = append(d.events, f)
			}
			// A non-event frame here cannot be a reply to anything this Device
			// has asked for -- nothing has been sent yet -- so it is dropped,
			// the same call Do makes about a reply that outlived its request.
		}
		if time.Now().After(deadline) {
			return
		}
		if err := d.fill(deadline); err != nil {
			return
		}
	}
}

// flushStaleBacklog discards whatever d.conn already has buffered from
// before this session, if the conn can even be asked to -- the same
// optional-interface pattern fill already uses for SetReadDeadline. A conn
// from serialport.Open always can: go.bug.st/serial.Port implements
// ResetInputBuffer (TCIFLUSH), and serialport.Open calls it too, for
// exactly the same reason, so this is deliberately redundant for that
// conn rather than the only place it happens.
//
// This replaced a fixed time window that discarded anything read within it,
// tried first and reverted: there is no window size that is both long
// enough to reliably catch backlog (which can take a few milliseconds to
// become readable, not only the ~0.3ms measured directly at open) and short
// enough to never catch a GENUINE event, because how fast a genuine one can
// arrive is a property of the BOARD, not a constant this package can assume.
// The ATtiny85 has no bootloader and answers over a software UART at 19200
// baud -- its own BOOT event is fully on the wire within single-digit
// milliseconds of the reset that produced it, inside any window wide enough
// to reliably drain a real OS receive buffer's worth of backlog. A purge is
// a point in time, not a duration: it cannot discard a byte that has not
// arrived yet, so it carries none of that risk regardless of how fast the
// board answers.
//
// Neither purge is a proof, and both remaining gaps are the same one in
// opposite directions: a byte physically in flight -- still crossing the
// USB link, not yet delivered to the OS -- at the instant either purge
// runs is untouched by it, which could in principle let a stale byte
// through if the reopen followed the close closely enough, or clip a
// genuine one on a board fast enough to have already started answering.
// Closing this completely would need a true session boundary -- proof this
// exchange is with the device THIS open reset, not just an absence of
// buffered bytes -- which no io.ReadWriter or serial library used here
// exposes. What is here is the practical bound: two purges, run back to
// back with no deliberate delay between them, catch backlog that is
// sitting still (which is what every measurement above and thirty
// consecutive reset-then-read round trips on real hardware, zero
// failures, show actual backlog to be) without the fixed window's proven
// failure mode of discarding a real fast board's own event outright.
//
// This also assumes New runs reasonably promptly after the conn becomes
// valid for this session -- the same assumption serialport.Open's own purge
// makes about the gap between it returning and its caller's next call, just
// with a caller-controlled rather than microsecond-scale gap. A caller that
// obtains a conn and does its own reads or writes with it before handing it
// to New should not expect this purge to distinguish that traffic from
// genuine backlog; nothing available here can. Every caller in this
// repository (tfwctl, tfwprov) calls New immediately after
// serialport.Open, which is the assumption in practice, not only in theory.
//
// ok is false only when the conn offers ResetInputBuffer and it returned an
// error -- a conn offering none at all returns true, the same documented,
// best-effort limitation this package has always had for a plain
// io.ReadWriter. A failed purge is different: something that claimed it
// could clear the backlog did not, so awaitBoot treats that as reason to
// trust nothing it might read rather than gamble on it (see the caller).
func (d *Device) flushStaleBacklog() (ok bool) {
	f, canFlush := d.conn.(interface{ ResetInputBuffer() error })
	if !canFlush {
		return true
	}
	return f.ResetInputBuffer() == nil
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
			f, ok := d.decodeNext()
			if !ok {
				break
			}
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

// decodeNext pops one complete frame off the front of d.rx. ok is false when
// the buffer holds no complete frame yet, which asks the caller to read more --
// this is the one piece of Do's read loop that awaitBoot also needs, so it
// lives once rather than twice.
func (d *Device) decodeNext() (Frame, bool) {
	for {
		f, n, err := Decode(d.rx)
		if errors.Is(err, ErrShortFrame) {
			return Frame{}, false
		}
		if err != nil {
			// Resynchronise on the next SOF rather than discarding the whole
			// buffer: a genuine frame may begin inside it.
			if i := indexAfter(d.rx, SOF, 1); i > 0 {
				d.rx = d.rx[i:]
				continue
			}
			d.rx = nil
			return Frame{}, false
		}
		d.rx = d.rx[n:]
		return f, true
	}
}

func indexAfter(b []byte, c byte, from int) int {
	for i := from; i < len(b); i++ {
		if b[i] == c {
			return i
		}
	}
	return -1
}
