// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"encoding/binary"
	"fmt"
	"math"
)

// The OTA session -- docs/ota/. The same four commands, with the same payloads
// and states, are answered by tfw-boot (into application flash) and by an
// application image that links the stager (into the external store, issue
// #75); a host drives both the same way. An image without either answers
// StatusErrUnknownCmd.
//
// Over Ethernet all four are on the link's allow-list (ADR decision 8).
const (
	CmdOTABegin  Cmd = 0x68 // [total_len:4] -> -
	CmdOTAData   Cmd = 0x69 // [offset:4][bytes] -> -
	CmdOTAEnd    Cmd = 0x6A // - -> -
	CmdOTAStatus Cmd = 0x6B // - -> [state:1][received:4]
)

// The rest of ADR decision 8 (#77), answered by an image whose tfw-boot runs
// the trial and which stages into the store; any other image answers
// StatusErrUnknownCmd. Serial only: none of the three is on the Ethernet
// allow-list.
const (
	CmdOTAInfo     Cmd = 0x6C // [slot:1] -> 44 bytes, see SlotInfo
	CmdOTAActivate Cmd = 0x6D // [mode:1] -> -
	CmdOTAConfirm  Cmd = 0x6E // - -> -
)

// OTAInfoLen is OTA_INFO's reply length.
const OTAInfoLen = 44

// OTA_ACTIVATE's mode byte.
const (
	OTAActivateAtReset  byte = 0 // tried at the next reset
	OTAActivateResetNow byte = 1 // the device resets once the reply is out
)

// SlotInfo is OTA_INFO's reply:
// [slots:1][active:1][flags:1][floor:4][state:1][image_version:4][measurement:32].
// State is the platform block's numbering (include/tfw/boot.h): boot_state
// for slot 0, ota_state (or PENDING for a marked image) for slot 1.
type SlotInfo struct {
	Slots        uint8
	Active       uint8
	Flags        uint8
	Floor        uint32
	State        uint8
	ImageVersion uint32
	Measurement  [32]byte
}

// OTA_INFO flags.
const (
	SlotFellBack uint8 = 1 << 0 // the last boot fell back
	SlotRefused  uint8 = 1 << 1 // the fallback followed a refusal at evaluation
	SlotTrial    uint8 = 1 << 2 // the device is in TRIAL
)

// OTAInfo reads one slot's state: 0 the running image, 1 the store.
func (d *Device) OTAInfo(ctx context.Context, slot byte) (*SlotInfo, error) {
	f, err := d.Do(ctx, CmdOTAInfo, []byte{slot})
	if err != nil {
		return nil, err
	}
	p := f.Payload
	if len(p) < OTAInfoLen {
		return nil, &ShortReplyError{Cmd: CmdOTAInfo, Got: len(p), Need: OTAInfoLen}
	}
	si := &SlotInfo{
		Slots: p[0], Active: p[1], Flags: p[2],
		Floor:        binary.LittleEndian.Uint32(p[3:7]),
		State:        p[7],
		ImageVersion: binary.LittleEndian.Uint32(p[8:12]),
	}
	copy(si.Measurement[:], p[12:44])
	return si, nil
}

// OTAActivate takes the staged image for the next boot. The device reads it
// back and checks it first; one that cannot be verified is refused with
// StatusErrBadState and nothing changes.
func (d *Device) OTAActivate(ctx context.Context, mode byte) error {
	if mode > OTAActivateResetNow {
		return &ArgRangeError{
			Cmd: CmdOTAActivate, Arg: "mode", Value: int64(mode),
			Min: 0, Max: int64(OTAActivateResetNow),
		}
	}
	_, err := d.Do(ctx, CmdOTAActivate, []byte{mode})
	return err
}

// OTAConfirm confirms the image on trial, after attestation. Outside TRIAL
// it is refused with StatusErrBadState.
func (d *Device) OTAConfirm(ctx context.Context) error {
	_, err := d.Do(ctx, CmdOTAConfirm, nil)
	return err
}

