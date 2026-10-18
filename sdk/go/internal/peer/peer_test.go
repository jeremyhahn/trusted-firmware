// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"bytes"
	"errors"
	"io"
	"path/filepath"
	"testing"
	"time"
)

func TestFromEnvDefaultsToTheSimulatorsBoard(t *testing.T) {
	t.Setenv(PortEnv, "")
	t.Setenv(BoardEnv, "")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if got.Hardware() || got.Board != SimBoard {
		t.Fatalf("target = %+v, want the simulator on %s", got, SimBoard)
	}
}

func TestFromEnvNamesTheBoardOnThePort(t *testing.T) {
	t.Setenv(PortEnv, "/dev/ttyACM0")
	t.Setenv(BoardEnv, "atmega2560")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if !got.Hardware() || got.Port != "/dev/ttyACM0" || got.Board != "atmega2560" {
		t.Fatalf("target = %+v, want atmega2560 on /dev/ttyACM0", got)
	}
}

func TestFromEnvRefusesHalfATarget(t *testing.T) {
	for _, c := range []struct{ port, board string }{
		{"/dev/ttyACM0", ""},
		{"", "atmega2560"},
	} {
		t.Setenv(PortEnv, c.port)
		t.Setenv(BoardEnv, c.board)
		if got, err := FromEnv(); err == nil {
			t.Errorf("port %q board %q: target = %+v, want an error", c.port, c.board, got)
		}
	}
}

func TestOpenFailsOnAMissingPort(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "ttyNONE")
	if c, err := Open(missing, time.Second); err == nil {
		_ = c.Close()
		t.Fatal("a port that does not exist was opened")
	}
}

// fakePort stands in for a serial port: Read returns (0, nil) while it has
// nothing, as serialport.Open's read timeout does, until silent reads have
// passed. Each of those returns at once: the silent reads are counted, and a
// port with nothing at all is bounded by the caller's own deadline.
type fakePort struct {
	silent  int
	data    []byte
	err     error
	written bytes.Buffer
	closed  bool
}

func (f *fakePort) Read(p []byte) (int, error) {
	if f.silent > 0 {
		f.silent--
		return 0, nil
	}
	if f.err != nil {
		return 0, f.err
	}
	if len(f.data) == 0 {
		return 0, nil
	}
	n := copy(p, f.data)
	f.data = f.data[n:]
	return n, nil
}

func (f *fakePort) Write(p []byte) (int, error) { return f.written.Write(p) }
func (f *fakePort) Close() error                { f.closed = true; return nil }

