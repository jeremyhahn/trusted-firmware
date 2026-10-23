// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"bytes"
	"errors"
	"io"
	"testing"
	"time"
)

// Mutation tests (#163): each one kills a fault the other tests let through.

// lowerFailPort fails only when DTR is dropped; raising it succeeds. The
// shared resettablePort returns dtrErr on both edges, so a start that ignored
// the first failure would still report the second.
type lowerFailPort struct {
	resettablePort
	lowerErr error
}

func (l *lowerFailPort) SetDTR(on bool) error {
	if !on {
		l.calls = append(l.calls, "dtr-off")
		return l.lowerErr
	}
	return l.resettablePort.SetDTR(on)
}

func TestStartReportsAFailureToDropDTR(t *testing.T) {
	boom := errors.New("boom")
	l := &lowerFailPort{resettablePort: resettablePort{fakePort: fakePort{data: []byte{0xA5}}}, lowerErr: boom}
	if _, err := start(l, time.Second); !errors.Is(err, boom) {
		t.Fatalf("err = %v, want it to wrap %v", err, boom)
	}
	// A failed drop is not followed by the rest of the reset.
	if len(l.calls) != 1 || l.calls[0] != "dtr-off" {
		t.Fatalf("port calls = %v, want only dtr-off", l.calls)
	}
}

// timedPort records when each DTR edge was asked for.
type timedPort struct {
	*bootAfterPurge
	off, on time.Time
}

func (p *timedPort) SetDTR(on bool) error {
	if on {
		p.on = time.Now()
	} else {
		p.off = time.Now()
	}
	return p.bootAfterPurge.SetDTR(on)
}

// DTR is held off for the reset pulse: a board whose DTR rises at once sees no
// edge long enough to discharge the reset capacitor.
func TestStartHoldsDTROffForTheResetPulse(t *testing.T) {
	p := &timedPort{bootAfterPurge: &bootAfterPurge{&resettablePort{}, []byte{0xA5}}}
	if _, err := start(p, time.Second); err != nil {
		t.Fatal(err)
	}
	if held := p.on.Sub(p.off); held < ResetPulse {
		t.Fatalf("DTR held off for %s, want at least %s", held, ResetPulse)
	}
}

// lastBytesPort returns its data together with an error, as a reader may hand
// back the last bytes before it reports the end.
type lastBytesPort struct {
	fakePort
}

func (l *lastBytesPort) Read(p []byte) (int, error) {
	n := copy(p, l.data)
	l.data = l.data[n:]
	return n, io.EOF
}

// Bytes that arrive with an error are the board's first transmission; the
// error does not discard them.
func TestAwaitStartKeepsBytesThatArriveWithAnError(t *testing.T) {
	boot := []byte{0xA5, 0x03}
	c, err := awaitStart(&lastBytesPort{fakePort{data: append([]byte(nil), boot...)}}, time.Second)
	if err != nil {
		t.Fatalf("err = %v, want the bytes kept", err)
	}
	got := make([]byte, 8)
	if n, err := c.Read(got); err != nil || !bytes.Equal(got[:n], boot) {
		t.Fatalf("first read = %x, %v; want %x", got[:n], err, boot)
	}
}
