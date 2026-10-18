// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Two things are asserted of every error type here: that it says what
// happened, and that the sentinel a caller has always matched against still
// matches. The second is the load-bearing one -- the sentinels are published
// surface, and a typed error that stopped answering errors.Is would break
// every host switching on them while still compiling.

func TestFrameErrorReportsWhatWasWrong(t *testing.T) {
	err := &FrameError{Reason: "response with no room for a status byte"}
	assert.Equal(t, "tfw: malformed frame: response with no room for a status byte", err.Error())
}

func TestFrameErrorMatchesItsSentinel(t *testing.T) {
	var err error = &FrameError{Reason: "length field is 0, outside 2..67"}
	assert.True(t, errors.Is(err, ErrBadFormat))
	assert.False(t, errors.Is(err, ErrBadCRC), "a malformed frame is not a damaged one")

	var fe *FrameError
	require.True(t, errors.As(err, &fe))
	assert.Equal(t, "length field is 0, outside 2..67", fe.Reason)
}

func TestCRCErrorCarriesBothChecksums(t *testing.T) {
	err := &CRCError{Computed: 0x1234, Claimed: 0xABCD}
	assert.Equal(t, "tfw: frame CRC mismatch: computed 1234, frame claims abcd", err.Error())
}

// The one every existing test depends on: a decode failure must still answer
// errors.Is(err, ErrBadCRC), and must now also yield the two values.
func TestCRCErrorMatchesItsSentinelAndUnpacks(t *testing.T) {
	wire, err := EncodeRequest(1, CmdPing, []byte{1, 2, 3})
	require.NoError(t, err)
	wire[5] ^= 0x01

	_, _, err = Decode(wire)
	require.Error(t, err)
	assert.True(t, errors.Is(err, ErrBadCRC))

	var ce *CRCError
	require.True(t, errors.As(err, &ce))
	assert.NotEqual(t, ce.Computed, ce.Claimed, "a CRC error whose two values agree is not one")
}

func TestPayloadSizeErrorReportsSizeAndLimit(t *testing.T) {
	err := &PayloadSizeError{Size: 65, Limit: MaxPayload}
	assert.Equal(t, "tfw: payload of 65 bytes exceeds the protocol maximum of 64", err.Error())
}

func TestPayloadSizeErrorMatchesItsSentinel(t *testing.T) {
	_, err := EncodeRequest(1, CmdPing, make([]byte, MaxPayload+1))
	require.Error(t, err)
	assert.True(t, errors.Is(err, ErrPayloadTooLarge))

	var pe *PayloadSizeError
	require.True(t, errors.As(err, &pe))
	assert.Equal(t, MaxPayload+1, pe.Size)
	assert.Equal(t, MaxPayload, pe.Limit)
}

func TestShortReplyErrorNamesTheCommandAndBothLengths(t *testing.T) {
	err := &ShortReplyError{Cmd: CmdCaps, Got: 9, Need: capsHeaderLen}
	assert.Equal(t, "tfw: command 0x00 reply is 9 bytes, needs at least 14", err.Error())
}

// A truncated REPLY is not a truncated FRAME: the frame arrived whole and its
// CRC held, so the link is fine and the two ends disagree about a layout.
// Conflating them would send a host resynchronising a stream that is in sync.
func TestShortReplyErrorIsNotAShortFrame(t *testing.T) {
	var err error = &ShortReplyError{Cmd: CmdSysInfo, Got: 3, Need: sysInfoLen}
	assert.False(t, errors.Is(err, ErrShortFrame))
	assert.False(t, errors.Is(err, ErrBadFormat))

	var se *ShortReplyError
	require.True(t, errors.As(err, &se))
	assert.Equal(t, CmdSysInfo, se.Cmd)
	assert.Equal(t, 3, se.Got)
	assert.Equal(t, sysInfoLen, se.Need)
}

func TestProtocolVersionErrorCarriesBothVersions(t *testing.T) {
	err := &ProtocolVersionError{DeviceMajor: 2, DeviceMinor: 3, HostMajor: 1, HostMinor: 0}
	assert.Equal(t, "tfw: device speaks protocol 2.3, this host speaks 1.0", err.Error())
}

func TestProtocolVersionErrorUnpacks(t *testing.T) {
	// A 1.x device against this 2.x host: the mismatch the #12 bump exists for.
	var err error = &ProtocolVersionError{DeviceMajor: 1, HostMajor: ProtoVersionMajor}
	var pe *ProtocolVersionError
	require.True(t, errors.As(err, &pe))
	assert.Equal(t, uint8(1), pe.DeviceMajor)
	assert.Equal(t, ProtoVersionMajor, pe.HostMajor)
	// It is nobody's sentinel: a version mismatch is a decision for the caller,
	// not a category to be swept up with framing failures.
	assert.False(t, errors.Is(err, ErrBadFormat))
}

func TestArgRangeErrorReportsTheBoundsItRefused(t *testing.T) {
	for _, tc := range []struct {
		name string
		err  *ArgRangeError
		want string
	}{
		{
			name: "a duration the scheduler cannot hold",
			err:  &ArgRangeError{Cmd: CmdTimerSet, Arg: "duration", Value: 0, Min: 1, Max: timerMaxMillis, Unit: "ms"},
			want: "tfw: command 0x14: duration is 0 ms, outside 1..2147483647",
		},
		{
			name: "an eeprom chunk over what the device commits at once",
			err:  &ArgRangeError{Cmd: CmdEEPROMWrite, Arg: "length", Value: 9, Min: 1, Max: EEPROMChunk, Unit: "bytes"},
			want: "tfw: command 0x41: length is 9 bytes, outside 1..8",
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			assert.Equal(t, tc.want, tc.err.Error())
		})
	}
}

