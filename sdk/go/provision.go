// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
)

// Provisioning commands.
//
// These exist on the wire permanently and are implemented almost nowhere: a
// production firmware is built with TFW_FEATURE_PROVISION 0, so its dispatch
// table has no entry for any of them and it answers StatusErrUnknownCmd. Only the
// image on a provisioning jig carries the handlers, and that image is replaced
// before the part leaves the bench.
//
// That is deliberate and it is the security property. A device that can be
// re-provisioned over its own command channel has no identity worth attesting
// to, because whoever reaches the serial port becomes the manufacturer. The ids
// are reserved rather than reused so a provisioning host and a production
// device can never misunderstand each other.
const (
	CmdProvInfo        Cmd = 0x70
	CmdProvWriteConfig Cmd = 0x71
	CmdProvLock        Cmd = 0x72
	CmdProvGenKey      Cmd = 0x73
	CmdProvPubKey      Cmd = 0x74
	CmdProvSeedUDS     Cmd = 0x75
	CmdProvWritePubKey Cmd = 0x7B // [slot:1][half:1][coordinate:32] -> X||Y read back
	CmdProvSlotLock    Cmd = 0x7D // [slot:1][crc:2 LE] -> empty
	CmdProvConfigRead  Cmd = 0x7E // [block:1] 0..3 -> the 32 bytes of that block
)

// Element zones, for Lock. Locking is irreversible on the part; see
// docs/device-provisioning/user.md.
const (
	ZoneConfig uint8 = 0x00
	ZoneData   uint8 = 0x01
)

// Slot assignments, mirroring TFW_ATECC_SLOT_* in src/crypto/atecc608a/atecc.h.
// A template that disagrees with these produces a part that is configured for a
// firmware other than the one that will run on it, and the element cannot be
// unlocked to correct it.
//
// Every board's map is the TrustFLEX one (boards/*/board_profile.h,
// docs/device-provisioning/adr-trustflex-608c-slot-map.md, (a1)): the DeviceID
// in slot 0, as a pre-locked ATECC608C-TFLXTLS ships it, and the UDS in slot
// 6, whose shipped I/O-protection role it takes over. SlotTrustFLEXUDS, the
// name slot 6 had while the blank-part plan still seeded slot 1, is gone:
// SlotUDS is slot 6.
const (
	SlotDeviceID uint8 = 0
	SlotUDS      uint8 = 6
)

// The slots ProvWritePubKey writes. SlotSecureBootKey, 15, is the image
// slot of every board's map (TFW_ATECC_SLOT_IMAGE) and the TrustFLEX part's
// secure-boot public key (DS40002515B table 3-16).
const (
	// SlotImageSigning names slot 2, the image slot before the TrustFLEX
	// map; no shipped image accepts it any more (PROV_WRITE_PUBKEY answers
	// StatusErrRange).
	//
	// Deprecated: use SlotSecureBootKey.
	SlotImageSigning  uint8 = 2
	SlotSecureBootKey uint8 = 15
)

// ElementInfo is what a provisioning jig can learn about the part before it
// writes anything.
type ElementInfo struct {
	// Revision identifies the die. The third byte is 0x60 on the 608 family.
	Revision [4]byte

	// Serial is the element's own nine-byte serial number, which the part
	// splits across two ranges of config block 0 -- bytes 0..3 and 8..12. The
	// split is the element's; reassembling it here rather than on the device
	// keeps the firmware from having an opinion about the layout.
	Serial [9]byte

	// ConfigLocked and DataLocked are decoded from config bytes 86 and 87,
	// where 0x55 means unlocked and anything else means locked. Note the
	// polarity: the value that looks like a flag being SET is the zone being
	// OPEN.
	ConfigLocked bool
	DataLocked   bool

	// ConfigBlock0 is the raw block, kept so a caller can compare a part
	// against a template byte for byte rather than against this struct's view
	// of it.
	ConfigBlock0 [32]byte
}

