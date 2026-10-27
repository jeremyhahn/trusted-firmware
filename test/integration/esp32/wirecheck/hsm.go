package main

// -hsm: the tamper-response cases on QEMU's mps2-an505 (docs/hsm-self-defence/
// m7-design.md slice 3), each on a fresh image. The sets are hsm_sets.c's,
// read from -hsm-dir. What the engine decided is read twice: its state off
// the channel, and the board's LEDs over DigitalRead, which is what its port
// drove (src/hal/cortex-m/mps2/hsm_port.c): LED 0 for lockdown, LED 1 for
// the wipe.

import (
	"context"
	"errors"
	"fmt"
	"os"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

const (
	ledDown = 0 // A2 power off, A5 shutdown: every lockdown
	ledWipe = 1 // A4
)

var hsmCases = map[string]func(context.Context, *tfw.Device, string, time.Duration){
	"noset":     hsmNoSet,
	"forged":    hsmForged,
	"replay":    hsmReplay,
	"silence":   hsmSilence,
	"wipe":      hsmWipe,
	"noverdict": hsmNoVerdict,
}

func hsmCheck(ctx context.Context, d *tfw.Device, name, dir string, wait time.Duration) {
	c, ok := hsmCases[name]
	if !ok {
		fail("hsm", fmt.Errorf("no case %q", name))
	}
	c(ctx, d, dir, wait)
	fmt.Printf("wirecheck: hsm %s: ok\n", name)
}

// R1: with no set held, any signal is the built-in lockdown.
func hsmNoSet(ctx context.Context, d *tfw.Device, _ string, _ time.Duration) {
	st, err := d.HSMReport(ctx, tfw.HSMSignalHotplug)
	hsmState("report S5", st, err, tfw.HSMLocked, 0)
	hsmLEDs(ctx, d, true, false)
}

// R7: a set under another key is refused and counted, and changes nothing.
func hsmForged(ctx context.Context, d *tfw.Device, dir string, _ time.Duration) {
	_, err := d.HSMLoad(ctx, hsmSet(dir, "hsm-forged.cose"))
	hsmRefused("load forged", err, tfw.StatusErrCrypto)
	st, err := d.HSMHeartbeat(ctx)
	hsmState("heartbeat", st, err, 0, 1)
	hsmLEDs(ctx, d, false, false)
}

// R7: a set below the floor the held one set is refused; the held one stays.
func hsmReplay(ctx context.Context, d *tfw.Device, dir string, _ time.Duration) {
	st, err := d.HSMLoad(ctx, hsmSet(dir, "hsm-long.cose"))
	hsmState("load", st, err, tfw.HSMSetHeld, 0)
	_, err = d.HSMLoad(ctx, hsmSet(dir, "hsm-replay.cose"))
	hsmRefused("load replay", err, tfw.StatusErrProtectedRegion)
	st, err = d.HSMHeartbeat(ctx)
	hsmState("heartbeat", st, err, tfw.HSMSetHeld, 1)
	hsmLEDs(ctx, d, false, false)
}

// R5: silence past the heartbeat locks down with no message sent, so the loop
// is what ticked the engine. Nothing is sent on the HSM channel until LED 0 is
// lit; DigitalRead does not reach the engine.
func hsmSilence(ctx context.Context, d *tfw.Device, dir string, wait time.Duration) {
	st, err := d.HSMLoad(ctx, hsmSet(dir, "hsm-short.cose"))
	hsmState("load", st, err, tfw.HSMSetHeld, 0)
	deadline := time.Now().Add(wait)
	for !hsmLED(ctx, d, ledDown) {
		if time.Now().After(deadline) {
			fail("hsm silence", fmt.Errorf("LED %d still dark %v after the set's 300 ms heartbeat", ledDown, wait))
		}
	}
	hsmLEDs(ctx, d, true, false)
	st, err = d.HSMHeartbeat(ctx)
	hsmState("heartbeat", st, err, tfw.HSMSetHeld|tfw.HSMLocked, 0)
}

// R3: S5's rule wipes when a compromised verdict corroborates it, then locks
// down.
func hsmWipe(ctx context.Context, d *tfw.Device, dir string, _ time.Duration) {
	st, err := d.HSMLoad(ctx, hsmSet(dir, "hsm-long.cose"))
	hsmState("load", st, err, tfw.HSMSetHeld, 0)
	st, err = d.HSMVerdict(ctx, tfw.HSMVerdictCompromised)
	hsmState("verdict", st, err, tfw.HSMSetHeld, 0)
	st, err = d.HSMReport(ctx, tfw.HSMSignalHotplug)
	hsmState("report S5", st, err, tfw.HSMSetHeld|tfw.HSMLocked|tfw.HSMWiped, 0)
	hsmLEDs(ctx, d, true, true)
}

// R2, R3: with no verdict there is no wipe, only the rule's own actions.
func hsmNoVerdict(ctx context.Context, d *tfw.Device, dir string, _ time.Duration) {
	st, err := d.HSMLoad(ctx, hsmSet(dir, "hsm-long.cose"))
	hsmState("load", st, err, tfw.HSMSetHeld, 0)
	st, err = d.HSMReport(ctx, tfw.HSMSignalHotplug)
	hsmState("report S5", st, err, tfw.HSMSetHeld, 0)
	hsmLEDs(ctx, d, false, false)
}

// hsmSet reads one envelope hsm_sets wrote, through a root scoped to dir, so
// name cannot reach outside the directory the run was given.
func hsmSet(dir, name string) []byte {
	root, err := os.OpenRoot(dir)
	if err != nil {
		fail("hsm set", fmt.Errorf("%w (hsm_sets writes it)", err))
	}
	b, err := root.ReadFile(name)
	if cerr := root.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		fail("hsm set", fmt.Errorf("%w (hsm_sets writes it)", err))
	}
	return b
}

func hsmState(check string, st *tfw.HSMState, err error, flags uint8, refused uint32) {
	if err != nil {
		fail("hsm "+check, err)
	}
	if st.Flags != flags || st.Refused != refused {
		fail("hsm "+check, fmt.Errorf("flags 0x%02x refused %d, want flags 0x%02x refused %d",
			st.Flags, st.Refused, flags, refused))
	}
}

func hsmRefused(check string, err error, want tfw.Status) {
	if !errors.Is(err, want) {
		fail("hsm "+check, fmt.Errorf("got %v, want %v", err, want))
	}
}

func hsmLED(ctx context.Context, d *tfw.Device, pin uint8) bool {
	on, err := d.DigitalRead(ctx, pin)
	if err != nil {
		fail("hsm led", err)
	}
	return on
}

func hsmLEDs(ctx context.Context, d *tfw.Device, down, wipe bool) {
	if got := hsmLED(ctx, d, ledDown); got != down {
		fail("hsm led", fmt.Errorf("LED %d (lockdown) lit %v, want %v", ledDown, got, down))
	}
	if got := hsmLED(ctx, d, ledWipe); got != wipe {
		fail("hsm led", fmt.Errorf("LED %d (wipe) lit %v, want %v", ledWipe, got, wipe))
	}
}