// OTAChunk is the most image bytes one OTA_DATA carries: the payload less the
// offset.
const OTAChunk = 60

// OTAState is OTA_STATUS's state byte. The numbering is wire.
type OTAState uint8

const (
	OTAIdle      OTAState = 0
	OTAReceiving OTAState = 1
	OTAComplete  OTAState = 2
	OTAFailed    OTAState = 3
)

func (s OTAState) String() string {
	switch s {
	case OTAIdle:
		return "idle"
	case OTAReceiving:
		return "receiving"
	case OTAComplete:
		return "complete"
	case OTAFailed:
		return "failed"
	}
	return fmt.Sprintf("OTAState(%d)", uint8(s))
}

// OTABegin opens a session for an image of total bytes, trailer included. A
// new BEGIN abandons any session in progress; on the stager it also withdraws
// the staged record before a byte moves. The device refuses a length that is
// not whole 256-byte pages or does not fit its area with StatusErrRange.
func (d *Device) OTABegin(ctx context.Context, total uint32) error {
	p := binary.LittleEndian.AppendUint32(nil, total)
	_, err := d.Do(ctx, CmdOTABegin, p)
	return err
}

// OTAData sends the bytes at offset, which must be the next one the device
// expects; anything else is StatusErrRange.
func (d *Device) OTAData(ctx context.Context, offset uint32, data []byte) error {
	if len(data) == 0 || len(data) > OTAChunk {
		return &ArgRangeError{
			Cmd: CmdOTAData, Arg: "length", Value: int64(len(data)),
			Min: 1, Max: OTAChunk, Unit: "bytes",
		}
	}
	p := binary.LittleEndian.AppendUint32(make([]byte, 0, 4+len(data)), offset)
	_, err := d.Do(ctx, CmdOTAData, append(p, data...))
	return err
}

// OTAEnd closes the session and has the device check the image against its
// trailer. Before every byte has arrived it is StatusErrFraming; an image
// whose measurement disagrees is StatusErrCrypto, and the session is failed.
func (d *Device) OTAEnd(ctx context.Context) error {
	_, err := d.Do(ctx, CmdOTAEnd, nil)
	return err
}

// OTAStatus reports the session's state and how many bytes it has taken.
func (d *Device) OTAStatus(ctx context.Context) (OTAState, uint32, error) {
	f, err := d.Do(ctx, CmdOTAStatus, nil)
	if err != nil {
		return 0, 0, err
	}
	if len(f.Payload) < 5 {
		return 0, 0, &ShortReplyError{Cmd: CmdOTAStatus, Got: len(f.Payload), Need: 5}
	}
	return OTAState(f.Payload[0]), binary.LittleEndian.Uint32(f.Payload[1:5]), nil
}

// StageImage runs a whole session: BEGIN, the image in OTAChunk pieces, END.
// It stops at the first refusal and returns it; OTAStatus says how far the
// device got, and a fresh StageImage starts over.
func (d *Device) StageImage(ctx context.Context, image []byte) error {
	total, err := otaTotal(int64(len(image)))
	if err != nil {
		return err
	}
	if err := d.OTABegin(ctx, total); err != nil {
		return err
	}
	for off := 0; off < len(image); off += OTAChunk {
		end := min(off+OTAChunk, len(image))
		if err := d.OTAData(ctx, uint32(off), image[off:end]); err != nil {
			return err
		}
	}
	return d.OTAEnd(ctx)
}

// otaTotal converts an image length to BEGIN's u32 total. An image past the
// wire field is refused here, not truncated: a wrapped total would announce
// a different image than the one that follows.
func otaTotal(n int64) (uint32, error) {
	if n < 0 || n > math.MaxUint32 {
		return 0, &ArgRangeError{
			Cmd: CmdOTABegin, Arg: "image", Value: n,
			Min: 0, Max: math.MaxUint32, Unit: "bytes",
		}
	}
	return uint32(n), nil
}
