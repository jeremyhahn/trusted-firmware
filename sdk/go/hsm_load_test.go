package tfw

import (
	"context"
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The largest envelope the device holds is sent, not refused: 17 full chunks
// and one of 4 bytes, each carrying total 1024 and an offset past 255, so
// both high bytes are on the wire.
func TestHSMLoadSendsAnEnvelopeOfExactlyHSMEnvelopeMax(t *testing.T) {
	env := make([]byte, HSMEnvelopeMax)
	for i := range env {
		env[i] = byte(i)
	}
	link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
	_, err := New(link).HSMLoad(context.Background(), env)
	require.NoError(t, err)
	require.Len(t, link.sent, 18)
	var got []byte
	for i, f := range link.sent {
		assert.Equal(t, CmdHSMLoad, f.Cmd())
		assert.Equal(t, []byte{0x00, 0x04}, f.Payload[0:2], "total, frame %d", i)
		off := binary.LittleEndian.AppendUint16(nil, uint16(i*HSMLoadChunk))
		assert.Equal(t, off, f.Payload[2:4], "offset, frame %d", i)
		got = append(got, f.Payload[4:]...)
	}
	assert.Equal(t, []byte{0x2C, 0x01}, link.sent[5].Payload[2:4], "offset 300")
	assert.Equal(t, []byte{0xFC, 0x03}, link.sent[17].Payload[2:4], "offset 1020")
	assert.Len(t, link.sent[17].Payload, 4+4)
	assert.Equal(t, env, got)
}
