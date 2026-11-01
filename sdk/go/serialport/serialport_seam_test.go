// SPDX-License-Identifier: AGPL-3.0-or-later

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

// fakePort is a serial.Port that records each call it receives and returns
// the error set for it.
type fakePort struct {
	calls      []string
	timeoutErr error
	purgeErr   error
	callErr    error
	closeErr   error
	closes     int
}

func (f *fakePort) record(name string) error {
	f.calls = append(f.calls, name)
	return f.callErr
}

func (f *fakePort) SetMode(*serial.Mode) error { return f.record("SetMode") }
func (f *fakePort) Read(b []byte) (int, error) {
	return copy(b, "rx"), f.record("Read")
}
func (f *fakePort) Write(b []byte) (int, error) { return len(b), f.record("Write") }
func (f *fakePort) Drain() error                { return f.record("Drain") }
func (f *fakePort) ResetInputBuffer() error {
	if err := f.record("ResetInputBuffer"); err != nil {
		return err
	}
	return f.purgeErr
}
func (f *fakePort) ResetOutputBuffer() error { return f.record("ResetOutputBuffer") }
func (f *fakePort) SetDTR(bool) error        { return f.record("SetDTR") }
func (f *fakePort) SetRTS(bool) error        { return f.record("SetRTS") }
func (f *fakePort) GetModemStatusBits() (*serial.ModemStatusBits, error) {
	return &serial.ModemStatusBits{CTS: true}, f.record("GetModemStatusBits")
}
func (f *fakePort) SetReadTimeout(time.Duration) error {
	if err := f.record("SetReadTimeout"); err != nil {
		return err
	}
	return f.timeoutErr
}
func (f *fakePort) Break(time.Duration) error { return f.record("Break") }
func (f *fakePort) Close() error {
	f.closes++
	return f.closeErr
}

// withOpen makes openPort return fake for the life of the test.
func withOpen(t *testing.T, fake *fakePort) {
	t.Helper()
	saved := openPort
	t.Cleanup(func() { openPort = saved })
	openPort = func(string, *serial.Mode) (serial.Port, error) { return fake, nil }
}

func TestOpenClosesThePortWhenTheReadTimeoutCannotBeSet(t *testing.T) {
	cause := errors.New("timeout refused")
	fake := &fakePort{timeoutErr: cause}
	withOpen(t, fake)

	p, err := Open("/dev/ttyFAKE0", 0)
	assert.Nil(t, p)
	require.ErrorIs(t, err, cause)
	assert.Contains(t, err.Error(), "serialport: setting read timeout on /dev/ttyFAKE0")
	assert.Equal(t, 1, fake.closes, "the port is not leaked")
}

func TestOpenClosesThePortWhenTheReceiveBufferCannotBePurged(t *testing.T) {
	cause := errors.New("purge refused")
	fake := &fakePort{purgeErr: cause}
	withOpen(t, fake)

	p, err := Open("/dev/ttyFAKE0", 0)
	assert.Nil(t, p)
	require.ErrorIs(t, err, cause)
	assert.Contains(t, err.Error(), "serialport: purging /dev/ttyFAKE0's receive buffer")
	assert.Equal(t, 1, fake.closes, "the port is not leaked")
}

func TestListReportsAnEnumerationFailure(t *testing.T) {
	cause := errors.New("no device directory")
	saved := listPorts
	t.Cleanup(func() { listPorts = saved })
	listPorts = func() ([]string, error) { return nil, cause }

	ports, err := List()
	assert.Nil(t, ports)
	require.ErrorIs(t, err, cause)
	assert.Contains(t, err.Error(), "serialport: enumerating ports")
}

func TestListReturnsWhatTheLibraryEnumerates(t *testing.T) {
	saved := listPorts
	t.Cleanup(func() { listPorts = saved })
	listPorts = func() ([]string, error) { return []string{"/dev/ttyFAKE0", "/dev/ttyFAKE1"}, nil }

	ports, err := List()
	require.NoError(t, err)
	assert.Equal(t, []string{"/dev/ttyFAKE0", "/dev/ttyFAKE1"}, ports)
}

// openFake opens a port over fake and returns it as the serial.Port it is.
func openFake(t *testing.T, fake *fakePort) serial.Port {
	t.Helper()
	withOpen(t, fake)
	conn, err := Open("/dev/ttyFAKE0", 0)
	require.NoError(t, err)
	p, ok := conn.(serial.Port)
	require.True(t, ok, "Open returns a serial.Port")
	fake.calls = nil
	return p
}