func TestArgRangeErrorUnpacks(t *testing.T) {
	var err error = &ArgRangeError{Cmd: CmdEEPROMRead, Arg: "length", Value: 0, Min: 1, Max: EEPROMChunk, Unit: "bytes"}
	var ae *ArgRangeError
	require.True(t, errors.As(err, &ae))
	assert.Equal(t, CmdEEPROMRead, ae.Cmd)
	assert.EqualValues(t, EEPROMChunk, ae.Max)
}

func TestTimeoutErrorNamesTheCommandItWaitedFor(t *testing.T) {
	err := &TimeoutError{Cmd: CmdAttest, Elapsed: 3 * time.Second}
	assert.Equal(t, "tfw: command 0x60 did not respond after 3s", err.Error())
}

func TestTimeoutErrorMatchesItsSentinel(t *testing.T) {
	var err error = &TimeoutError{Cmd: CmdPing, Elapsed: time.Second}
	assert.True(t, errors.Is(err, ErrTimeout))

	var te *TimeoutError
	require.True(t, errors.As(err, &te))
	assert.Equal(t, CmdPing, te.Cmd)
	assert.Equal(t, time.Second, te.Elapsed)
}

func TestLinkErrorReportsTheDirectionAndTheCause(t *testing.T) {
	err := &LinkError{Op: "write", Err: fmt.Errorf("port not open")}
	assert.Equal(t, "tfw: write: port not open", err.Error())
}

// The distinction a caller acts on -- a closed port needs a reconnect, a
// stalled one does not -- lives in the wrapped error, so it must survive the
// wrapping.
func TestLinkErrorUnwrapsToTheTransportFailure(t *testing.T) {
	var err error = &LinkError{Op: "read", Err: io.EOF}
	assert.True(t, errors.Is(err, io.EOF))
	assert.ErrorIs(t, errors.Unwrap(err), io.EOF)

	var le *LinkError
	require.True(t, errors.As(err, &le))
	assert.Equal(t, "read", le.Op)
}

// A typed nil in a non-nil interface is the way a typed error reports failure
// when nothing failed: every one of these paths succeeds, so every one must
// return an error that IS nil, not one that merely holds a nil pointer.
func TestSuccessfulCallsReturnATrulyNilError(t *testing.T) {
	_, err := EncodeRequest(1, CmdPing, []byte{1})
	assert.Nil(t, err)
	assert.NoError(t, err)

	wire, err := EncodeResponse(1, CmdPing, StatusOK, []byte{1})
	require.NoError(t, err)

	_, _, err = Decode(wire)
	assert.Nil(t, err)
	assert.NoError(t, err)
}

// The three failures Device.Do can produce that no reply is involved in. They
// are tested THROUGH Do rather than by constructing the errors, because what
// is worth pinning is that the transport's failure reaches the caller as the
// type it is meant to, having survived the read loop.

// silentLink accepts every request and never answers: what a device that
// crashed mid-command looks like from this side.
type silentLink struct{ portDeadline }

func (*silentLink) Write(p []byte) (int, error) { return len(p), nil }

// Read waits out the deadline Do set, as a real serial port does: a read that
// returned instantly would spin the loop for the whole timeout.
func (l *silentLink) Read([]byte) (int, error) { return l.expire() }

func TestDoReportsAnUnansweredCommandAsATimeout(t *testing.T) {
	// silentLink never produces an error, so the connect-time boot wait would
	// otherwise pay its own bound before Do even runs; disabled because this
	// test is about Do's timeout, not New's.
	d := New(&silentLink{}, WithTimeout(20*time.Millisecond), WithBootWait(0))
	_, err := d.Do(context.Background(), CmdPing, nil)
	require.Error(t, err)
	assert.True(t, errors.Is(err, ErrTimeout))

	var te *TimeoutError
	require.True(t, errors.As(err, &te))
	assert.Equal(t, CmdPing, te.Cmd, "the command is what says whether the deadline was reasonable")
	assert.GreaterOrEqual(t, te.Elapsed, 20*time.Millisecond)
}

// closedLink is a port that went away after the request was written.
type closedLink struct{}

func (closedLink) Write(p []byte) (int, error) { return len(p), nil }

func (closedLink) Read(p []byte) (int, error) { return 0, io.EOF }

func TestDoReportsAClosedPortAsALinkError(t *testing.T) {
	d := New(closedLink{}, WithTimeout(time.Second))
	_, err := d.Do(context.Background(), CmdPing, nil)
	require.Error(t, err)

	var le *LinkError
	require.True(t, errors.As(err, &le))
	assert.Equal(t, "read", le.Op)
	// The caller reconnects on this and does not on a timeout, so the two must
	// not be the same error.
	assert.True(t, errors.Is(err, io.EOF))
	assert.False(t, errors.Is(err, ErrTimeout))
}

// deafLink refuses the write itself: the port is open and the device is not
// listening, or the handle is already closed.
type deafLink struct{}

func (deafLink) Write(p []byte) (int, error) { return 0, os.ErrClosed }

func (deafLink) Read(p []byte) (int, error) { return 0, io.EOF }

func TestDoReportsAFailedWriteAsALinkError(t *testing.T) {
	d := New(deafLink{}, WithTimeout(time.Second))
	_, err := d.Do(context.Background(), CmdPing, nil)
	require.Error(t, err)

	var le *LinkError
	require.True(t, errors.As(err, &le))
	assert.Equal(t, "write", le.Op)
	assert.True(t, errors.Is(err, os.ErrClosed))
}
