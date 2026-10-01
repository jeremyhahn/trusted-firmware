// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"encoding/binary"
	"time"
)

// The fixed parts of the CAPS reply.
//
// capsHeaderLen is where an older firmware stops: everything through the
// feature bitmap. capsAlgorithmsEnd covers the algorithm bitmap appended after
// it. The two are separate constants rather than one because the difference
// between them is precisely what a host must tolerate missing.
const (
	capsHeaderLen     = 14
	capsAlgorithmsEnd = 16
	capsPinPageLen    = 5
)

// The other fixed reply layouts, named where the numbers would otherwise be
// repeated between the bounds check and the slicing that follows it.
const (
	sysInfoLen    = 7 // uptime:4, free RAM:2, reset cause:1
	timerQueryLen = 6 // active:1, held level:1, remaining:4
)

// sysInfoErrCountersEnd is where the framer's error counters end. #5 appended
// them after reset_cause rather than inserting them, so sysInfoLen is where a
// host built before they existed stops reading and this is where one built
// after they existed does -- the same appended-tail pattern capsAlgorithmsEnd
// uses for CAPS.
const sysInfoErrCountersEnd = sysInfoLen + 4 // err_crc:2, err_format:2

// timerMaxMillis is the longest timer the device will arm, mirroring
// AG_SCHED_MAX_MS.
//
// Half the range of the uint32 the wire carries, and the half is the point:
// the sweep decides whether a deadline has passed with a SIGNED difference, so
// that the millisecond counter wrapping every 49.7 days is a non-event. Past
// 2^31 ms that comparison can no longer tell "due" from "not yet", and the
// failure mode is a valve that stays open for seven weeks.
const timerMaxMillis = 0x7FFFFFFF

// Ping echoes a payload through the device. The one command that proves the
// whole path -- framing, dispatch and back -- without touching any hardware.
func (d *Device) Ping(ctx context.Context, msg []byte) ([]byte, error) {
	f, err := d.Do(ctx, CmdPing, msg)
	if err != nil {
		return nil, err
	}
	return f.Payload, nil
}

// Caps asks the device what it is.
//
// A host issues this once on connect and caches it. Everything else it does --
// which pins can do PWM, whether attestation is available, how large a payload
// it may send -- follows from the answer, so nothing downstream needs to
// hardcode a board.
func (d *Device) Caps(ctx context.Context) (*Caps, error) {
	f, err := d.Do(ctx, CmdCaps, nil)
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < capsHeaderLen {
		return nil, &ShortReplyError{Cmd: CmdCaps, Got: len(f.Payload), Need: capsHeaderLen}
	}
	p := f.Payload
	c := &Caps{
		ProtoMajor:    p[0],
		ProtoMinor:    p[1],
		FirmwareMajor: p[2],
		FirmwareMinor: p[3],
		FirmwarePatch: p[4],
		BoardID:       p[5],
		PinCount:      p[6],
		SchedSlots:    p[7],
		MaxPayload:    p[8],
		CryptoTier:    CryptoTier(p[9]),
		Features:      binary.LittleEndian.Uint32(p[10:capsHeaderLen]),
	}
	// The algorithm bitmap was appended after the feature bitmap, so firmware
	// older than it simply stops here. That is not an error and must not be
	// read as one: the reply is complete by the contract the device was built
	// against. The empty set is the honest answer -- it says the device claims
	// no algorithm, which is exactly what this host knew about it before the
	// field existed, and is the direction a negotiation may safely fail in.
	if len(p) >= capsAlgorithmsEnd {
		c.Algorithms = AlgorithmSet(binary.LittleEndian.Uint16(p[capsHeaderLen:capsAlgorithmsEnd]))
	}
	// A major mismatch means the frames themselves may not parse the same way.
	// Minor is tolerated in both directions: that is what lets a fleet run
	// mixed firmware without a flag day. The Caps is returned ALONGSIDE the
	// error, because what the device said about itself is the one thing worth
	// logging when a host refuses to talk to it.
	if c.ProtoMajor != ProtoVersionMajor {
		return c, &ProtocolVersionError{
			DeviceMajor: c.ProtoMajor, DeviceMinor: c.ProtoMinor,
			HostMajor: ProtoVersionMajor, HostMinor: ProtoVersionMinor,
		}
	}
	return c, nil
}

