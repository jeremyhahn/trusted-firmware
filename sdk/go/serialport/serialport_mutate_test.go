package serialport

import (
	"errors"
	"os"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"go.bug.st/serial"
)

// Mutation tests (#163): each pins a fault the seam and pty tests let
// through. A pty forces CS8 and clears PARENB whatever it is asked, so the
// line settings are pinned at the seam, as the Mode Open hands the library.

// argPort records the arguments the wrapper passes on, which fakePort drops.
type argPort struct {
	*fakePort
	mode    *serial.Mode
	dtr     []bool
	rts     []bool
	brk     time.Duration
	timeout time.Duration
}

func (a *argPort) SetMode(m *serial.Mode) error {
	a.mode = m
	return a.fakePort.SetMode(m)
}

func (a *argPort) SetDTR(v bool) error {
	a.dtr = append(a.dtr, v)
	return a.fakePort.SetDTR(v)
}

func (a *argPort) SetRTS(v bool) error {
	a.rts = append(a.rts, v)
	return a.fakePort.SetRTS(v)
}

func (a *argPort) Break(d time.Duration) error {
	a.brk = d
	return a.fakePort.Break(d)
}

func (a *argPort) SetReadTimeout(t time.Duration) error {
	a.timeout = t
	return a.fakePort.SetReadTimeout(t)
}

// openArgs opens through the seam onto an argPort and returns both.
func openArgs(t *testing.T) (serial.Port, *argPort) {
	t.Helper()
	fake := &argPort{fakePort: &fakePort{}}
	saved := openPort
	t.Cleanup(func() { openPort = saved })
	openPort = func(string, *serial.Mode) (serial.Port, error) { return fake, nil }
	conn, err := Open("/dev/ttyFAKE0", 0)
	require.NoError(t, err)
	p, ok := conn.(serial.Port)
	require.True(t, ok, "Open returns a serial.Port")
	return p, fake
}

func TestMutateOpenAsksForEightNOneAtTheBaud(t *testing.T) {
	for _, tc := range []struct {
		name string
		baud int
		want int
	}{
		{"zero is DefaultBaud", 0, DefaultBaud},
		{"negative is DefaultBaud", -1, DefaultBaud},
		{"one is kept", 1, 1},
		{"9600 is kept", 9600, 9600},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var gotPath string
			var gotMode *serial.Mode
			saved := openPort
			t.Cleanup(func() { openPort = saved })
			openPort = func(path string, m *serial.Mode) (serial.Port, error) {
				gotPath, gotMode = path, m
				return &fakePort{}, nil
			}

			p, err := Open("/dev/ttyFAKE3", tc.baud)
			require.NoError(t, err)
			assert.Equal(t, "/dev/ttyFAKE3", gotPath)
			require.NotNil(t, gotMode)
			assert.Equal(t, serial.Mode{
				BaudRate: tc.want,
				DataBits: 8,
				Parity:   serial.NoParity,
				StopBits: serial.OneStopBit,
			}, *gotMode)
			require.NoError(t, p.Close())
		})
	}
}

func TestMutateOpenSetsTheReadFloorThenPurgesLast(t *testing.T) {
	fake := &argPort{fakePort: &fakePort{}}
	saved := openPort
	t.Cleanup(func() { openPort = saved })
	openPort = func(string, *serial.Mode) (serial.Port, error) { return fake, nil }

	p, err := Open("/dev/ttyFAKE0", 0)
	require.NoError(t, err)
	assert.Equal(t, 200*time.Millisecond, fake.timeout)
	assert.Equal(t, []string{"SetReadTimeout", "ResetInputBuffer"}, fake.calls,
		"the purge is the last thing Open does, so nothing queued before it survives")
	assert.Zero(t, fake.closes, "a port Open returns is not closed")
	require.NoError(t, p.Close())
}

func TestMutatePortPassesEachArgumentThrough(t *testing.T) {
	p, fake := openArgs(t)

	mode := &serial.Mode{BaudRate: 9600, DataBits: 7, Parity: serial.EvenParity, StopBits: serial.TwoStopBits}
	require.NoError(t, p.SetMode(mode))
	assert.Same(t, mode, fake.mode)

	require.NoError(t, p.SetDTR(true))
	require.NoError(t, p.SetDTR(false))
	assert.Equal(t, []bool{true, false}, fake.dtr)

	require.NoError(t, p.SetRTS(true))
	require.NoError(t, p.SetRTS(false))
	assert.Equal(t, []bool{true, false}, fake.rts)

	require.NoError(t, p.Break(37*time.Millisecond))
	assert.Equal(t, 37*time.Millisecond, fake.brk)

	require.NoError(t, p.SetReadTimeout(3*time.Second))
	assert.Equal(t, 3*time.Second, fake.timeout)
}

// closeThenRead closes the wrapper mid-read and returns what it was given.
type closeThenRead struct {
	*fakePort
	close func()
	n     int
	err   error
}

func (r *closeThenRead) Read(b []byte) (int, error) {
	r.close()
	return copy(b, "rx"[:r.n]), r.err
}

func TestMutateReadThatCompletesAsCloseLandsDeliversItsBytes(t *testing.T) {
	fake := &fakePort{}
	p := openFake(t, fake)
	c, ok := p.(*port)
	require.True(t, ok)
	c.p = &closeThenRead{fakePort: fake, close: func() { _ = c.Close() }, n: 2}

	buf := make([]byte, 4)
	n, err := p.Read(buf)
	require.NoError(t, err, "a read that succeeded is not turned into a failure by a racing Close")
	assert.Equal(t, "rx", string(buf[:n]))
}

func TestMutateReadThatCloseInterruptsKeepsTheBytesItGot(t *testing.T) {
	fake := &fakePort{}
	p := openFake(t, fake)
	c, ok := p.(*port)
	require.True(t, ok)
	c.p = &closeThenRead{fakePort: fake, close: func() { _ = c.Close() }, n: 1, err: errors.New("port has been closed")}

	buf := make([]byte, 4)
	n, err := p.Read(buf)
	require.ErrorIs(t, err, os.ErrClosed)
	assert.Equal(t, "r", string(buf[:n]), "io.Reader: the n bytes read are reported beside the error")
}
