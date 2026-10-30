// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"encoding/binary"
	"errors"
)

// The HSM self-defence channel -- docs/hsm-self-defence/. The A55's half of
// the channel to the M7's response engine: the heartbeat, a reported signal,
// the verifier's verdict, the policy to apply and the signed policy set or
// script. Answered only by an image built with the channel; any other answers
// StatusErrUnknownCmd.
//
// The SDK carries the envelope and never reads it: building and signing a
// policy set is the tenant's, through go-xkms, and verifying it the M7's.
const (
	CmdHSMHeartbeat Cmd = 0x76 // - -> state
	CmdHSMReport    Cmd = 0x77 // [signal:1] -> state
	CmdHSMVerdict   Cmd = 0x78 // [verdict:1] -> state
	CmdHSMSelect    Cmd = 0x79 // [index:1] -> state
	CmdHSMLoad      Cmd = 0x7A // [total:2][offset:2][bytes] -> state

	// CmdHSMRecordRead is the A6 record read back, one entry per call.
	CmdHSMRecordRead Cmd = 0x7C // [index:1] -> [count:1][dropped:2][at:4][kind:1][what:1][result:1]
)

// The signals the A55 may report (architecture.md section 1). S1-S3 are the
// M7's own sensors and S6 its own timer; the device refuses them here with
// StatusErrRange, and so does the SDK, before the wire.
const (
	HSMSignalIMAViolation byte = 4 // S4: an IMA appraisal failure
	HSMSignalHotplug      byte = 5 // S5: an unexpected device on a bus
)

// The verifier's verdict V.
const (
	HSMVerdictClear       byte = 0
	HSMVerdictCompromised byte = 1
)

// HSMStateLen is the reply every HSM command carries when it is not refused.
const HSMStateLen = 6

// HSMLoadChunk is the most envelope bytes one HSM_LOAD carries: the payload
// less its total and offset.
const HSMLoadChunk = 60

// HSMEnvelopeMax is the largest envelope the device takes
// (TFW_SUIT_INTEGRATED_MAX); a larger one is refused with StatusErrRange.
const HSMEnvelopeMax = 1024

// HSM state flags.
const (
	HSMSetHeld uint8 = 1 << 0 // a verified policy set is held
	HSMLocked  uint8 = 1 << 1 // the device keys are locked (lockdown)
	HSMWiped   uint8 = 1 << 2 // the device keys are wiped
)

// HSMState is the engine's state after a command:
// [flags:1][selected:1][refused:4]. Refused counts the envelopes refused since
// the M7 last reset.
type HSMState struct {
	Flags    uint8
	Selected uint8
	Refused  uint32
}

func (d *Device) hsm(ctx context.Context, cmd Cmd, payload []byte) (*HSMState, error) {
	f, err := d.Do(ctx, cmd, payload)
	if err != nil {
		return nil, err
	}
	p := f.Payload
	if len(p) < HSMStateLen {
		return nil, &ShortReplyError{Cmd: cmd, Got: len(p), Need: HSMStateLen}
	}
	return &HSMState{Flags: p[0], Selected: p[1], Refused: binary.LittleEndian.Uint32(p[2:6])}, nil
}

// HSMHeartbeat tells the engine the A55 is alive. One must arrive within the
// set's heartbeat period, or the next message finds S6 raised.
func (d *Device) HSMHeartbeat(ctx context.Context) (*HSMState, error) {
	return d.hsm(ctx, CmdHSMHeartbeat, nil)
}

// HSMReport reports S4 or S5.
func (d *Device) HSMReport(ctx context.Context, signal byte) (*HSMState, error) {
	if signal != HSMSignalIMAViolation && signal != HSMSignalHotplug {
		return nil, &ArgRangeError{
			Cmd: CmdHSMReport, Arg: "signal", Value: int64(signal),
			Min: int64(HSMSignalIMAViolation), Max: int64(HSMSignalHotplug),
		}
	}
	return d.hsm(ctx, CmdHSMReport, []byte{signal})
}

// HSMVerdict delivers the verifier's verdict.
func (d *Device) HSMVerdict(ctx context.Context, verdict byte) (*HSMState, error) {
	if verdict > HSMVerdictCompromised {
		return nil, &ArgRangeError{
			Cmd: CmdHSMVerdict, Arg: "verdict", Value: int64(verdict),
			Min: 0, Max: int64(HSMVerdictCompromised),
		}
	}
	return d.hsm(ctx, CmdHSMVerdict, []byte{verdict})
}