// "There is nothing here", twice, and they are not the same shape.
//
// The port byte is the board descriptor's own packing: a port index in the high
// nibble and a bit in the low. A pin with no digital port carries AG_PORT_NONE
// in the high nibble, so the test is on the NIBBLE, not on the whole byte --
// A6 on an ATmega328P arrives as 0xF0, which a whole-byte comparison against
// 0xFF reads as port 15, bit 0: a port no board has, reported as one it does.
// A pin beyond the board is 0xFF, which the same nibble test also catches.
const (
	pinPortNone    = 0x0F // AG_PORT_NONE, in the high nibble
	pinADCChanNone = 0xFF // AG_ADC_NONE, a whole byte
)

// PinInfo asks the device about one pin.
//
// The same CAPS command, with a pin index as its argument: no argument returns
// the board header, one byte returns that pin's descriptor. This is what lets
// a host discover a board rather than hardcode one -- which pins have an ADC
// channel, which can carry a duty cycle, which the firmware has already
// claimed for itself.
func (d *Device) PinInfo(ctx context.Context, pin uint8) (*PinInfo, error) {
	f, err := d.Do(ctx, CmdCaps, []byte{pin})
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < capsPinPageLen {
		return nil, &ShortReplyError{Cmd: CmdCaps, Got: len(f.Payload), Need: capsPinPageLen}
	}
	p := f.Payload
	// The pin is echoed rather than assumed, so a descriptor stays identifiable
	// once it has been copied away from the call that asked for it.
	info := &PinInfo{Pin: p[0], Flags: PinFlags(p[1]), PWM: PWMSupport(p[4])}
	if p[2]>>4 != pinPortNone {
		info.HasPort = true
		info.Port, info.Bit = p[2]>>4, p[2]&0x0F
	}
	if p[3] != pinADCChanNone {
		info.HasADC = true
		info.ADCChannel = p[3]
	}
	return info, nil
}

// SysInfo reads the device's runtime state.
func (d *Device) SysInfo(ctx context.Context) (*SysInfo, error) {
	f, err := d.Do(ctx, CmdSysInfo, nil)
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < sysInfoLen {
		return nil, &ShortReplyError{Cmd: CmdSysInfo, Got: len(f.Payload), Need: sysInfoLen}
	}
	p := f.Payload
	si := &SysInfo{
		// A full 32 bits. The firmware this replaces pushed millis() through
		// itoa() into an int and wrapped every 65 seconds.
		Uptime:     time.Duration(binary.LittleEndian.Uint32(p[0:4])) * time.Millisecond,
		FreeRAM:    binary.LittleEndian.Uint16(p[4:6]),
		ResetCause: ResetCause(p[6]),
	}
	// Appended, #5: a device older than this host sends exactly sysInfoLen
	// bytes and is correct, not truncated -- the reply is complete by the
	// contract it was built against, so the zero value stands for "not
	// reported" rather than for "no errors seen".
	if len(p) >= sysInfoErrCountersEnd {
		si.ErrCRC = binary.LittleEndian.Uint16(p[sysInfoLen : sysInfoLen+2])
		si.ErrFormat = binary.LittleEndian.Uint16(p[sysInfoLen+2 : sysInfoErrCountersEnd])
	}
	return si, nil
}

// ResetMode selects what a reset lands in.
type ResetMode uint8

const (
	// ResetSoft restarts the application firmware.
	ResetSoft ResetMode = 0
	// ResetBootloader restarts into the bootloader, for flashing. It is
	// NOT yet a distinct bootloader entry: the firmware takes the same reset
	// as ResetSoft, so no authenticated session is required. Issue #28 makes
	// it a real path and adds the gate there.
	ResetBootloader ResetMode = 1
)

// Reset asks the device to restart.
//
// The device acknowledges BEFORE restarting and waits for the reply to drain,
// so a returned nil means the command was accepted -- not that the device is
// back. The caller reconnects and waits for the boot event.
func (d *Device) Reset(ctx context.Context, mode ResetMode) error {
	_, err := d.Do(ctx, CmdReset, []byte{byte(mode)})
	return err
}

// PinMode configures a pin's direction.
type PinMode uint8

const (
	// PinInput is a plain input, pull-up off.
	PinInput PinMode = 0
	// PinOutput drives the pin.
	PinOutput PinMode = 1
	// PinInputPullup is an input with the internal pull-up enabled.
	PinInputPullup PinMode = 2
)

