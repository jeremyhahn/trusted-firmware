// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"errors"
	"os"
	"testing"
	"time"
)

// portDeadline is a real port's read deadline, which fill sets on every read:
// a Read with nothing to hand back, and nothing coming, blocks until that
// deadline and reports it expired. Returning at once would spin the SDK's
// read loop for its whole bound, so expire moves the test clock past the
// deadline instead: the loop sees it expired, and nothing waits for it.
type portDeadline struct{ at time.Time }

// testClock is the SDK's clock while a test drives a fake port. It stands
// still until a port deadline expires, then steps just past it, the way a
// real port's read returns at or after its deadline and never before.
var testClock *time.Time

// useTestClock gives the calling test the clock a fake port moves. A
// portDeadline used without it panics rather than wait on the wall clock.
func useTestClock(t *testing.T) {
	t.Helper()
	at := time.Unix(1<<30, 0)
	testClock = &at
	now = func() time.Time { return *testClock }
	t.Cleanup(func() { now, testClock = time.Now, nil })
}

func (d *portDeadline) SetReadDeadline(at time.Time) error {
	d.at = at
	return nil
}

func (d *portDeadline) expire() (int, error) {
	if testClock == nil {
		panic("portDeadline without useTestClock: a fake port must not wait on the wall clock")
	}
	if d.at.IsZero() {
		// A real port with no deadline blocks here for ever.
		panic("portDeadline: Read with nothing to return and no read deadline set")
	}
	if !testClock.After(d.at) {
		*testClock = d.at.Add(time.Nanosecond)
	}
	return 0, os.ErrDeadlineExceeded
}

// autoResetLink models a serial adapter that resets a classic Arduino on
// open: the board's bootloader jumps to the application, which announces
// itself with TFW_EVENT_BOOT some reads later, and only then answers ordinary
// commands. bootOnRead == 0 models the adapter that never resets the board at
// all -- no event ever comes, which New must still tolerate.
type autoResetLink struct {
	portDeadline
	bootOnRead int
	bootSent   bool
	rx         bytes.Buffer
	// reads counts calls to Read, so a disabled wait can be proven to have
	// touched the conn zero times -- a fact, rather than a wall-clock bound
	// tight enough to flake under scheduler load.
	reads int
}

func newAutoResetLink(bootOnRead int) *autoResetLink {
	return &autoResetLink{bootOnRead: bootOnRead}
}

func (l *autoResetLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, f.Payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *autoResetLink) Read(p []byte) (int, error) {
	l.reads++
	// Due regardless of what Write already queued: on real hardware the BOOT
	// event is on the wire before the host's request even lands, since it is
	// the bootloader's reset that triggers it. Prepending rather than
	// appending keeps that ordering true here too, whether Read is polled
	// before any request was sent (New's own wait) or after one already
	// queued a reply (a disabled wait, where Do does the reading instead).
	if !l.bootSent && l.bootOnRead > 0 && l.reads >= l.bootOnRead {
		ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventBoot})
		if err != nil {
			return 0, err
		}
		queued := append([]byte{}, l.rx.Bytes()...)
		l.rx.Reset()
		l.rx.Write(ev)
		l.rx.Write(queued)
		l.bootSent = true
	}
	if l.rx.Len() == 0 {
		if !l.bootSent && l.bootOnRead > 0 {
			// The deadline of a read before the reset finishes: nothing
			// yet, and the caller's own bound is what governs.
			return 0, os.ErrDeadlineExceeded
		}
		return l.expire()
	}
	return l.rx.Read(p)
}

// (a) The board resets, the firmware's BOOT event arrives inside the bound,
// and New consumes it: the first request after New returns must succeed
// without the caller doing anything about the reset, and the consumed event
// must not resurface as an unexpected one.
func TestNewWaitsForBootEventThenFirstRequestSucceeds(t *testing.T) {
	useTestClock(t)
	link := newAutoResetLink(3)
	before := now()
	d := New(link, WithBootWait(2*time.Second))
	if waited := now().Sub(before); waited >= 2*time.Second {
		t.Fatalf("New waited the full bound (%s); it should have returned as soon as BOOT arrived", waited)
	}

	got, err := d.Do(context.Background(), CmdPing, []byte("hello"))
	if err != nil {
		t.Fatalf("first request after connect: %v", err)
	}
	if string(got.Payload) != "hello" {
		t.Fatalf("payload = %q, want %q", got.Payload, "hello")
	}
	if events := d.Events(); len(events) != 0 {
		t.Fatalf("events = %+v, want none: the connect-time BOOT must be consumed, not reported", events)
	}
}

