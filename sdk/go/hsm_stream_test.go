// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// hsmReassemblyLink answers HSM_LOAD the way the M7 does: it applies
// tfw_hsm_cmd_load's reassembly rules (src/core/hsm_cmd.c) to each chunk the
// SDK sends, so a chunk stream the device would refuse fails on the host.
type hsmReassemblyLink struct {
	captureLink
	total, have uint16
	got         []byte
	wholes      int
	refused     int
}

func (l *hsmReassemblyLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.status, l.payload = StatusOK, hsmReply()
	if !l.take(f.Payload) {
		l.status, l.payload = StatusErrRange, nil
	}
	return l.captureLink.Write(p)
}

// take applies one chunk, [total:2 LE][offset:2 LE][bytes], and reports
// whether the device keeps it.
func (l *hsmReassemblyLink) take(p []byte) bool {
	if len(p) < 4 {
		return l.refuse()
	}
	t := binary.LittleEndian.Uint16(p[0:2])
	offset := binary.LittleEndian.Uint16(p[2:4])
	data := p[4:]
	if offset == 0 {
		// hsm_cmd.c line 144: offset 0 begins an envelope of 1..TFW_SUIT_INTEGRATED_MAX.
		if t == 0 || t > HSMEnvelopeMax {
			return l.refuse()
		}
		// hsm_cmd.c lines 148-149: a new envelope drops what was held.
		l.total, l.have, l.got = t, 0, nil
	} else if l.total == 0 || t != l.total || offset != l.have {
		// hsm_cmd.c line 150: a later chunk continues the pending envelope in order.
		return l.refuse()
	}
	// hsm_cmd.c line 153: no chunk runs past the total.
	if len(data) > int(l.total-l.have) {
		return l.refuse()
	}
	// hsm_cmd.c lines 156-159: the bytes are appended.
	l.got = append(l.got, data...)
	l.have += uint16(len(data))
	// hsm_cmd.c lines 160-165: the chunk that completes the envelope hands it
	// to the engine and resets the pending state.
	if l.have == l.total {
		l.wholes++
		l.total, l.have = 0, 0
	}
	return true
}

// refuse is hsm_cmd.c's drop(TFW_ERR_RANGE): the pending state is reset.
func (l *hsmReassemblyLink) refuse() bool {
	l.total, l.have = 0, 0
	l.refused++
	return false
}

// hsmLoadChunkFrame builds an HSM_LOAD request carrying one chunk.
func hsmLoadChunkFrame(t *testing.T, seq Seq, total, offset uint16, data []byte) []byte {
	t.Helper()
	p := make([]byte, 4, 4+len(data))
	binary.LittleEndian.PutUint16(p[0:2], total)
	binary.LittleEndian.PutUint16(p[2:4], offset)
	b, err := EncodeRequest(seq, CmdHSMLoad, append(p, data...))
	require.NoError(t, err)
	return b
}

// hsmLoadAnswer sends one chunk to the link and decodes the status it answers.
func hsmLoadAnswer(t *testing.T, l *hsmReassemblyLink, total, offset uint16, data []byte) Status {
	t.Helper()
	l.rx.Reset()
	_, err := l.Write(hsmLoadChunkFrame(t, 1, total, offset, data))
	require.NoError(t, err)
	f, _, err := Decode(l.rx.Bytes())
	require.NoError(t, err)
	assert.Equal(t, CmdHSMLoad, f.Cmd())
	return f.Status
}

// The model is not vacuous: each refusal tfw_hsm_cmd_load makes is made here.
func TestHSMReassemblyLinkRefusesWhatTheDeviceRefuses(t *testing.T) {
	chunk := bytes.Repeat([]byte{0xA5}, HSMLoadChunk)

	t.Run("an empty chunk after a whole envelope", func(t *testing.T) {
		l := &hsmReassemblyLink{}
		require.Equal(t, StatusOK, hsmLoadAnswer(t, l, HSMLoadChunk, 0, chunk))
		require.Equal(t, 1, l.wholes)
		assert.Equal(t, StatusErrRange, hsmLoadAnswer(t, l, HSMLoadChunk, HSMLoadChunk, nil))
		assert.Equal(t, 1, l.refused)
		assert.Equal(t, 1, l.wholes)
	})
	t.Run("a second chunk at the wrong offset", func(t *testing.T) {
		l := &hsmReassemblyLink{}
		require.Equal(t, StatusOK, hsmLoadAnswer(t, l, 2*HSMLoadChunk, 0, chunk))
		assert.Equal(t, StatusErrRange, hsmLoadAnswer(t, l, 2*HSMLoadChunk, HSMLoadChunk+1, chunk[1:]))
		assert.Equal(t, 1, l.refused)
		assert.Zero(t, l.wholes)
	})
	t.Run("a second chunk with a different total", func(t *testing.T) {
		l := &hsmReassemblyLink{}
		require.Equal(t, StatusOK, hsmLoadAnswer(t, l, 2*HSMLoadChunk, 0, chunk))
		assert.Equal(t, StatusErrRange, hsmLoadAnswer(t, l, 2*HSMLoadChunk+1, HSMLoadChunk, chunk))
		assert.Equal(t, 1, l.refused)
		assert.Zero(t, l.wholes)
	})
	for _, total := range []uint16{0, HSMEnvelopeMax + 1} {
		l := &hsmReassemblyLink{}
		assert.Equal(t, StatusErrRange, hsmLoadAnswer(t, l, total, 0, chunk[:1]), "total %d", total)
		assert.Equal(t, 1, l.refused, "total %d", total)
	}
	t.Run("a chunk past the total", func(t *testing.T) {
		l := &hsmReassemblyLink{}
		assert.Equal(t, StatusErrRange, hsmLoadAnswer(t, l, HSMLoadChunk-1, 0, chunk))
		assert.Equal(t, 1, l.refused)
		assert.Zero(t, l.wholes)
		assert.Empty(t, l.got)
	})
}

