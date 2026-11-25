package tfw

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// countingLink records every request frame written to it, so a test can say
// both what was sent and that nothing was.
type countingLink struct {
	recordingLink
	writes int
}

func (l *countingLink) Write(p []byte) (int, error) {
	l.writes++
	return l.recordingLink.Write(p)
}

func TestAppCommandSendsTheOpThenTheArgs(t *testing.T) {
	link := &countingLink{}
	_, err := New(link).AppCommand(context.Background(), 0x01, []byte{0x0a, 0x0b})
	require.NoError(t, err)

	assert.Equal(t, 1, link.writes)
	assert.Equal(t, CmdAppCmd, link.cmd)
	assert.Equal(t, []byte{0x01, 0x0a, 0x0b}, link.payload)
}

func TestAppCommandWithNoArgsSendsTheOpAlone(t *testing.T) {
	link := &countingLink{}
	_, err := New(link).AppCommand(context.Background(), 0x00, nil)
	require.NoError(t, err)
	assert.Equal(t, []byte{0x00}, link.payload)
}

func TestAppCommandSendsSixtyThreeBytesOfArgs(t *testing.T) {
	link := &countingLink{}
	args := bytes.Repeat([]byte{0x5a}, 63)
	_, err := New(link).AppCommand(context.Background(), 0x02, args)
	require.NoError(t, err)

	assert.Equal(t, 1, link.writes)
	assert.Equal(t, append([]byte{0x02}, args...), link.payload)
}

func TestAppCommandRefusesSixtyFourBytesBeforeSending(t *testing.T) {
	link := &countingLink{}
	_, err := New(link).AppCommand(context.Background(), 0x02, bytes.Repeat([]byte{0x5a}, 64))

	var re *ArgRangeError
	require.True(t, errors.As(err, &re), "err = %v", err)
	assert.Equal(t, &ArgRangeError{Cmd: CmdAppCmd, Arg: "args", Value: 64, Min: 0, Max: 63, Unit: "bytes"}, re)
	assert.Zero(t, link.writes, "a refused call must write nothing")
}

func TestAppCommandReturnsTheReplyBytesExactly(t *testing.T) {
	reply := []byte{0x00, 0xff, 0x10, 0x00}
	link := &countingLink{recordingLink: recordingLink{cannedLink: cannedLink{payload: reply}}}
	got, err := New(link).AppCommand(context.Background(), 0x00, nil)
	require.NoError(t, err)
	assert.Equal(t, reply, got)

	// The result is the caller's: changing it does not reach the link.
	got[0] = 0xee
	assert.Equal(t, byte(0x00), link.cannedLink.payload[0])
}

func TestAppCommandAcceptsAnEmptyReply(t *testing.T) {
	got, err := New(&countingLink{}).AppCommand(context.Background(), 0x00, nil)
	require.NoError(t, err)
	assert.Empty(t, got)
}

func TestAppCommandReturnsTheDeviceRefusals(t *testing.T) {
	for _, st := range []Status{
		StatusErrUnknownCmd,   // no application is linked, or over Ethernet
		StatusErrBadState,     // linked and not running
		StatusErrUnsupported,  // the application has no such op
		StatusErrAuthRequired, // the application wants a session
	} {
		t.Run(st.String(), func(t *testing.T) {
			link := &countingLink{recordingLink: recordingLink{cannedLink: cannedLink{status: st}}}
			got, err := New(link).AppCommand(context.Background(), 0x01, nil)
			assert.Nil(t, got)

			var s Status
			require.True(t, errors.As(err, &s), "err = %v", err)
			assert.Equal(t, st, s)
			assert.True(t, errors.Is(err, st))
		})
	}
}