// (b) An adapter that does not reset the board -- DTR wired off, or a board
// that was already running -- never sends BOOT at all. New must still fall
// through once its bound elapses -- never before it -- and the device must
// still work afterwards.
func TestNewFallsThroughWhenNoBootEventArrives(t *testing.T) {
	useTestClock(t)
	link := newAutoResetLink(0) // never sends BOOT
	bound := 30 * time.Millisecond
	before := now()
	d := New(link, WithBootWait(bound))
	if waited := now().Sub(before); waited < bound {
		t.Fatalf("New returned after %s, want at least the %s bound", waited, bound)
	}

	got, err := d.Do(context.Background(), CmdPing, []byte("still works"))
	if err != nil {
		t.Fatalf("request after falling through: %v", err)
	}
	if string(got.Payload) != "still works" {
		t.Fatalf("payload = %q, want %q", got.Payload, "still works")
	}
}

// staleBacklogLink models the stale-event bug directly: a BOOT event a
// PREVIOUS session's reset produced is already sitting in the conn's buffer
// before New ever reads from it -- exactly what an OS receive buffer hands
// back on the next open if nothing purges it first -- and a GENUINE BOOT
// event, from THIS session's own reset, follows on the genuineOnRead-th Read
// call, independent of whatever was already queued.
//
// ResetInputBuffer models the OS-level purge (TCIFLUSH) serialport.Open
// performs, and that awaitBoot's flushStaleBacklog also asks the conn to
// perform for itself, through the same optional-interface pattern fill
// already uses for SetReadDeadline.
type staleBacklogLink struct {
	portDeadline
	genuineOnRead int // 0 disables the genuine event entirely
	genuineSent   bool
	reads         int
	rx            bytes.Buffer
	resetCalls    int
}

func newStaleBacklogLink(genuineOnRead int) *staleBacklogLink {
	l := &staleBacklogLink{genuineOnRead: genuineOnRead}
	ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventBoot})
	if err != nil {
		panic(err) // a fixed, known-good payload; only a test bug makes this fail
	}
	l.rx.Write(ev) // present before New ever calls Read, exactly like backlog
	return l
}

func (l *staleBacklogLink) ResetInputBuffer() error {
	l.resetCalls++
	l.rx.Reset()
	return nil
}

func (l *staleBacklogLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, f.Payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *staleBacklogLink) Read(p []byte) (int, error) {
	l.reads++
	if !l.genuineSent && l.genuineOnRead > 0 && l.reads >= l.genuineOnRead {
		ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventBoot})
		if err != nil {
			return 0, err
		}
		queued := append([]byte{}, l.rx.Bytes()...)
		l.rx.Reset()
		l.rx.Write(ev)
		l.rx.Write(queued)
		l.genuineSent = true
	}
	if l.rx.Len() == 0 {
		if !l.genuineSent && l.genuineOnRead > 0 {
			return 0, os.ErrDeadlineExceeded
		}
		return l.expire()
	}
	return l.rx.Read(p)
}

// The reported bug, modelled directly: New must purge the stale backlog
// (ResetInputBuffer is called) rather than accept it as readiness, and must
// still recognise and consume the GENUINE BOOT event that follows. Getting
// this wrong is precisely what sent a request into the bootloader's window
// on the session that reported it.
func TestNewFlushesAStaleBootEventThenWaitsForTheGenuineOne(t *testing.T) {
	useTestClock(t)
	link := newStaleBacklogLink(3)
	d := New(link, WithBootWait(2*time.Second))
	if link.resetCalls == 0 {
		t.Fatal("New never asked the conn to purge its stale backlog")
	}

	got, err := d.Do(context.Background(), CmdPing, []byte("after the real boot"))
	if err != nil {
		t.Fatalf("first request after connect: %v", err)
	}
	if string(got.Payload) != "after the real boot" {
		t.Fatalf("payload = %q, want %q", got.Payload, "after the real boot")
	}
	// Neither the stale BOOT nor the genuine one should surface: the stale
	// one because it was never this session's to report, the genuine one
	// because New consumed it exactly as the plain case above proves it does.
	if events := d.Events(); len(events) != 0 {
		t.Fatalf("events = %+v, want none", events)
	}
}

// Stale backlog with no genuine BOOT to follow -- an adapter that reset once,
// a long time ago, and will not reset again for this open. New must still
// fall through cleanly rather than mistaking the stale event for readiness.
func TestNewFlushesAStaleBootEventThenFallsThroughWithNoGenuineOne(t *testing.T) {
	useTestClock(t)
	link := newStaleBacklogLink(0)
	bound := 30 * time.Millisecond
	d := New(link, WithBootWait(bound))
	if link.resetCalls == 0 {
		t.Fatal("New never asked the conn to purge its stale backlog")
	}

	got, err := d.Do(context.Background(), CmdPing, []byte("still works"))
	if err != nil {
		t.Fatalf("request after falling through: %v", err)
	}
	if string(got.Payload) != "still works" {
		t.Fatalf("payload = %q, want %q", got.Payload, "still works")
	}
	if events := d.Events(); len(events) != 0 {
		t.Fatalf("events = %+v, want none: the stale BOOT was never this session's to report", events)
	}
}