// SetPinMode configures a pin.
//
// Pins the firmware is itself using -- the UART, for one -- are refused. That
// is not conservatism: driving the transmit pin cuts the line the command
// arrived on, and the host never learns why the device went quiet.
func (d *Device) SetPinMode(ctx context.Context, pin uint8, mode PinMode) error {
	_, err := d.Do(ctx, CmdPinMode, []byte{pin, byte(mode)})
	return err
}

// DigitalWrite drives a pin high or low.
func (d *Device) DigitalWrite(ctx context.Context, pin uint8, high bool) error {
	level := byte(0)
	if high {
		level = 1
	}
	_, err := d.Do(ctx, CmdDigitalWrite, []byte{pin, level})
	return err
}

// DigitalRead reads the level on a pin. Reads the input register, so it
// reports the voltage on the wire rather than the output latch.
func (d *Device) DigitalRead(ctx context.Context, pin uint8) (bool, error) {
	f, err := d.Do(ctx, CmdDigitalRead, []byte{pin})
	if err != nil {
		return false, err
	}
	if len(f.Payload) < 1 {
		return false, &ShortReplyError{Cmd: CmdDigitalRead, Got: len(f.Payload), Need: 1}
	}
	return f.Payload[0] != 0, nil
}

// TimerSet drives a pin to a level and has the DEVICE drive it back after d.
//
// The deadline is owned by the device, deliberately. A host that crashes with a
// pump running must not leave the pump running, and the only way to guarantee
// that is for the firmware to hold the timer. Re-arming a pin that already has
// one extends it rather than consuming a second slot.
//
// Returns the slot index the device assigned.
func (d *Device) TimerSet(ctx context.Context, pin uint8, high bool, dur time.Duration) (uint8, error) {
	ms := dur.Milliseconds()
	if ms <= 0 || ms > timerMaxMillis {
		return 0, &ArgRangeError{
			Cmd: CmdTimerSet, Arg: "duration", Value: ms,
			Min: 1, Max: timerMaxMillis, Unit: "ms",
		}
	}
	level := byte(0)
	if high {
		level = 1
	}
	p := make([]byte, 6)
	p[0], p[1] = pin, level
	binary.LittleEndian.PutUint32(p[2:], uint32(ms))
	f, err := d.Do(ctx, CmdTimerSet, p)
	if err != nil {
		return 0, err
	}
	if len(f.Payload) < 1 {
		return 0, &ShortReplyError{Cmd: CmdTimerSet, Got: len(f.Payload), Need: 1}
	}
	return f.Payload[0], nil
}

// TimerCancel stops a pin's timer. If applyFinal is true the pin is driven to
// the level it would have reverted to; otherwise it is left where it is.
func (d *Device) TimerCancel(ctx context.Context, pin uint8, applyFinal bool) error {
	f := byte(0)
	if applyFinal {
		f = 1
	}
	_, err := d.Do(ctx, CmdTimerCancel, []byte{pin, f})
	return err
}

// TimerState is what a pin's timer is doing.
type TimerState struct {
	Active    bool
	HeldLevel bool
	Remaining time.Duration
}

// TimerQuery reports a pin's timer.
func (d *Device) TimerQuery(ctx context.Context, pin uint8) (*TimerState, error) {
	f, err := d.Do(ctx, CmdTimerQuery, []byte{pin})
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < timerQueryLen {
		return nil, &ShortReplyError{Cmd: CmdTimerQuery, Got: len(f.Payload), Need: timerQueryLen}
	}
	return &TimerState{
		Active:    f.Payload[0] != 0,
		HeldLevel: f.Payload[1] != 0,
		Remaining: time.Duration(binary.LittleEndian.Uint32(f.Payload[2:6])) * time.Millisecond,
	}, nil
}

// AnalogRead returns the RAW converter count, 0..1023 on a 10-bit ADC.
//
// Not a voltage and not a percentage. Calibration is a property of the sensor
// and its wiring, both of which the host knows and the device cannot. The
// firmware this project replaces reported a moisture reading as a percentage
// it was not, and called the same function on a flow meter that outputs pulses.
func (d *Device) AnalogRead(ctx context.Context, pin uint8) (uint16, error) {
	f, err := d.Do(ctx, CmdAnalogRead, []byte{pin})
	if err != nil {
		return 0, err
	}
	if len(f.Payload) < 2 {
		return 0, &ShortReplyError{Cmd: CmdAnalogRead, Got: len(f.Payload), Need: 2}
	}
	return binary.LittleEndian.Uint16(f.Payload[:2]), nil
}