// Every envelope length HSMLoad accepts is cut into a stream the M7 takes
// whole: no chunk is refused, the last one completes the envelope, and no
// chunk follows it (an empty chunk after the whole is TFW_ERR_RANGE).
func TestHSMLoadStreamIsTakenWholeByTheDevicesReassemblyAtEveryLength(t *testing.T) {
	for n := 1; n <= HSMEnvelopeMax; n++ {
		env := make([]byte, n)
		for i := range env {
			env[i] = byte(i*7 + n)
		}
		l := &hsmReassemblyLink{}
		_, err := New(l).HSMLoad(context.Background(), env)
		if err != nil {
			t.Fatalf("n=%d: HSMLoad: %v", n, err)
		}
		if l.refused != 0 || l.wholes != 1 || !bytes.Equal(l.got, env) {
			t.Fatalf("n=%d: refused %d, wholes %d, envelope taken intact %v",
				n, l.refused, l.wholes, bytes.Equal(l.got, env))
		}
		if want := (n + HSMLoadChunk - 1) / HSMLoadChunk; len(l.sent) != want {
			t.Fatalf("n=%d: %d frames sent, want %d", n, len(l.sent), want)
		}
		for i, f := range l.sent {
			if len(f.Payload) > MaxPayload {
				t.Fatalf("n=%d: frame %d payload %d > MaxPayload", n, i, len(f.Payload))
			}
			if i < len(l.sent)-1 && len(f.Payload) != 4+HSMLoadChunk {
				t.Fatalf("n=%d: frame %d carries %d bytes, want %d", n, i, len(f.Payload)-4, HSMLoadChunk)
			}
		}
	}
}

// hsmRecordEntryWithCount is a 10-byte record entry whose count byte is count.
func hsmRecordEntryWithCount(count, at byte) []byte {
	return []byte{count, 0, 0, at, 0, 0, 0, 1, at, 0}
}

func TestHSMRecordReadAllFollowsARecordThatGrowsWhileRead(t *testing.T) {
	link := &recordLink{entries: [][]byte{
		hsmRecordEntryWithCount(2, 1),
		hsmRecordEntryWithCount(3, 2), // an entry was appended after the first read
		hsmRecordEntryWithCount(3, 3),
	}}
	got, err := New(link).HSMRecordReadAll(context.Background())
	require.NoError(t, err)
	require.Len(t, got, 3)
	assert.Equal(t, uint32(3), got[2].At)
	require.Len(t, link.sent, 3)
	for i, f := range link.sent {
		assert.Equal(t, []byte{byte(i)}, f.Payload, "read %d", i)
	}
}

func TestHSMRecordReadAllStopsWhenTheCountFallsBelowTheIndexRead(t *testing.T) {
	link := &recordLink{entries: [][]byte{
		hsmRecordEntryWithCount(3, 1),
		hsmRecordEntryWithCount(1, 2), // the record shrank under the read
		hsmRecordEntryWithCount(3, 3),
	}}
	got, err := New(link).HSMRecordReadAll(context.Background())
	require.NoError(t, err)
	require.Len(t, got, 2)
	assert.Len(t, link.sent, 2)
}

func TestHSMRecordReadAllReturnsAShortReplyWithTheEntriesBeforeIt(t *testing.T) {
	link := &recordLink{entries: [][]byte{
		hsmRecordEntryWithCount(3, 1),
		hsmRecordEntryWithCount(3, 2)[:HSMRecordLen-1],
	}}
	got, err := New(link).HSMRecordReadAll(context.Background())
	var short *ShortReplyError
	require.True(t, errors.As(err, &short), "err = %v", err)
	assert.Equal(t, CmdHSMRecordRead, short.Cmd)
	assert.Equal(t, HSMRecordLen-1, short.Got)
	assert.Equal(t, HSMRecordLen, short.Need)
	require.Len(t, got, 1)
	assert.Equal(t, uint32(1), got[0].At)
}
