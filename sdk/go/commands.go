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
