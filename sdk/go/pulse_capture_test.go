package tfw

import (
	"context"
	"encoding/binary"
	"errors"
	"math"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The contract under test is docs/device-capabilities/api.md "Pulse":
//
//	PULSE_CAPTURE  req  pin:u8  start_level:u8(0|1)  max_edges:u8  timeout_us:u32
//	               resp  n:u8  interval_us:u16 * n
//
// with max_edges bounded by TFW_CAPTURE_MAX = (TFW_MAX_PAYLOAD - 1) / 2.
const captureMax = (MaxPayload - 1) / 2

// captureLink answers every request with a canned reply and keeps the request
// it was sent, so a test can read back the wire form the SDK built.
type captureLink struct {
	cannedLink
	sent []Frame
}

func (l *captureLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.sent = append(l.sent, f)
	return l.cannedLink.Write(p)
}

// intervals builds the documented reply: a count byte, then that many
// little-endian u16 intervals.
func intervals(us ...uint16) []byte {
	p := []byte{byte(len(us))}
	for _, v := range us {
		p = binary.LittleEndian.AppendUint16(p, v)
	}
	return p
}

func TestPulseCaptureRefusesATimeoutOutsideTheWireFieldBeforeSending(t *testing.T) {
	for _, tc := range []struct {
		name    string
		timeout time.Duration
	}{
		{"one microsecond below zero", -time.Microsecond},
		{"an hour below zero", -time.Hour},
		{"one microsecond past u32", (math.MaxUint32 + 1) * time.Microsecond},
		{"the largest duration", time.Duration(math.MaxInt64)},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{}
			_, err := New(link).PulseCapture(context.Background(), 2, true, 8, tc.timeout)
			var re *ArgRangeError
			require.True(t, errors.As(err, &re), "%v", err)
			assert.Equal(t, CmdPulseCapture, re.Cmd)
			assert.Equal(t, "timeout", re.Arg)
			assert.Equal(t, "us", re.Unit)
			assert.Empty(t, link.sent, "a refused argument must never reach the wire")
		})
	}
}

func TestPulseCaptureEncodesTheDocumentedRequest(t *testing.T) {
	for _, tc := range []struct {
		name      string
		pin       uint8
		startHigh bool
		maxEdges  uint8
		timeout   time.Duration
		want      []byte
	}{
		{"start high", 7, true, 40, 5 * time.Millisecond, []byte{7, 1, 40, 0x88, 0x13, 0, 0}},
		{"start low", 3, false, 1, 50 * time.Millisecond, []byte{3, 0, 1, 0x50, 0xC3, 0, 0}},
		{"zero timeout", 0, false, captureMax, 0, []byte{0, 0, captureMax, 0, 0, 0, 0}},
		{"u32 ceiling", 255, true, 255, math.MaxUint32 * time.Microsecond, []byte{255, 1, 255, 0xFF, 0xFF, 0xFF, 0xFF}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{cannedLink: cannedLink{payload: intervals()}}
			_, err := New(link).PulseCapture(context.Background(), tc.pin, tc.startHigh, tc.maxEdges, tc.timeout)
			require.NoError(t, err)
			require.Len(t, link.sent, 1)
			assert.Equal(t, CmdPulseCapture, link.sent[0].Cmd())
			assert.Equal(t, tc.want, link.sent[0].Payload)
		})
	}
}

func TestPulseCaptureReturnsEachDocumentedResultShape(t *testing.T) {
	full := make([]uint16, captureMax)
	for i := range full {
		full[i] = uint16(26 + 44*(i%2)) // a DHT22's 0-bit and 1-bit
	}
	for _, tc := range []struct {
		name string
		want []uint16
	}{
		{"no edges", []uint16{}},
		{"one interval", []uint16{26}},
		{"the extremes of u16", []uint16{0, 0xFFFF, 0x1234}},
		{"TFW_CAPTURE_MAX intervals", full},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{cannedLink: cannedLink{payload: intervals(tc.want...)}}
			got, err := New(link).PulseCapture(context.Background(), 2, true, captureMax, time.Millisecond)
			require.NoError(t, err)
			assert.Equal(t, tc.want, got)
		})
	}
}

func TestPulseCaptureRejectsAShortReply(t *testing.T) {
	for _, tc := range []struct {
		name    string
		payload []byte
		need    int
	}{
		{"empty", []byte{}, 1},
		{"a count with no intervals", []byte{1}, 3},
		{"half an interval", []byte{1, 0x1A}, 3},
		{"one interval short", intervals(26, 70, 26)[:5], 7},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{cannedLink: cannedLink{payload: tc.payload}}
			_, err := New(link).PulseCapture(context.Background(), 2, true, 8, time.Millisecond)
			var se *ShortReplyError
			require.True(t, errors.As(err, &se), "%v", err)
			assert.Equal(t, CmdPulseCapture, se.Cmd)
			assert.Equal(t, len(tc.payload), se.Got)
			assert.Equal(t, tc.need, se.Need)
		})
	}
}

// A refusal reaches the caller as the status the device sent: TIMEOUT when no
// qualifying edge came (engineering.md), RANGE for a timeout past
// TFW_PULSE_MAX_US or max_edges past TFW_CAPTURE_MAX, BAD_PIN and UNSUPPORTED
// for a pin or a board without the feature.
func TestPulseCaptureSurfacesTheDeviceRefusal(t *testing.T) {
	for _, st := range []Status{StatusErrTimeout, StatusErrRange, StatusErrBadPin, StatusErrUnsupported, StatusErrReservedPin} {
		t.Run(st.String(), func(t *testing.T) {
			link := &captureLink{cannedLink: cannedLink{status: st}}
			got, err := New(link).PulseCapture(context.Background(), 2, true, 8, time.Millisecond)
			assert.Nil(t, got)
			assert.Equal(t, st, err)
		})
	}
}
