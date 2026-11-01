package tfw

import (
	"bytes"
	"context"
	"errors"
	"io"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
)

// TestPeripheralRefusals covers what each peripheral command does with a
// refused request and a reply too short for its layout. The refusal must
// come back as the device's status, and a short reply as a ShortReplyError
// naming the command, never as a zero value read past the end.
func TestPeripheralRefusals(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name  string
		cmd   Cmd
		short int // a reply this long is too short; -1 when any length is accepted
		call  func(*Device) error
	}{
		{"ping", CmdPing, -1, func(d *Device) error { _, err := d.Ping(ctx, []byte{1}); return err }},
		{"digital read", CmdDigitalRead, 0, func(d *Device) error { _, err := d.DigitalRead(ctx, 13); return err }},
		{"timer set", CmdTimerSet, 0, func(d *Device) error { _, err := d.TimerSet(ctx, 13, true, time.Second); return err }},
		{"timer query", CmdTimerQuery, timerQueryLen - 1, func(d *Device) error { _, err := d.TimerQuery(ctx, 13); return err }},
		{"analog read", CmdAnalogRead, 1, func(d *Device) error { _, err := d.AnalogRead(ctx, 0); return err }},
		{"eeprom read", CmdEEPROMRead, -1, func(d *Device) error { _, err := d.EEPROMRead(ctx, EEPROMReservedEnd, 4); return err }},
		{"pulse in", CmdPulseIn, 3, func(d *Device) error { _, err := d.PulseIn(ctx, 2, true, time.Millisecond); return err }},
		{"i2c transfer", CmdI2CXfer, -1, func(d *Device) error { _, err := d.I2CTransfer(ctx, 0x50, []byte{0}, 1); return err }},
		{"spi transfer", CmdSPIXfer, -1, func(d *Device) error { _, err := d.SPITransfer(ctx, 10, []byte{0}); return err }},
		{"one-wire reset", CmdOneWireReset, 0, func(d *Device) error { _, err := d.OneWireReset(ctx, 4); return err }},
		{"one-wire read", CmdOneWireRead, -1, func(d *Device) error { _, err := d.OneWireRead(ctx, 4, 8); return err }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := tc.call(New(&cannedLink{status: StatusErrBusy}))
			require.Equal(t, StatusErrBusy, asStatus(t, err))
			if tc.short >= 0 {
				err = tc.call(New(&cannedLink{payload: make([]byte, tc.short)}))
				requireShortReply(t, err, tc.cmd)
			}
		})
	}
}

func TestPeripheralReplies(t *testing.T) {
	ctx := context.Background()

	echo, err := New(&cannedLink{payload: []byte{1, 2, 3}}).Ping(ctx, []byte{1, 2, 3})
	require.NoError(t, err)
	require.Equal(t, []byte{1, 2, 3}, echo)

	high, err := New(&cannedLink{payload: []byte{1}}).DigitalRead(ctx, 13)
	require.NoError(t, err)
	require.True(t, high)

	slot, err := New(&cannedLink{payload: []byte{2}}).TimerSet(ctx, 13, false, time.Second)
	require.NoError(t, err)
	require.Equal(t, uint8(2), slot)

	// Active, held high, 1500 ms left, little-endian.
	ts, err := New(&cannedLink{payload: []byte{1, 1, 0xDC, 0x05, 0, 0}}).TimerQuery(ctx, 13)
	require.NoError(t, err)
	require.Equal(t, &TimerState{Active: true, HeldLevel: true, Remaining: 1500 * time.Millisecond}, ts)

	count, err := New(&cannedLink{payload: []byte{0xFF, 0x03}}).AnalogRead(ctx, 0)
	require.NoError(t, err)
	require.Equal(t, uint16(1023), count)

	width, err := New(&cannedLink{payload: []byte{0xE8, 0x03, 0, 0}}).PulseIn(ctx, 2, false, time.Millisecond)
	require.NoError(t, err)
	require.Equal(t, time.Millisecond, width)

	present, err := New(&cannedLink{payload: []byte{0}}).OneWireReset(ctx, 4)
	require.NoError(t, err)
	require.False(t, present)

	nonce, err := New(&cannedLink{payload: bytes.Repeat([]byte{9}, AuthNonceLen)}).AuthChallenge(ctx)
	require.NoError(t, err)
	require.Equal(t, bytes.Repeat([]byte{9}, AuthNonceLen), nonce)

	require.NoError(t, New(&cannedLink{}).ConfigureSPI(ctx, SPIMode3, SPIClockDiv128, true))
}

