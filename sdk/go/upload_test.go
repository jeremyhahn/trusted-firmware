package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"testing"

	"github.com/stretchr/testify/require"
)

var errLineDown = errors.New("line down")

// otaSession is the far side of a link that keeps an OTA session the way
// src/core/ota_stage.c does: BEGIN opens it, OTA_DATA must land at the next
// offset, END needs every byte, OTA_STATUS answers the state and the count.
// The maps script failures by OTA_DATA offset, each spent once it fires.
type otaSession struct {
	state  OTAState
	total  uint32
	stored []byte
	// data is the offset of every OTA_DATA put on the line, payloads its
	// payload length; ends and statuses count OTA_END and OTA_STATUS.
	data     []uint32
	payloads []int
	ends     int
	statuses int
	// before loses the frame before the device sees it; after takes the bytes
	// and loses the reply; broken fails the device's store, failing the session.
	before, after, broken map[uint32]int
	// count rewrites OTA_STATUS's count; statusDown loses OTA_STATUS.
	count      func(uint32) uint32
	statusDown bool
	// onData runs as an OTA_DATA reaches the device.
	onData func(off uint32)
	rx     bytes.Buffer
}

func (s *otaSession) spend(m map[uint32]int, off uint32) bool {
	if m[off] == 0 {
		return false
	}
	m[off]--
	return true
}

func (s *otaSession) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	st, reply := StatusOK, []byte(nil)
	switch f.Cmd() {
	case CmdOTABegin:
		s.total = binary.LittleEndian.Uint32(f.Payload)
		s.state, s.stored = OTAReceiving, nil
	case CmdOTAData:
		off := binary.LittleEndian.Uint32(f.Payload)
		s.data = append(s.data, off)
		s.payloads = append(s.payloads, len(f.Payload))
		if s.spend(s.before, off) {
			return 0, errLineDown
		}
		if s.onData != nil {
			s.onData(off)
		}
		switch {
		case s.state != OTAReceiving:
			st = StatusErrBusy
		case off != uint32(len(s.stored)) || len(s.stored)+len(f.Payload)-4 > int(s.total):
			st = StatusErrRange
		case s.spend(s.broken, off):
			s.state, st = OTAFailed, StatusErrInternal
		default:
			s.stored = append(s.stored, f.Payload[4:]...)
			if s.spend(s.after, off) {
				return 0, errLineDown
			}
		}
	case CmdOTAEnd:
		s.ends++
		if uint32(len(s.stored)) != s.total {
			st = StatusErrFraming
			break
		}
		s.state = OTAComplete
	case CmdOTAStatus:
		s.statuses++
		if s.statusDown {
			return 0, errLineDown
		}
		n := uint32(len(s.stored))
		if s.count != nil {
			n = s.count(n)
		}
		reply = binary.LittleEndian.AppendUint32([]byte{byte(s.state)}, n)
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), st, reply)
	if err != nil {
		return 0, err
	}
	s.rx.Write(resp)
	return len(p), nil
}

func (s *otaSession) Read(p []byte) (int, error) { return s.rx.Read(p) }

// uploadStream is n patterned bytes, so a byte landing at the wrong offset
// shows in the stored copy.
func uploadStream(n int) *Image {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i*7 + 3)
	}
	return &Image{Stream: b}
}

// chunkOffsets is every chunk edge of an n-byte stream, in order.
func chunkOffsets(n int) []uint32 {
	var offs []uint32
	for off := 0; off < n; off += OTAChunk {
		offs = append(offs, uint32(off))
	}
	return offs
}

func TestUploadWholeImage(t *testing.T) {
	img := uploadStream(5*OTAChunk + 7)
	s := &otaSession{}
	var seen []uint32
	err := New(s).Upload(context.Background(), img, UploadProgress(func(sent, total uint32) {
		require.Equal(t, uint32(len(img.Stream)), total)
		seen = append(seen, sent)
	}))
	require.NoError(t, err)
	require.Equal(t, OTAComplete, s.state)
	require.Equal(t, img.Stream, s.stored)
	require.Equal(t, chunkOffsets(len(img.Stream)), s.data, "each chunk once, in order")
	require.Equal(t, []uint32{60, 120, 180, 240, 300, 307}, seen)
	require.Equal(t, 1, s.ends)
}

func TestUploadChunksFitMaxPayload(t *testing.T) {
	img := uploadStream(3*OTAChunk + 1)
	s := &otaSession{}
	require.NoError(t, New(s).Upload(context.Background(), img))
	require.Equal(t, []int{MaxPayload, MaxPayload, MaxPayload, 4 + 1}, s.payloads,
		"offset and data fill TFW_MAX_PAYLOAD and never pass it")
}

