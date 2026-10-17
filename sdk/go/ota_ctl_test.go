package tfw

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func infoReply() []byte {
	p := []byte{2, 0, 0x05, 7, 0, 0, 0, 4, 0x2A, 0, 0, 0}
	return append(p, bytes.Repeat([]byte{0xAB}, 32)...)
}

func TestOTAInfoDecodesTheReply(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{payload: infoReply()}}
	si, err := New(link).OTAInfo(context.Background(), 1)
	require.NoError(t, err)
	require.Len(t, link.sent, 1)
	assert.Equal(t, CmdOTAInfo, link.sent[0].Cmd())
	assert.Equal(t, []byte{1}, link.sent[0].Payload)
	assert.Equal(t, uint8(2), si.Slots)
	assert.Equal(t, uint8(0), si.Active)
	assert.Equal(t, SlotFellBack|SlotTrial, si.Flags)
	assert.Equal(t, uint32(7), si.Floor)
	assert.Equal(t, uint8(4), si.State)
	assert.Equal(t, uint32(42), si.ImageVersion)
	assert.Equal(t, bytes.Repeat([]byte{0xAB}, 32), si.Measurement[:])
}

func TestOTAInfoRejectsAShortReply(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{payload: infoReply()[:43]}}
	_, err := New(link).OTAInfo(context.Background(), 0)
	var se *ShortReplyError
	require.True(t, errors.As(err, &se), "%v", err)
	assert.Equal(t, CmdOTAInfo, se.Cmd)
	assert.Equal(t, 43, se.Got)
	assert.Equal(t, OTAInfoLen, se.Need)
}

func TestOTAActivateSendsTheMode(t *testing.T) {
	for _, mode := range []byte{OTAActivateAtReset, OTAActivateResetNow} {
		link := &captureLink{}
		require.NoError(t, New(link).OTAActivate(context.Background(), mode))
		require.Len(t, link.sent, 1)
		assert.Equal(t, CmdOTAActivate, link.sent[0].Cmd())
		assert.Equal(t, []byte{mode}, link.sent[0].Payload)
	}
}

func TestOTAAuthSendsTheSignature(t *testing.T) {
	var sig [64]byte
	for i := range sig {
		sig[i] = byte(i)
	}
	link := &captureLink{}
	require.NoError(t, New(link).OTAAuth(context.Background(), sig))
	require.Len(t, link.sent, 1)
	assert.Equal(t, CmdOTAAuth, link.sent[0].Cmd())
	assert.Equal(t, sig[:], link.sent[0].Payload)
}

func TestOTAActivateRefusesAnUnknownModeBeforeTheWire(t *testing.T) {
	link := &captureLink{}
	err := New(link).OTAActivate(context.Background(), 2)
	var re *ArgRangeError
	require.True(t, errors.As(err, &re), "%v", err)
	assert.Equal(t, CmdOTAActivate, re.Cmd)
	assert.Equal(t, "mode", re.Arg)
	assert.Empty(t, link.sent, "a refused argument must never reach the wire")
}

func TestOTAActivateAndConfirmCarryTheRefusal(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{status: StatusErrBadState}}
	assert.Equal(t, StatusErrBadState, New(link).OTAActivate(context.Background(), 0))
	link = &captureLink{cannedLink: cannedLink{status: StatusErrBadState}}
	assert.Equal(t, StatusErrBadState, New(link).OTAConfirm(context.Background()))
	require.Len(t, link.sent, 1)
	assert.Equal(t, CmdOTAConfirm, link.sent[0].Cmd())
	assert.Empty(t, link.sent[0].Payload)
}