// HSMSelect names the policy of the held set to apply. An index past the set,
// or no set held, is StatusErrRange.
func (d *Device) HSMSelect(ctx context.Context, index byte) (*HSMState, error) {
	return d.hsm(ctx, CmdHSMSelect, []byte{index})
}

// HSMLoad sends a signed policy set or script envelope in HSMLoadChunk pieces
// and returns the state after the last. It stops at the first refusal and
// returns it: the device's verdict on the envelope comes with the last chunk
// (StatusErrCrypto, StatusErrUnsupported, StatusErrProtectedRegion, ...).
func (d *Device) HSMLoad(ctx context.Context, envelope []byte) (*HSMState, error) {
	n := len(envelope)
	if n < 1 || n > HSMEnvelopeMax {
		return nil, &ArgRangeError{
			Cmd: CmdHSMLoad, Arg: "length", Value: int64(n),
			Min: 1, Max: HSMEnvelopeMax, Unit: "bytes",
		}
	}
	total := uint16(n)
	var st *HSMState
	for off := 0; off < len(envelope); off += HSMLoadChunk {
		end := min(off+HSMLoadChunk, len(envelope))
		p := make([]byte, 4, 4+end-off)
		binary.LittleEndian.PutUint16(p[0:2], total)
		binary.LittleEndian.PutUint16(p[2:4], uint16(off))
		s, err := d.hsm(ctx, CmdHSMLoad, append(p, envelope[off:end]...))
		if err != nil {
			return nil, err
		}
		st = s
	}
	return st, nil
}

// HSMRecordLen is the reply of HSM_RECORD_READ: the count and the dropped
// count, then the entry.
const HSMRecordLen = 10

// The kinds of an A6 record entry.
const (
	HSMRecordSignal   byte = 1 // a signal the engine saw
	HSMRecordDecision byte = 2 // a decision the engine took
	HSMRecordAction   byte = 3 // an action the engine ran
)

// HSMRecordEntry is one entry of the A6 record, with the record's own totals.
// Count is how many entries the record holds; Dropped how many were refused
// because it was full (capped at 0xFFFF). At is the engine's clock when the
// entry was made. What is the signal, the verdict V or the action by Kind;
// Result is an action's status, and StatusOK for the other kinds.
type HSMRecordEntry struct {
	Count   uint8
	Dropped uint16
	At      uint32
	Kind    uint8
	What    uint8
	Result  uint8
}

// HSMRecordRead reads one entry of the A6 record. An index past the end of the
// record, which includes any index of an empty one, is StatusErrRange. There
// is no call that erases the record.
func (d *Device) HSMRecordRead(ctx context.Context, index byte) (*HSMRecordEntry, error) {
	f, err := d.Do(ctx, CmdHSMRecordRead, []byte{index})
	if err != nil {
		return nil, err
	}
	p := f.Payload
	if len(p) < HSMRecordLen {
		return nil, &ShortReplyError{Cmd: CmdHSMRecordRead, Got: len(p), Need: HSMRecordLen}
	}
	return &HSMRecordEntry{
		Count:   p[0],
		Dropped: binary.LittleEndian.Uint16(p[1:3]),
		At:      binary.LittleEndian.Uint32(p[3:7]),
		Kind:    p[7],
		What:    p[8],
		Result:  p[9],
	}, nil
}

// HSMRecordReadAll reads the A6 record from its first entry to its last. It
// stops at the first StatusErrRange, which is the end; an empty record is no
// entries and no error. Any other refusal is returned with the entries read
// before it.
func (d *Device) HSMRecordReadAll(ctx context.Context) ([]HSMRecordEntry, error) {
	var out []HSMRecordEntry
	for i := 0; i <= 0xFF; i++ {
		e, err := d.HSMRecordRead(ctx, byte(i))
		if errors.Is(err, StatusErrRange) {
			return out, nil
		}
		if err != nil {
			return out, err
		}
		out = append(out, *e)
		if int(e.Count) <= i+1 {
			return out, nil
		}
	}
	return out, nil
}