// ADCReference selects what the converter measures against.
type ADCReference uint8

const (
	// ADCRefVCC measures against the supply rail.
	ADCRefVCC ADCReference = 0
	// ADCRefExternal measures against the AREF pin.
	ADCRefExternal ADCReference = 1
	// ADCRefInternal measures against the internal bandgap.
	ADCRefInternal ADCReference = 2
)

// SetADCReference selects the ADC reference.
func (d *Device) SetADCReference(ctx context.Context, ref ADCReference) error {
	_, err := d.Do(ctx, CmdADCRef, []byte{byte(ref)})
	return err
}

// EEPROMChunk is the largest read or write the device accepts in one command.
//
// Chunked because a byte takes about 3.4 ms to commit: a 64-byte write would
// hold the device's main loop for 220 ms, long enough to miss a scheduled
// deadline. The host does the chunking; the device stays responsive.
const EEPROMChunk = 8

// EEPROMReservedEnd is the first address a host may always write.
//
// Below it live the bootloader's public-key hash, the device serial, the boot
// counter, the OTA state and -- element tier only, since issue #4 -- the
// AUTH_CHALLENGE/AUTH_RESPONSE session's operator public key. The device
// refuses writes there unconditionally, and it must: without that refusal the
// generic EEPROM primitive would let anyone on the serial line overwrite the
// hash the bootloader verifies against, or the operator key that gates
// reserved-region reads.
//
// Grew from 0x80 to 0xC0 when the operator public key was added: an address a
// pre-#4 host could once write now falls inside the refused region if it
// happens to land in [0x80, 0xC0). That is a real, deliberate narrowing --
// see docs/wire-protocol/api.md.
const EEPROMReservedEnd = 0xC0

// EEPROMRead reads up to EEPROMChunk bytes.
//
// On an element-tier device, an address below EEPROMReservedEnd additionally
// requires an authenticated session (see AuthChallenge/AuthResponse) --
// StatusErrAuthRequired otherwise. On a soft-tier or CRYPTO=none device this
// is NOT enforced: AUTH_CHALLENGE/AUTH_RESPONSE are not compiled into either
// tier (the element is what makes an operator signature checkable without a
// shared secret on the device), so every read is permitted there exactly as
// it always was. See docs/security/threat-model.md for why that gap is
// accepted rather than hidden.
func (d *Device) EEPROMRead(ctx context.Context, addr uint16, n uint8) ([]byte, error) {
	if n == 0 || n > EEPROMChunk {
		return nil, &ArgRangeError{
			Cmd: CmdEEPROMRead, Arg: "length", Value: int64(n),
			Min: 1, Max: EEPROMChunk, Unit: "bytes",
		}
	}
	p := []byte{byte(addr), byte(addr >> 8), n}
	f, err := d.Do(ctx, CmdEEPROMRead, p)
	if err != nil {
		return nil, err
	}
	return f.Payload, nil
}

// EEPROMWrite writes up to EEPROMChunk bytes at or above EEPROMReservedEnd.
func (d *Device) EEPROMWrite(ctx context.Context, addr uint16, data []byte) error {
	if len(data) == 0 || len(data) > EEPROMChunk {
		return &ArgRangeError{
			Cmd: CmdEEPROMWrite, Arg: "length", Value: int64(len(data)),
			Min: 1, Max: EEPROMChunk, Unit: "bytes",
		}
	}
	p := append([]byte{byte(addr), byte(addr >> 8)}, data...)
	_, err := d.Do(ctx, CmdEEPROMWrite, p)
	return err
}

// PWMWrite sets a duty cycle, 0..255. Zero disconnects the pin from its
// compare unit rather than holding it low through the timer, which would
// quietly prevent DigitalWrite from working on it afterwards.
//
// Pins whose compare unit belongs to the millisecond tick return
// StatusErrUnsupported. Caps reports that up front, so a host never has to
// discover it here.
func (d *Device) PWMWrite(ctx context.Context, pin uint8, duty uint8) error {
	_, err := d.Do(ctx, CmdPWMWrite, []byte{pin, duty})
	return err
}