func TestAwaitStartKeepsTheFirstTransmissionForTheCaller(t *testing.T) {
	boot := []byte{0xA5, 0x03, 0x00, 0x80, 0x01}
	f := &fakePort{silent: 3, data: append([]byte(nil), boot...)}
	c, err := awaitStart(f, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	// The wait read the BOOT bytes; the caller must still see every one.
	got := make([]byte, 2)
	var all []byte
	for len(all) < len(boot) {
		n, err := c.Read(got)
		if err != nil {
			t.Fatal(err)
		}
		all = append(all, got[:n]...)
	}
	if !bytes.Equal(all, boot) {
		t.Fatalf("read %x after the wait, want %x", all, boot)
	}
	if _, err := c.Write([]byte{1, 2}); err != nil || !bytes.Equal(f.written.Bytes(), []byte{1, 2}) {
		t.Fatalf("write err = %v, port saw %x; want 0102 passed through", err, f.written.Bytes())
	}
	if err := c.Close(); err != nil || !f.closed {
		t.Fatalf("close err = %v, port closed = %v", err, f.closed)
	}
}

// tfw.Device purges any conn offering ResetInputBuffer before it waits for
// BOOT; the started conn must not offer it, or the purge would discard the very
// event the wait kept.
func TestAwaitStartOffersNoPurge(t *testing.T) {
	c, err := awaitStart(&fakePort{data: []byte{0xA5}}, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := c.(interface{ ResetInputBuffer() error }); ok {
		t.Fatal("the started conn offers ResetInputBuffer")
	}
}

func TestAwaitStartFailsOnASilentBoard(t *testing.T) {
	f := &fakePort{}
	start := time.Now()
	_, err := awaitStart(f, 20*time.Millisecond)
	if !errors.Is(err, ErrSilent) {
		t.Fatalf("err = %v, want %v", err, ErrSilent)
	}
	if took := time.Since(start); took > time.Second {
		t.Fatalf("a 20ms bound took %s", took)
	}
	if len(f.written.Bytes()) != 0 {
		t.Fatalf("wrote %x to a board that had not started", f.written.Bytes())
	}
}

// resettablePort is a fakePort that also drives DTR and purges, recording the
// order it was asked to.
type resettablePort struct {
	fakePort
	calls    []string
	dtrErr   error
	raiseErr error // raising DTR only
	purgeErr error
}

func (r *resettablePort) SetDTR(on bool) error {
	if on {
		r.calls = append(r.calls, "dtr-on")
		if r.raiseErr != nil {
			return r.raiseErr
		}
	} else {
		r.calls = append(r.calls, "dtr-off")
	}
	return r.dtrErr
}

func (r *resettablePort) ResetInputBuffer() error {
	r.calls = append(r.calls, "purge")
	// A purge discards whatever the old application sent before the reset.
	r.data = nil
	return r.purgeErr
}

func TestStartResetsTheBoardThenWaitsForItsFirstTransmission(t *testing.T) {
	r := &resettablePort{fakePort: fakePort{data: []byte("stale")}}
	// The new application's BOOT arrives after the purge, and after a while.
	r.silent = 2
	c, err := start(&bootAfterPurge{r, []byte{0xA5, 0x01}}, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"dtr-off", "dtr-on", "purge"}
	if len(r.calls) != len(want) {
		t.Fatalf("port calls = %v, want %v", r.calls, want)
	}
	for i := range want {
		if r.calls[i] != want[i] {
			t.Fatalf("port calls = %v, want %v", r.calls, want)
		}
	}
	got := make([]byte, 8)
	n, err := c.Read(got)
	if err != nil || !bytes.Equal(got[:n], []byte{0xA5, 0x01}) {
		t.Fatalf("first read = %x, %v; want the new application's a501", got[:n], err)
	}
	if len(r.written.Bytes()) != 0 {
		t.Fatalf("wrote %x before the board had started", r.written.Bytes())
	}
}

// bootAfterPurge delivers boot once the purge has happened, as a board does:
// its application starts after the bootloader, long after the reset.
type bootAfterPurge struct {
	*resettablePort
	boot []byte
}

func (b *bootAfterPurge) ResetInputBuffer() error {
	err := b.resettablePort.ResetInputBuffer()
	b.data = b.boot
	return err
}

func TestStartFailsOnAPortThatCannotReset(t *testing.T) {
	if _, err := start(&fakePort{data: []byte{0xA5}}, time.Second); err == nil {
		t.Fatal("a port with no DTR control was accepted")
	}
}

func TestStartReportsAFailedReset(t *testing.T) {
	boom := errors.New("boom")
	for _, r := range []*resettablePort{{dtrErr: boom}, {raiseErr: boom}, {purgeErr: boom}} {
		if _, err := start(r, time.Second); !errors.Is(err, boom) {
			t.Errorf("err = %v, want it to wrap %v", err, boom)
		}
	}
}

func TestAwaitStartFailsOnAReadError(t *testing.T) {
	_, err := awaitStart(&fakePort{err: io.ErrUnexpectedEOF}, time.Second)
	if !errors.Is(err, io.ErrUnexpectedEOF) {
		t.Fatalf("err = %v, want it to wrap %v", err, io.ErrUnexpectedEOF)
	}
}