// ProvInfo reads the element's identity and lock state.
//
// This is the only provisioning call that is safe to make on a part you care
// about: it writes nothing.
func (d *Device) ProvInfo(ctx context.Context) (*ElementInfo, error) {
	f, err := d.Do(ctx, CmdProvInfo, nil)
	if err != nil {
		return nil, err
	}
	// 4 revision bytes, config block 0, then the two lock bytes.
	//
	// The lock bytes are NOT inside block 0. The element puts LockValue at
	// config byte 86 and LockConfig at 87, which is block 2 -- so this used to
	// bounds-check at 36 and then index p[90] and p[91], which no reply the
	// protocol can carry ever reaches: MaxPayload is 64 and the device sent 36.
	// Every legal reply panicked, and nothing caught it because this file had
	// no test. The device sends them explicitly now.
	const need = 4 + 32 + 2
	if len(f.Payload) < need {
		return nil, &ShortReplyError{Cmd: CmdProvInfo, Got: len(f.Payload), Need: need}
	}
	p := f.Payload
	// 0x55 is the element's "unlocked" value. Note the polarity: the byte that
	// looks like a flag being SET is the zone being OPEN.
	info := &ElementInfo{
		DataLocked:   p[36] != 0x55,
		ConfigLocked: p[37] != 0x55,
	}
	copy(info.Revision[:], p[0:4])
	copy(info.ConfigBlock0[:], p[4:4+32])
	copy(info.Serial[0:4], p[4:8])
	copy(info.Serial[4:9], p[12:17])
	return info, nil
}

// ProvWriteConfig writes one 32-byte block of the element's config zone.
//
// Blocks 0 and 1 are refused by the device: they hold the serial number, the
// revision and the OTP mode, which are not ours to set. The refusal comes from
// the firmware rather than the element so that a template being debugged gets
// "protected region" instead of the element's generic execution error.
func (d *Device) ProvWriteConfig(ctx context.Context, block uint8, data []byte) error {
	if len(data) != 32 {
		return &ArgRangeError{Cmd: CmdProvWriteConfig, Arg: "data", Value: int64(len(data)), Min: 32, Max: 32, Unit: "bytes"}
	}
	payload := make([]byte, 0, 33)
	payload = append(payload, block)
	payload = append(payload, data...)
	_, err := d.Do(ctx, CmdProvWriteConfig, payload)
	return err
}

// ProvLock locks a zone. THIS CANNOT BE UNDONE.
//
// The summary CRC is mandatory, and refusing to make it optional is the point.
// The element accepts a no-CRC form that locks without checking the zone holds
// what the caller believes it holds -- which turns one transcription error in a
// template into a part with no erase and no reset. Requiring it makes the
// element verify the zone against the host's expectation before the
// irreversible step, so the two have to agree.
func (d *Device) ProvLock(ctx context.Context, zone uint8, summaryCRC uint16) error {
	if summaryCRC == 0 {
		return &ArgRangeError{Cmd: CmdProvLock, Arg: "summaryCRC", Value: 0, Min: 1, Max: 0xFFFF, Unit: ""}
	}
	payload := []byte{zone, 0, 0}
	binary.LittleEndian.PutUint16(payload[1:], summaryCRC)
	_, err := d.Do(ctx, CmdProvLock, payload)
	return err
}

// ProvSlotLock locks one data slot of a part whose zones are already locked,
// the element's Lock in SlotLock mode. THIS CANNOT BE UNDONE.
//
// crc is the CRC-16 of the slot's contents, and like ProvLock's summary CRC it
// is mandatory: the element locks only if the slot holds what the host
// believes it holds. The firmware accepts only the board's UDS slot and
// image slot (StatusErrRange otherwise) and refuses a slot already locked
// (StatusErrProtectedRegion). This is the image slot's lock: the UDS slot's
// CRC is of a secret the host never holds, so a non-zero crc for it is
// StatusErrBadArgc; lock it with ProvSlotLockUDS.
func (d *Device) ProvSlotLock(ctx context.Context, slot uint8, crc uint16) error {
	if crc == 0 {
		return &ArgRangeError{Cmd: CmdProvSlotLock, Arg: "crc", Value: 0, Min: 1, Max: 0xFFFF, Unit: ""}
	}
	payload := []byte{slot, 0, 0}
	binary.LittleEndian.PutUint16(payload[1:], crc)
	_, err := d.Do(ctx, CmdProvSlotLock, payload)
	return err
}

// ProvSlotLockUDS slot-locks the UDS slot, the element's Lock in SlotLock
// mode. THIS CANNOT BE UNDONE.
//
// The UDS comes from the element's TRNG and never crosses the host link
// (ProvSeedUDS), so the host cannot compute its CRC. The request carries a
// zero CRC and the firmware locks against the CRC this boot's PROV_SEED_UDS
// took, after checking that an HMAC keyed by the slot works. With no seed
// this boot the firmware answers StatusErrBadState; a slot other than the
// board's UDS slot is StatusErrBadArgc or StatusErrRange from the firmware.
func (d *Device) ProvSlotLockUDS(ctx context.Context, slot uint8) error {
	_, err := d.Do(ctx, CmdProvSlotLock, []byte{slot, 0, 0})
	return err
}

