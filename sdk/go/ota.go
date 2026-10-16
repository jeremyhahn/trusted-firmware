// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"encoding/binary"
	"fmt"
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
	if err := d.OTABegin(ctx, uint32(len(image))); err != nil {
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
