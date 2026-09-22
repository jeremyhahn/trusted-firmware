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