func TestOTAInfoRefusal(t *testing.T) {
	_, err := New(&cannedLink{status: StatusErrBadState}).OTAInfo(context.Background(), 0)
	require.Equal(t, StatusErrBadState, asStatus(t, err))
}

func TestAttestLaterPageRefusals(t *testing.T) {
	ctx := context.Background()
	// Page 0 as the device sends it, so both cases reach the second request.
	first := append([]byte{0, EvidenceLen}, make([]byte, attestPage)...)
	_, err := New(&scriptedLink{replies: []cannedLink{{payload: first}, {status: StatusErrRange}}}).
		Attest(ctx, make([]byte, AttestNonceLen))
	require.Equal(t, StatusErrRange, asStatus(t, err))
	_, err = New(&scriptedLink{replies: []cannedLink{{payload: first}, {payload: make([]byte, attestHdr)}}}).
		Attest(ctx, make([]byte, AttestNonceLen))
	requireShortReply(t, err, CmdAttest)
}

// chunkLink hands each prepared chunk to its own Read, so a test controls
// what the device has buffered when it decodes. before is put on the line
// ahead of the real reply.
type chunkLink struct {
	before func(seq Seq, cmd Cmd) ([]byte, error)
	reads  [][]byte
}

func (l *chunkLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	pre, err := l.before(f.Seq, f.Cmd())
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, []byte{7})
	if err != nil {
		return 0, err
	}
	l.reads = append(l.reads, pre, resp)
	return len(p), nil
}

func (l *chunkLink) Read(p []byte) (int, error) {
	if len(l.reads) == 0 {
		return 0, io.EOF
	}
	n := copy(p, l.reads[0])
	l.reads = l.reads[1:]
	return n, nil
}

func TestDeviceReplyMatching(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name   string
		before func(Seq, Cmd) ([]byte, error)
	}{
		// A late reply to an earlier request is dropped, not returned.
		{"stale sequence", func(seq Seq, cmd Cmd) ([]byte, error) {
			return EncodeResponse(seq+1, cmd, StatusOK, []byte{9})
		}},
		// A bad length field with no later start byte: nothing in it can
		// begin a frame, so the whole buffer goes.
		{"garbage with no start byte", func(Seq, Cmd) ([]byte, error) {
			return []byte{SOF, 0x00, 0x00, 0x00, 0x00, 0x00}, nil
		}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, err := New(&chunkLink{before: tc.before}).Ping(ctx, nil)
			require.NoError(t, err)
			require.Equal(t, []byte{7}, got)
		})
	}
}

func TestDeviceRequestRefusals(t *testing.T) {
	// A payload past MaxPayload never reaches the link.
	link := &cannedLink{}
	_, err := New(link).Ping(context.Background(), make([]byte, MaxPayload+1))
	var pe *PayloadSizeError
	require.True(t, errors.As(err, &pe), "error %v is not a PayloadSizeError", err)
	require.Zero(t, link.rx.Len())

	// A cancelled context ends the wait before any reply is read.
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	_, err = New(&cannedLink{}).Ping(ctx, nil)
	require.ErrorIs(t, err, context.Canceled)
}

func TestDeviceSequenceSkipsZero(t *testing.T) {
	d := New(&cannedLink{})
	var last Seq
	last--
	d.seq = last
	_, err := d.Ping(context.Background(), nil)
	require.NoError(t, err)
	require.Equal(t, Seq(1), d.seq, "the sequence wrapped onto SeqNone")
}