// A conn that offers no way to purge its backlog at all gets no protection
// from New beyond what it already gets from finding the FIRST thing it reads
// is not a valid BOOT event -- awaitBoot has no time-based fallback here on
// purpose (see flushStaleBacklog's doc comment for why one was tried and
// reverted). autoResetLink implements no ResetInputBuffer, so this is
// already exercised implicitly by every test above that uses it; asserted
// explicitly here for the boundary itself.
func TestNewNeverCallsResetInputBufferOnAConnThatLacksIt(t *testing.T) {
	useTestClock(t)
	link := newAutoResetLink(3)
	// autoResetLink has no ResetInputBuffer method; if New required one via
	// an unchecked type assertion instead of the optional-interface pattern,
	// this would panic rather than simply proceed without it.
	d := New(link, WithBootWait(2*time.Second))
	if !link.bootSent {
		t.Fatal("New returned before the boot event; it did not proceed through the wait")
	}
	if _, err := d.Do(context.Background(), CmdPing, nil); err != nil {
		t.Fatalf("ping after New: %v", err)
	}
}

// failedPurgeLink offers ResetInputBuffer, and it always fails -- a conn
// that claimed it could clear its own backlog and could not, which is a
// different fact from one that never offered to.
type failedPurgeLink struct {
	*staleBacklogLink
}

func (l *failedPurgeLink) ResetInputBuffer() error {
	l.resetCalls++
	return errors.New("simulated purge failure")
}

// A failed purge means nothing read afterward can be trusted as this
// session's own -- New must fall through immediately, exactly as it would
// if the wait were disabled, rather than gamble on a stale BOOT being
// accepted or a genuine one being missed. Whatever was queued -- stale,
// genuine, or both -- is left for Do to observe normally afterward.
func TestNewFallsThroughImmediatelyWhenThePurgeFails(t *testing.T) {
	useTestClock(t)
	link := &failedPurgeLink{staleBacklogLink: newStaleBacklogLink(1)}
	before := now()
	d := New(link, WithBootWait(2*time.Second))
	if waited := now().Sub(before); waited >= 2*time.Second {
		t.Fatalf("New waited the full bound (%s); a failed purge should fall through immediately", waited)
	}
	if link.resetCalls == 0 {
		t.Fatal("New never attempted the purge at all")
	}

	// The link's genuine event lands on the first read, Do's: New must not
	// have consumed any of what was queued.
	if _, err := d.Do(context.Background(), CmdPing, nil); err != nil {
		t.Fatalf("ping: %v", err)
	}
	events := d.Events()
	if len(events) == 0 {
		t.Fatal("no boot event was observed; a failed purge must not silently consume what was queued")
	}
	if !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
		t.Fatalf("first event = %+v, want a boot event", events[0])
	}
}

// (c) Zero and negative bounds both mean "disabled": New returns immediately,
// touching the conn not at all -- no read, no purge -- and a BOOT event the
// board sends after that is no longer New's to consume: it surfaces through
// Do exactly like any other unsolicited event, which is the caller's signal
// that the device reset.
func TestWithBootWaitZeroOrNegativeDisablesTheWait(t *testing.T) {
	for _, tc := range []struct {
		name  string
		bound time.Duration
	}{
		{"zero", 0},
		{"negative", -time.Second},
	} {
		t.Run(tc.name, func(t *testing.T) {
			useTestClock(t)
			link := newStaleBacklogLink(1)
			d := New(link, WithBootWait(tc.bound))
			// Facts, not a wall-clock guess: disabled means New never touched
			// the conn at all, which holds regardless of how long the test
			// process was descheduled for in between.
			if link.resetCalls != 0 {
				t.Fatalf("New purged the conn %d time(s) with the boot wait disabled, want zero", link.resetCalls)
			}
			if link.reads != 0 {
				t.Fatalf("New read the conn %d time(s) with the boot wait disabled, want zero", link.reads)
			}

			// The link's own genuine BOOT event lands on the first read, Do's:
			// with the wait disabled, nothing consumed it on the way in, and
			// the STALE one queued at construction is still there too -- Do
			// will see both, in order, which is fine: disabling the wait means
			// the caller owns interpreting whatever it sees.
			if _, err := d.Do(context.Background(), CmdPing, nil); err != nil {
				t.Fatalf("ping: %v", err)
			}
			events := d.Events()
			if len(events) != 2 {
				t.Fatalf("observed %d event(s), want the stale and the genuine boot event; disabling the wait must not discard either", len(events))
			}
			for i, ev := range events {
				if !ev.IsEvent() || ev.Payload[0] != EventBoot {
					t.Fatalf("event %d = %+v, want a boot event", i, ev)
				}
			}
		})
	}
}