// portCalls drives each serial.Port method but Close, by the name the fake
// records it under.
var portCalls = []struct {
	name string
	call func(serial.Port) error
}{
	{"SetMode", func(p serial.Port) error { return p.SetMode(&serial.Mode{}) }},
	{"Read", func(p serial.Port) error { _, err := p.Read(make([]byte, 2)); return err }},
	{"Write", func(p serial.Port) error { _, err := p.Write([]byte("tx")); return err }},
	{"Drain", func(p serial.Port) error { return p.Drain() }},
	{"ResetInputBuffer", func(p serial.Port) error { return p.ResetInputBuffer() }},
	{"ResetOutputBuffer", func(p serial.Port) error { return p.ResetOutputBuffer() }},
	{"SetDTR", func(p serial.Port) error { return p.SetDTR(true) }},
	{"SetRTS", func(p serial.Port) error { return p.SetRTS(true) }},
	{"GetModemStatusBits", func(p serial.Port) error { _, err := p.GetModemStatusBits(); return err }},
	{"SetReadTimeout", func(p serial.Port) error { return p.SetReadTimeout(time.Second) }},
	{"Break", func(p serial.Port) error { return p.Break(time.Millisecond) }},
}

func TestPortPassesEachCallThroughWhileOpen(t *testing.T) {
	for _, tc := range portCalls {
		t.Run(tc.name, func(t *testing.T) {
			fake := &fakePort{}
			p := openFake(t, fake)
			require.NoError(t, tc.call(p))
			assert.Equal(t, []string{tc.name}, fake.calls)
		})
	}
}

func TestPortReturnsTheLibrarysErrorWhileOpen(t *testing.T) {
	for _, tc := range portCalls {
		t.Run(tc.name, func(t *testing.T) {
			cause := errors.New("line fault")
			fake := &fakePort{}
			p := openFake(t, fake)
			fake.callErr = cause
			err := tc.call(p)
			require.ErrorIs(t, err, cause)
			assert.NotErrorIs(t, err, os.ErrClosed)
		})
	}
}

func TestPortCarriesWhatTheLibraryReturns(t *testing.T) {
	fake := &fakePort{}
	p := openFake(t, fake)

	buf := make([]byte, 4)
	n, err := p.Read(buf)
	require.NoError(t, err)
	assert.Equal(t, "rx", string(buf[:n]))

	n, err = p.Write([]byte("abc"))
	require.NoError(t, err)
	assert.Equal(t, 3, n)

	bits, err := p.GetModemStatusBits()
	require.NoError(t, err)
	assert.True(t, bits.CTS)
}

func TestPortRefusesEachCallOnceClosed(t *testing.T) {
	for _, tc := range portCalls {
		t.Run(tc.name, func(t *testing.T) {
			fake := &fakePort{}
			p := openFake(t, fake)
			require.NoError(t, p.Close())

			err := tc.call(p)
			require.ErrorIs(t, err, os.ErrClosed)
			assert.Contains(t, err.Error(), "serialport: port is closed")
			assert.Empty(t, fake.calls, "the closed descriptor is not touched")
		})
	}
}

func TestPortClosesTheLibrarysPortOnce(t *testing.T) {
	cause := errors.New("close fault")
	fake := &fakePort{closeErr: cause}
	p := openFake(t, fake)

	require.ErrorIs(t, p.Close(), cause, "the first Close reports the library's error")
	assert.NoError(t, p.Close())
	assert.Equal(t, 1, fake.closes)
}

// A Read the library fails because Close ran while it was in flight reports
// the port closed, not the library's own error.
func TestPortReadThatCloseInterruptsReportsClosed(t *testing.T) {
	fake := &fakePort{}
	p := openFake(t, fake)
	c, ok := p.(*port)
	require.True(t, ok)
	c.p = &closeThenRead{fakePort: fake, close: func() { _ = c.Close() }, n: 1, err: errors.New("port has been closed")}

	buf := make([]byte, 4)
	n, err := p.Read(buf)
	require.ErrorIs(t, err, os.ErrClosed)
	assert.Equal(t, "r", string(buf[:n]), "io.Reader: the n bytes read are reported beside the error")
	assert.Equal(t, 1, fake.closes)
}

// A read that succeeded is not turned into a failure by a Close that lands
// while it returns.
func TestPortReadThatCompletesAsCloseLandsDeliversItsBytes(t *testing.T) {
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

// Open keeps a baud of one: only zero and below mean DefaultBaud. The real
// line's test (serialport_linux_test.go) covers the other bauds and 8N1.
func TestOpenKeepsABaudOfOne(t *testing.T) {
	var gotMode *serial.Mode
	saved := openPort
	t.Cleanup(func() { openPort = saved })
	openPort = func(_ string, m *serial.Mode) (serial.Port, error) {
		gotMode = m
		return &fakePort{}, nil
	}

	p, err := Open("/dev/ttyFAKE3", 1)
	require.NoError(t, err)
	require.NotNil(t, gotMode)
	assert.Equal(t, serial.Mode{
		BaudRate: 1,
		DataBits: 8,
		Parity:   serial.NoParity,
		StopBits: serial.OneStopBit,
	}, *gotMode)
	require.NoError(t, p.Close())
}

func TestOpenSetsTheReadFloorThenPurgesLast(t *testing.T) {
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

func TestPortPassesEachArgumentThrough(t *testing.T) {
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
