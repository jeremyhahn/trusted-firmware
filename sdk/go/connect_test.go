// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"bytes"
	"context"
	"os"
	"testing"
	"time"
)

// autoResetLink models a serial adapter that resets a classic Arduino on
// open: the board's bootloader jumps to the application, which announces
// itself with AG_EVENT_BOOT some time later, and only then answers ordinary
// commands. bootAfter == 0 models the adapter that never resets the board at
// all -- no event ever comes, which New must still tolerate.
type autoResetLink struct {
	start     time.Time
	bootAfter time.Duration
	bootSent  bool
	rx        bytes.Buffer
	// reads counts calls to Read, so a disabled wait can be proven to have
	// touched the conn zero times -- a fact, rather than a wall-clock bound
	// tight enough to flake under scheduler load.
	reads int
}

func newAutoResetLink(bootAfter time.Duration) *autoResetLink {
	return &autoResetLink{start: time.Now(), bootAfter: bootAfter}
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
	if !l.bootSent && l.bootAfter > 0 && time.Since(l.start) >= l.bootAfter {
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
		// A real port's read deadline expiring with nothing buffered: not
		// fatal, and the caller's own bound is what governs.
		time.Sleep(2 * time.Millisecond)
		return 0, os.ErrDeadlineExceeded
	}
	return l.rx.Read(p)
}

// (a) The board resets, the firmware's BOOT event arrives inside the bound,
// and New consumes it: the first request after New returns must succeed
// without the caller doing anything about the reset, and the consumed event
// must not resurface as an unexpected one.
func TestNewWaitsForBootEventThenFirstRequestSucceeds(t *testing.T) {
	link := newAutoResetLink(20 * time.Millisecond)
	before := time.Now()
	d := New(link, WithBootWait(2*time.Second))
	if waited := time.Since(before); waited >= 2*time.Second {
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
// through once its bound elapses, paying the bound and nothing more, and the
// device must still work afterwards.
func TestNewFallsThroughWhenNoBootEventArrives(t *testing.T) {
	link := newAutoResetLink(0) // never sends BOOT
	bound := 30 * time.Millisecond
	before := time.Now()
	d := New(link, WithBootWait(bound))
	if waited := time.Since(before); waited < bound {
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

// eventBeforeBootLink models a device that raises some OTHER event -- DEADMAN,
// say -- before its BOOT event, or instead of one within the bound. Whatever
// awaitBoot sees that is not the boot it is looking for must still reach
// Device.Events, exactly as Do would record it once ordinary requests begin.
type eventBeforeBootLink struct {
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
		time.Sleep(2 * time.Millisecond)
		return 0, os.ErrDeadlineExceeded
	}
	return l.rx.Read(p)
}

func TestAwaitBootRecordsAnyOtherEventSeenWhileWaiting(t *testing.T) {
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

// (c) Zero and negative bounds both mean "disabled": New returns immediately,
// paying no bound at all, and a BOOT event the board sends after that is no
// longer New's to consume -- it surfaces through Do exactly like any other
// unsolicited event, which is the caller's signal that the device reset.
func TestWithBootWaitZeroOrNegativeDisablesTheWait(t *testing.T) {
	for _, tc := range []struct {
		name  string
		bound time.Duration
	}{
		{"zero", 0},
		{"negative", -time.Second},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := newAutoResetLink(5 * time.Millisecond)
			d := New(link, WithBootWait(tc.bound))
			// A fact, not a wall-clock guess: disabled means New never touched
			// the conn at all, which holds regardless of how long the test
			// process was descheduled for in between.
			if link.reads != 0 {
				t.Fatalf("New called Read %d time(s) with the boot wait disabled, want zero", link.reads)
			}

			// Give the link's own BOOT event time to land, then let Do observe it:
			// with the wait disabled, nothing consumed it on the way in.
			time.Sleep(10 * time.Millisecond)
			if _, err := d.Do(context.Background(), CmdPing, nil); err != nil {
				t.Fatalf("ping: %v", err)
			}
			events := d.Events()
			if len(events) == 0 {
				t.Fatal("no boot event was observed; disabling the wait must not also discard it")
			}
			if !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
				t.Fatalf("first event = %+v, want a boot event", events[0])
			}
		})
	}
}