func TestUploadResumesFromStatus(t *testing.T) {
	img := uploadStream(4 * OTAChunk)
	offs := chunkOffsets(len(img.Stream))
	cases := []struct {
		name string
		s    *otaSession
		want []uint32
	}{
		{
			name: "the device took the bytes and the reply was lost",
			s:    &otaSession{after: map[uint32]int{120: 1}},
			want: offs,
		},
		{
			name: "the frame was lost before the device saw it",
			s:    &otaSession{before: map[uint32]int{120: 1}},
			want: []uint32{0, 60, 120, 120, 180},
		},
		{
			name: "two failures in one session, each resumed",
			s:    &otaSession{after: map[uint32]int{0: 1}, before: map[uint32]int{180: 1}},
			want: []uint32{0, 60, 120, 180, 180},
		},
		{
			name: "a chunk lost before delivery, then its retry's reply lost",
			s:    &otaSession{before: map[uint32]int{120: 1}, after: map[uint32]int{120: 1}},
			want: []uint32{0, 60, 120, 120, 180},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			require.NoError(t, New(tc.s).Upload(context.Background(), img))
			require.Equal(t, img.Stream, tc.s.stored)
			require.Equal(t, tc.want, tc.s.data, "no accepted byte is sent again")
			require.Equal(t, OTAComplete, tc.s.state)
		})
	}
}

func TestUploadStopsWhenResumeCannot(t *testing.T) {
	img := uploadStream(4 * OTAChunk)
	cases := []struct {
		name string
		s    *otaSession
		want []error
		data []uint32
	}{
		{
			name: "the device failed the session",
			s:    &otaSession{broken: map[uint32]int{60: 1}},
			want: []error{StatusErrInternal},
			data: []uint32{0, 60},
		},
		{
			name: "OTA_STATUS is lost too",
			s:    &otaSession{before: map[uint32]int{60: 1}, statusDown: true},
			want: []error{errLineDown},
			data: []uint32{0, 60},
		},
		{
			name: "the same chunk fails again with no progress",
			s:    &otaSession{before: map[uint32]int{60: 2}},
			want: []error{errLineDown},
			data: []uint32{0, 60, 60},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			err := New(tc.s).Upload(context.Background(), img)
			for _, w := range tc.want {
				require.ErrorIs(t, err, w)
			}
			require.Equal(t, tc.data, tc.s.data)
			require.Zero(t, tc.s.ends, "no OTA_END after a refusal")
		})
	}
}

func TestUploadRefusesAStatusCountItDidNotSend(t *testing.T) {
	img := uploadStream(4 * OTAChunk)
	cases := []struct {
		name  string
		count func(uint32) uint32
	}{
		{"beyond what was sent", func(n uint32) uint32 { return n + OTAChunk }},
		{"not on a chunk edge", func(n uint32) uint32 { return n - 1 }},
		{"behind the chunk in flight", func(n uint32) uint32 { return n - 2*OTAChunk }},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			s := &otaSession{after: map[uint32]int{120: 1}, count: tc.count}
			err := New(s).Upload(context.Background(), img)
			require.ErrorIs(t, err, ErrUploadCount)
			require.Equal(t, []uint32{0, 60, 120}, s.data, "nothing sent after the count")
			require.Zero(t, s.ends)
		})
	}
}

func TestUploadStopsWhenCancelledBetweenChunks(t *testing.T) {
	img := uploadStream(4 * OTAChunk)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s := &otaSession{}
	err := New(s).Upload(ctx, img, UploadProgress(func(sent, _ uint32) {
		if sent == 2*OTAChunk {
			cancel()
		}
	}))
	require.ErrorIs(t, err, context.Canceled)
	require.Equal(t, []uint32{0, 60}, s.data, "no OTA_DATA after the cancel")
	require.Zero(t, s.ends)
}

func TestUploadDoesNotResumeACancelledChunk(t *testing.T) {
	img := uploadStream(4 * OTAChunk)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s := &otaSession{onData: func(off uint32) {
		if off == 60 {
			cancel()
		}
	}}
	err := New(s).Upload(ctx, img)
	require.ErrorIs(t, err, context.Canceled)
	require.Equal(t, []uint32{0, 60}, s.data, "no OTA_DATA after the cancel")
	require.Zero(t, s.statuses, "a cancel is not a lost chunk to resume")
}
