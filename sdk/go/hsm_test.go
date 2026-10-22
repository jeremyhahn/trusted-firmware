package tfw

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// hsmReply is a state: set held, policy 2 selected, 258 envelopes refused.
func hsmReply() []byte { return []byte{0x01, 0x02, 0x02, 0x01, 0x00, 0x00} }

// refuseAtLink answers like captureLink until the at-th frame (1-based), which
// it refuses with status.
type refuseAtLink struct {
	captureLink
	at     int
	status Status
}

func (l *refuseAtLink) Write(p []byte) (int, error) {
	if len(l.sent)+1 == l.at {
		l.cannedLink.status = l.status
	}
	return l.captureLink.Write(p)
}

func TestHSMHeartbeatSendsNothingAndDecodesTheState(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
	st, err := New(link).HSMHeartbeat(context.Background())
	require.NoError(t, err)
	require.Len(t, link.sent, 1)
	assert.Equal(t, CmdHSMHeartbeat, link.sent[0].Cmd())
	assert.Empty(t, link.sent[0].Payload)
	assert.Equal(t, &HSMState{Flags: HSMSetHeld, Selected: 2, Refused: 258}, st)
}

func TestHSMRejectsAShortReply(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{payload: hsmReply()[:5]}}
	_, err := New(link).HSMHeartbeat(context.Background())
	var se *ShortReplyError
	require.True(t, errors.As(err, &se), "%v", err)
	assert.Equal(t, CmdHSMHeartbeat, se.Cmd)
	assert.Equal(t, 5, se.Got)
	assert.Equal(t, HSMStateLen, se.Need)
}

func TestHSMReportSendsTheSignal(t *testing.T) {
	for _, sig := range []byte{HSMSignalIMAViolation, HSMSignalHotplug} {
		link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
		_, err := New(link).HSMReport(context.Background(), sig)
		require.NoError(t, err)
		require.Len(t, link.sent, 1)
		assert.Equal(t, CmdHSMReport, link.sent[0].Cmd())
		assert.Equal(t, []byte{sig}, link.sent[0].Payload)
	}
}

// S1-S3 and S6 are the M7's own; the SDK refuses them before the wire.
func TestHSMReportRefusesASignalTheA55DoesNotObserve(t *testing.T) {
	for _, sig := range []byte{0, 1, 2, 3, 6, 0xFF} {
		link := &captureLink{}
		_, err := New(link).HSMReport(context.Background(), sig)
		var ae *ArgRangeError
		require.True(t, errors.As(err, &ae), "signal %d: %v", sig, err)
		assert.Equal(t, CmdHSMReport, ae.Cmd)
		assert.Equal(t, int64(sig), ae.Value)
		assert.Empty(t, link.sent)
	}
}

func TestHSMVerdictSendsTheVerdict(t *testing.T) {
	for _, v := range []byte{HSMVerdictClear, HSMVerdictCompromised} {
		link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
		_, err := New(link).HSMVerdict(context.Background(), v)
		require.NoError(t, err)
		require.Len(t, link.sent, 1)
		assert.Equal(t, CmdHSMVerdict, link.sent[0].Cmd())
		assert.Equal(t, []byte{v}, link.sent[0].Payload)
	}
}

func TestHSMVerdictRefusesAnUnknownVerdict(t *testing.T) {
	link := &captureLink{}
	_, err := New(link).HSMVerdict(context.Background(), 2)
	var ae *ArgRangeError
	require.True(t, errors.As(err, &ae), "%v", err)
	assert.Equal(t, CmdHSMVerdict, ae.Cmd)
	assert.Empty(t, link.sent)
}

func TestHSMSelectSendsTheIndex(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
	_, err := New(link).HSMSelect(context.Background(), 2)
	require.NoError(t, err)
	require.Len(t, link.sent, 1)
	assert.Equal(t, CmdHSMSelect, link.sent[0].Cmd())
	assert.Equal(t, []byte{2}, link.sent[0].Payload)
}