// Tone drives a square wave on a pin. Zero hertz stops it.
func (d *Device) Tone(ctx context.Context, pin uint8, hz uint16) error {
	p := []byte{pin, 0, 0}
	binary.LittleEndian.PutUint16(p[1:], hz)
	_, err := d.Do(ctx, CmdTone, p)
	return err
}

// NoTone stops the tone generator.
func (d *Device) NoTone(ctx context.Context, pin uint8) error {
	_, err := d.Do(ctx, CmdNoTone, []byte{pin})
	return err
}

// PulseIn measures one pulse of the given level.
func (d *Device) PulseIn(ctx context.Context, pin uint8, high bool, timeout time.Duration) (time.Duration, error) {
	level := byte(0)
	if high {
		level = 1
	}
	us := timeout.Microseconds()
	p := make([]byte, 6)
	p[0], p[1] = pin, level
	binary.LittleEndian.PutUint32(p[2:], uint32(us))
	f, err := d.Do(ctx, CmdPulseIn, p)
	if err != nil {
		return 0, err
	}
	if len(f.Payload) < 4 {
		return 0, &ShortReplyError{Cmd: CmdPulseIn, Got: len(f.Payload), Need: 4}
	}
	return time.Duration(binary.LittleEndian.Uint32(f.Payload[:4])) * time.Microsecond, nil
}

// PulseCapture records edge-to-edge intervals and returns them in microseconds.
//
// This is what makes timing-critical sensors work without any sensor living in
// the firmware. A DHT22 distinguishes a bit by a 26- versus 70-microsecond
// pulse, which no protocol with a round trip per bit can read; capturing the
// intervals and decoding them here keeps the device generic. The same call
// reads DHT11/22, HC-SR04, IR remotes and RC PPM.
func (d *Device) PulseCapture(ctx context.Context, pin uint8, startHigh bool, maxEdges uint8, timeout time.Duration) ([]uint16, error) {
	start := byte(0)
	if startHigh {
		start = 1
	}
	p := make([]byte, 7)
	p[0], p[1], p[2] = pin, start, maxEdges
	binary.LittleEndian.PutUint32(p[3:], uint32(timeout.Microseconds()))
	f, err := d.Do(ctx, CmdPulseCapture, p)
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < 1 {
		return nil, &ShortReplyError{Cmd: CmdPulseCapture, Got: len(f.Payload), Need: 1}
	}
	n := int(f.Payload[0])
	if len(f.Payload) < 1+n*2 {
		return nil, &ShortReplyError{Cmd: CmdPulseCapture, Got: len(f.Payload), Need: 1 + n*2}
	}
	out := make([]uint16, n)
	for i := range out {
		out[i] = binary.LittleEndian.Uint16(f.Payload[1+i*2:])
	}
	return out, nil
}

// I2CReservedAddr is the address the device refuses.
//
// The secure element sits on the same bus. A generic transfer that could
// address it would let a host reach the element around the firmware's crypto
// layer, making the device's own root of trust an ordinary peripheral.
const I2CReservedAddr = 0x60

// I2CTransfer writes then reads in one transaction.
func (d *Device) I2CTransfer(ctx context.Context, addr7 uint8, write []byte, readLen uint8) ([]byte, error) {
	p := append([]byte{addr7, readLen}, write...)
	f, err := d.Do(ctx, CmdI2CXfer, p)
	if err != nil {
		return nil, err
	}
	return f.Payload, nil
}

// SPITransfer clocks len(write) bytes out and the same number in, with the
// chip-select asserted throughout.
func (d *Device) SPITransfer(ctx context.Context, cs uint8, write []byte) ([]byte, error) {
	f, err := d.Do(ctx, CmdSPIXfer, append([]byte{cs}, write...))
	if err != nil {
		return nil, err
	}
	return f.Payload, nil
}

// I2CClockMin and I2CClockMax bound what SetI2CClock will send. Mirrors
// AG_I2C_HZ_MIN/AG_I2C_HZ_MAX in include/ag/config.h: below the floor the
// device's clock divisor cannot express the request meaningfully, and above
// the ceiling -- Fast-mode, 400 kHz -- is past what this family's TWI
// peripherals are rated for, not merely what their divisor can compute.
const (
	I2CClockMin = 1_000
	I2CClockMax = 400_000
)