// eventBeforeBootLink models a device that raises some OTHER event -- DEADMAN,
// say -- before its BOOT event, or instead of one within the bound. Whatever
// awaitBoot sees that is not the boot it is looking for must still reach
// Device.Events, exactly as Do would record it once ordinary requests begin.
type eventBeforeBootLink struct {
	portDeadline
	sent bool
	rx   bytes.Buffer
}

func (l *eventBeforeBootLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, f.Payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *eventBeforeBootLink) Read(p []byte) (int, error) {
	if !l.sent {
		ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventDeadman})
		if err != nil {
			return 0, err
		}
		l.rx.Write(ev)
		l.sent = true
	}
	if l.rx.Len() == 0 {
		return l.expire()
	}
	return l.rx.Read(p)
}

func TestAwaitBootRecordsAnyOtherEventSeenWhileWaiting(t *testing.T) {
	useTestClock(t)
	link := &eventBeforeBootLink{}
	d := New(link, WithBootWait(20*time.Millisecond))

	events := d.Events()
	if len(events) != 1 {
		t.Fatalf("events = %+v, want exactly one -- the DEADMAN seen while waiting", events)
	}
	if !events[0].IsEvent() || events[0].Payload[0] != EventDeadman {
		t.Fatalf("event = %+v, want a DEADMAN event", events[0])
	}

	// Falling through after seeing an unrelated event must not leave the
	// device unusable.
	got, err := d.Do(context.Background(), CmdPing, []byte("ok"))
	if err != nil {
		t.Fatalf("request after falling through: %v", err)
	}
	if string(got.Payload) != "ok" {
		t.Fatalf("payload = %q, want %q", got.Payload, "ok")
	}
}

// midRequestBootLink answers every request correctly, but slips an
// unsolicited BOOT event in ahead of the reply -- a device that reset for
// some unrelated reason while this exact command was already in flight.
type midRequestBootLink struct {
	rx bytes.Buffer
}

func (l *midRequestBootLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventBoot})
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, f.Payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(ev)
	l.rx.Write(resp)
	return len(p), nil
}

func (l *midRequestBootLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// Do must not mistake an event arriving mid-request for its reply, or wedge
// waiting for one that already arrived under a different guise: the request
// this test sends still gets its own, correctly correlated answer.
func TestDoIsNotWedgedByABootEventArrivingMidRequest(t *testing.T) {
	link := &midRequestBootLink{}
	d := New(link, WithBootWait(0)) // the connect-time wait is not what this test is about

	got, err := d.Do(context.Background(), CmdPing, []byte("still here"))
	if err != nil {
		t.Fatalf("Do: %v", err)
	}
	if string(got.Payload) != "still here" {
		t.Fatalf("payload = %q, want %q", got.Payload, "still here")
	}
	events := d.Events()
	if len(events) != 1 || !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
		t.Fatalf("events = %+v, want exactly one boot event", events)
	}
}

// bootThenSilentLink accepts a request, announces a BOOT event once, and then
// answers nothing further -- a device that reset mid-command and never came
// back for it.
type bootThenSilentLink struct {
	portDeadline
	rx   bytes.Buffer
	sent bool
}

func (l *bootThenSilentLink) Write(p []byte) (int, error) {
	if _, _, err := Decode(p); err != nil {
		return 0, err
	}
	if !l.sent {
		ev, err := EncodeResponse(SeqNone, Cmd(opEvent), StatusOK, []byte{EventBoot})
		if err != nil {
			return 0, err
		}
		l.rx.Write(ev)
		l.sent = true
	}
	return len(p), nil
}

func (l *bootThenSilentLink) Read(p []byte) (int, error) {
	if l.rx.Len() == 0 {
		return l.expire()
	}
	return l.rx.Read(p)
}

// A BOOT event alone is not a reply. Do must time out on its own configured
// bound rather than wedge forever, or be satisfied by an event that was never
// an answer to what it asked.
func TestDoTimesOutRatherThanWedgingOnABootEventWithNoReply(t *testing.T) {
	useTestClock(t)
	link := &bootThenSilentLink{}
	d := New(link, WithBootWait(0), WithTimeout(20*time.Millisecond))

	_, err := d.Do(context.Background(), CmdPing, nil)
	if err == nil {
		t.Fatal("expected a timeout; Do returned no error")
	}
	if !errors.Is(err, ErrTimeout) {
		t.Fatalf("err = %v, want ErrTimeout", err)
	}
	events := d.Events()
	if len(events) != 1 || !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
		t.Fatalf("events = %+v, want exactly one boot event", events)
	}
}