func TestHSMSelectReturnsTheDevicesRefusal(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{status: StatusErrRange}}
	_, err := New(link).HSMSelect(context.Background(), 9)
	require.ErrorIs(t, err, StatusErrRange)
}

func TestHSMLoadSendsTheEnvelopeInChunks(t *testing.T) {
	env := make([]byte, 2*HSMLoadChunk+10)
	for i := range env {
		env[i] = byte(i)
	}
	link := &captureLink{cannedLink: cannedLink{payload: hsmReply()}}
	st, err := New(link).HSMLoad(context.Background(), env)
	require.NoError(t, err)
	assert.Equal(t, HSMSetHeld, st.Flags)
	require.Len(t, link.sent, 3)
	var got []byte
	for i, f := range link.sent {
		assert.Equal(t, CmdHSMLoad, f.Cmd())
		assert.Equal(t, []byte{byte(len(env)), 0}, f.Payload[0:2], "total, frame %d", i)
		assert.Equal(t, []byte{byte(i * HSMLoadChunk), 0}, f.Payload[2:4], "offset, frame %d", i)
		got = append(got, f.Payload[4:]...)
	}
	assert.Len(t, link.sent[0].Payload, 4+HSMLoadChunk)
	assert.Len(t, link.sent[2].Payload, 4+10)
	assert.Equal(t, env, got)
}

func TestHSMLoadStopsAtTheFirstRefusal(t *testing.T) {
	env := bytes.Repeat([]byte{0xA5}, 3*HSMLoadChunk)
	link := &refuseAtLink{
		captureLink: captureLink{cannedLink: cannedLink{payload: hsmReply()}},
		at:          2, status: StatusErrRange,
	}
	st, err := New(link).HSMLoad(context.Background(), env)
	require.ErrorIs(t, err, StatusErrRange)
	assert.Nil(t, st)
	assert.Len(t, link.sent, 2)
}

func TestHSMLoadReturnsTheVerdictOnTheEnvelope(t *testing.T) {
	link := &captureLink{cannedLink: cannedLink{status: StatusErrCrypto}}
	_, err := New(link).HSMLoad(context.Background(), []byte{0xD8})
	require.ErrorIs(t, err, StatusErrCrypto)
	require.Len(t, link.sent, 1)
	assert.Equal(t, []byte{1, 0, 0, 0, 0xD8}, link.sent[0].Payload)
}

func TestHSMLoadRefusesAnEnvelopeTheDeviceCannotHold(t *testing.T) {
	for _, n := range []int{0, HSMEnvelopeMax + 1} {
		link := &captureLink{}
		_, err := New(link).HSMLoad(context.Background(), make([]byte, n))
		var ae *ArgRangeError
		require.True(t, errors.As(err, &ae), "length %d: %v", n, err)
		assert.Equal(t, CmdHSMLoad, ae.Cmd)
		assert.Equal(t, int64(n), ae.Value)
		assert.Empty(t, link.sent)
	}
}

// A length past 1024 is refused before it is narrowed to the uint16 total:
// 65537 would otherwise wrap to a total of 1 and send a frame the device takes.
func TestHSMLoadRefusesAnOversizeEnvelopeBeforeTheTotalWraps(t *testing.T) {
	for _, n := range []int{1025, 65536, 65537} {
		link := &captureLink{}
		_, err := New(link).HSMLoad(context.Background(), make([]byte, n))
		var ae *ArgRangeError
		require.True(t, errors.As(err, &ae), "length %d: %v", n, err)
		assert.Equal(t, CmdHSMLoad, ae.Cmd)
		assert.Equal(t, "length", ae.Arg)
		assert.Equal(t, int64(n), ae.Value)
		assert.Equal(t, int64(1), ae.Min)
		assert.Equal(t, int64(1024), ae.Max)
		assert.Empty(t, link.sent)
	}
}
