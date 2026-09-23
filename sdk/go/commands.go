// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"encoding/binary"
	"fmt"
	"time"
)

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
	if len(f.Payload) < 14 {
		return nil, fmt.Errorf("aggeneric: CAPS reply is %d bytes, expected at least 14", len(f.Payload))
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
		Features:      binary.LittleEndian.Uint32(p[10:14]),
	}
	// A major mismatch means the frames themselves may not parse the same way.
	// Minor is tolerated in both directions: that is what lets a fleet run
	// mixed firmware without a flag day.
	if c.ProtoMajor != 1 {
		return c, fmt.Errorf("aggeneric: device speaks protocol major %d, this host speaks 1", c.ProtoMajor)
	}
	return c, nil
}

// SysInfo reads the device's runtime state.
func (d *Device) SysInfo(ctx context.Context) (*SysInfo, error) {
	f, err := d.Do(ctx, CmdSysInfo, nil)
	if err != nil {
		return nil, err
	}
	if len(f.Payload) < 7 {
		return nil, fmt.Errorf("aggeneric: SYS_INFO reply is %d bytes, expected at least 7", len(f.Payload))
	}
	p := f.Payload
	return &SysInfo{
		// A full 32 bits. The firmware this replaces pushed millis() through
		// itoa() into an int and wrapped every 65 seconds.
		Uptime:     time.Duration(binary.LittleEndian.Uint32(p[0:4])) * time.Millisecond,
		FreeRAM:    binary.LittleEndian.Uint16(p[4:6]),
		ResetCause: ResetCause(p[6]),
	}, nil
}

// ResetMode selects what a reset lands in.
type ResetMode uint8

const (
	// ResetSoft restarts the application firmware.
	ResetSoft ResetMode = 0
	// ResetBootloader restarts into the bootloader, for flashing. Once
	// attestation exists this requires an authenticated session: it is a
	// firmware-replacement entry point reachable from the serial line.
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
		return false, fmt.Errorf("aggeneric: DIGITAL_READ returned no value")
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
	if ms <= 0 || ms > 0x7FFFFFFF {
		return 0, fmt.Errorf("aggeneric: duration %s is outside 1ms..24.8 days", dur)
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
		return 0, fmt.Errorf("aggeneric: TIMER_SET returned no slot")
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
	if len(f.Payload) < 6 {
		return nil, fmt.Errorf("aggeneric: TIMER_QUERY reply is %d bytes, expected 6", len(f.Payload))
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
		return 0, fmt.Errorf("aggeneric: ANALOG_READ reply is %d bytes, expected 2", len(f.Payload))
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

// EEPROMReservedEnd is the first address a host may write.
//
// Below it live the bootloader's public-key hash, the device serial, the boot
// counter and the OTA state. The device refuses writes there, and it must:
// without that refusal the generic EEPROM primitive would let anyone on the
// serial line overwrite the hash the bootloader verifies against.
const EEPROMReservedEnd = 0x80

// EEPROMRead reads up to EEPROMChunk bytes. Reads of the reserved region are
// permitted -- its contents are a hash and a serial number, public by nature.
func (d *Device) EEPROMRead(ctx context.Context, addr uint16, n uint8) ([]byte, error) {
	if n == 0 || n > EEPROMChunk {
		return nil, fmt.Errorf("aggeneric: read of %d bytes, limit is %d", n, EEPROMChunk)
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
		return fmt.Errorf("aggeneric: write of %d bytes, limit is %d", len(data), EEPROMChunk)
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
		return 0, fmt.Errorf("aggeneric: PULSE_IN returned no measurement")
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
		return nil, fmt.Errorf("aggeneric: PULSE_CAPTURE returned no count")
	}
	n := int(f.Payload[0])
	if len(f.Payload) < 1+n*2 {
		return nil, fmt.Errorf("aggeneric: PULSE_CAPTURE claimed %d intervals but sent %d bytes", n, len(f.Payload)-1)
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

// OneWireReset pulses the bus and reports whether any device answered.
func (d *Device) OneWireReset(ctx context.Context, pin uint8) (bool, error) {
	f, err := d.Do(ctx, CmdOneWireReset, []byte{pin})
	if err != nil {
		return false, err
	}
	if len(f.Payload) < 1 {
		return false, fmt.Errorf("aggeneric: ONEWIRE_RESET returned nothing")
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