// SetI2CClock reconfigures the generic I2C bus's clock. Applied immediately;
// a transfer already needing the previous rate should complete before this is
// sent.
//
// ag_bus_init() (main.c) already set 100 kHz at boot, so this is only needed
// to run the bus at something else -- a sensor datasheet asking for 400 kHz,
// say.
//
// On a device whose CryptoTier (from Caps) is CryptoSecureElement, every call
// is refused with StatusErrProtectedRegion, checked before the range above:
// the secure element's wake pulse needs this exact peripheral at or below
// 100 kHz, and a rate accepted here would be silently undone the next time
// any crypto operation ran.
func (d *Device) SetI2CClock(ctx context.Context, hz uint32) error {
	if hz < I2CClockMin || hz > I2CClockMax {
		return &ArgRangeError{
			Cmd: CmdI2CConfig, Arg: "clock", Value: int64(hz),
			Min: I2CClockMin, Max: I2CClockMax, Unit: "hz",
		}
	}
	p := make([]byte, 4)
	binary.LittleEndian.PutUint32(p, hz)
	_, err := d.Do(ctx, CmdI2CConfig, p)
	return err
}

// SPIMode is SPI's clock polarity and phase, numbered the way every SPI
// datasheet does: 0 through 3.
type SPIMode uint8

const (
	SPIMode0 SPIMode = 0
	SPIMode1 SPIMode = 1
	SPIMode2 SPIMode = 2
	SPIMode3 SPIMode = 3
)

// SPIClockDiv is the raw two-bit divider field ag_hal_spi_init writes into
// SPCR's SPR1:SPR0 -- a code, not a divisor, because the actual divisor is a
// property of the board's F_CPU that this SDK has no way to know.
type SPIClockDiv uint8

const (
	// SPIClockDiv4 is the fastest this HAL can ask the peripheral for.
	SPIClockDiv4  SPIClockDiv = 0
	SPIClockDiv16 SPIClockDiv = 1
	SPIClockDiv64 SPIClockDiv = 2
	// SPIClockDiv128 is the slowest, and what ag_bus_init() sets at boot.
	SPIClockDiv128 SPIClockDiv = 3
)

// ConfigureSPI reconfigures the generic SPI bus's mode, clock divider and bit
// order. Applied immediately; a transfer already in flight should complete
// before this is sent.
//
// ag_bus_init() (main.c) already set mode 0, the slowest divider and
// MSB-first at boot, so this is only needed to match a specific device --
// mode 3 and MSB-first for an SD card, say.
func (d *Device) ConfigureSPI(ctx context.Context, mode SPIMode, clkdiv SPIClockDiv, lsbFirst bool) error {
	if mode > SPIMode3 {
		return &ArgRangeError{
			Cmd: CmdSPIConfig, Arg: "mode", Value: int64(mode), Min: 0, Max: 3, Unit: "mode",
		}
	}
	if clkdiv > SPIClockDiv128 {
		return &ArgRangeError{
			Cmd: CmdSPIConfig, Arg: "clkdiv", Value: int64(clkdiv), Min: 0, Max: 3, Unit: "divider code",
		}
	}
	lsb := byte(0)
	if lsbFirst {
		lsb = 1
	}
	_, err := d.Do(ctx, CmdSPIConfig, []byte{byte(mode), byte(clkdiv), lsb})
	return err
}

// OneWireReset pulses the bus and reports whether any device answered.
func (d *Device) OneWireReset(ctx context.Context, pin uint8) (bool, error) {
	f, err := d.Do(ctx, CmdOneWireReset, []byte{pin})
	if err != nil {
		return false, err
	}
	if len(f.Payload) < 1 {
		return false, &ShortReplyError{Cmd: CmdOneWireReset, Got: len(f.Payload), Need: 1}
	}
	return f.Payload[0] != 0, nil
}

// OneWireWrite sends bytes on the bus.
func (d *Device) OneWireWrite(ctx context.Context, pin uint8, data []byte) error {
	_, err := d.Do(ctx, CmdOneWireWrite, append([]byte{pin}, data...))
	return err
}

// OneWireRead reads n bytes from the bus.
func (d *Device) OneWireRead(ctx context.Context, pin uint8, n uint8) ([]byte, error) {
	f, err := d.Do(ctx, CmdOneWireRead, []byte{pin, n})
	if err != nil {
		return nil, err
	}
	return f.Payload, nil
}