// ProvConfigRead reads one 32-byte block, 0 to 3, of the element's config
// zone. It writes nothing.
func (d *Device) ProvConfigRead(ctx context.Context, block uint8) ([]byte, error) {
	if block > 3 {
		return nil, &ArgRangeError{Cmd: CmdProvConfigRead, Arg: "block", Value: int64(block), Min: 0, Max: 3, Unit: ""}
	}
	f, err := d.Do(ctx, CmdProvConfigRead, []byte{block})
	if err != nil {
		return nil, err
	}
	const need = 32
	if len(f.Payload) != need {
		return nil, &ShortReplyError{Cmd: CmdProvConfigRead, Got: len(f.Payload), Need: need}
	}
	return append([]byte(nil), f.Payload...), nil
}

// ProvGenKey generates the DeviceID keypair inside the element and returns the
// public point. The private half never exists anywhere else.
//
// Separate from ProvPubKey on purpose: this one REPLACES the device's identity,
// and a verify path that could reach it would destroy what it was checking.
func (d *Device) ProvGenKey(ctx context.Context, slot uint8) ([]byte, error) {
	return d.provKey(ctx, CmdProvGenKey, slot)
}

// ProvPubKey reads a public point back without writing anything. For slot 0,
// the DeviceID, the firmware recomputes it from the private key already in
// the slot; for the image slot (SlotSecureBootKey) it reads the stored key
// back from the slot. It generates nothing, which is what makes `tfwprov
// --verify` safe to run against a provisioned part.
func (d *Device) ProvPubKey(ctx context.Context, slot uint8) ([]byte, error) {
	return d.provKey(ctx, CmdProvPubKey, slot)
}

func (d *Device) provKey(ctx context.Context, cmd Cmd, slot uint8) ([]byte, error) {
	f, err := d.Do(ctx, cmd, []byte{slot})
	if err != nil {
		return nil, err
	}
	const need = 64 // an uncompressed P-256 point, X||Y
	if len(f.Payload) < need {
		return nil, &ShortReplyError{Cmd: cmd, Got: len(f.Payload), Need: need}
	}
	out := make([]byte, need)
	copy(out, f.Payload[:need])
	return out, nil
}

// ProvSeedUDS fills the UDS slot from the element's own TRNG.
//
// It takes a slot and NO key material, and that is the design rather than an
// omission. A UDS the host generated is a UDS the host has seen, and a
// provisioning station is exactly the machine an attacker would target to get
// it. The secret is drawn by the element's RNG and crosses only the I2C bus
// (three times, through MCU SRAM), never the host link, so a compromised jig
// learns nothing it could use.
func (d *Device) ProvSeedUDS(ctx context.Context, slot uint8) error {
	_, err := d.Do(ctx, CmdProvSeedUDS, []byte{slot})
	return err
}

// ProvWritePubKey writes an uncompressed P-256 public key, X||Y, into a data
// slot and checks it there.
//
// A request carries one coordinate, so the key goes as two: X, then Y. Each
// reply is the slot as the element reads it back, and the second must be the
// key written; anything else is a *ReadBackError, and the slot holds what Got
// says. The firmware refuses a slot other than SlotImageSigning or
// SlotSecureBootKey with StatusErrRange, a config zone not yet locked with
// StatusErrBadState, and a slot already locked with StatusErrProtectedRegion.
func (d *Device) ProvWritePubKey(ctx context.Context, slot uint8, key []byte) error {
	const need = 64 // an uncompressed P-256 point, X||Y
	if len(key) != need {
		return &ArgRangeError{Cmd: CmdProvWritePubKey, Arg: "key", Value: int64(len(key)),
			Min: need, Max: need, Unit: "bytes"}
	}
	var back []byte
	for half := 0; half < 2; half++ {
		p := make([]byte, 0, 2+need/2)
		p = append(p, slot, byte(half))
		p = append(p, key[half*need/2:(half+1)*need/2]...)
		f, err := d.Do(ctx, CmdProvWritePubKey, p)
		if err != nil {
			return err
		}
		if len(f.Payload) < need {
			return &ShortReplyError{Cmd: CmdProvWritePubKey, Got: len(f.Payload), Need: need}
		}
		back = f.Payload[:need]
	}
	if !bytes.Equal(back, key) {
		return &ReadBackError{Slot: slot, Want: append([]byte(nil), key...), Got: append([]byte(nil), back...)}
	}
	return nil
}

// ReadBackError reports a slot that, read back after a write the element
// accepted, does not hold what was written.
type ReadBackError struct {
	Slot      uint8
	Want, Got []byte
}

// Error implements error. The text is for a log line: match with errors.As.
func (e *ReadBackError) Error() string {
	return fmt.Sprintf("tfw: slot %d reads back %x, wrote %x", e.Slot, e.Got, e.Want)
}
