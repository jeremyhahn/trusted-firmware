// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"io"
	"testing"
	"time"
)

// goneLink is a link that has gone away: no write reaches the device.
type goneLink struct{}

var errGone = errors.New("link gone")

func (goneLink) Read([]byte) (int, error)  { return 0, io.EOF }
func (goneLink) Write([]byte) (int, error) { return 0, errGone }

// Every command with well-formed arguments reaches the wire, so a lost link
// must come back as a LinkError wrapping the transport's own error: never a
// zero value read as an answer, and never a success.
func TestEveryCommandReportsALostLink(t *testing.T) {
	d := New(goneLink{}, WithBootWait(0))
	ctx := context.Background()
	for name, call := range map[string]func() error{
		"Ping":            func() error { _, err := d.Ping(ctx, []byte("x")); return err },
		"Caps":            func() error { _, err := d.Caps(ctx); return err },
		"PinInfo":         func() error { _, err := d.PinInfo(ctx, 3); return err },
		"SysInfo":         func() error { _, err := d.SysInfo(ctx); return err },
		"AppInfo":         func() error { _, err := d.AppInfo(ctx); return err },
		"Reset":           func() error { return d.Reset(ctx, ResetSoft) },
		"SetPinMode":      func() error { return d.SetPinMode(ctx, 3, PinOutput) },
		"DigitalWrite":    func() error { return d.DigitalWrite(ctx, 3, true) },
		"DigitalRead":     func() error { _, err := d.DigitalRead(ctx, 3); return err },
		"TimerSet":        func() error { _, err := d.TimerSet(ctx, 3, true, time.Second); return err },
		"TimerCancel":     func() error { return d.TimerCancel(ctx, 3, true) },
		"TimerQuery":      func() error { _, err := d.TimerQuery(ctx, 3); return err },
		"AnalogRead":      func() error { _, err := d.AnalogRead(ctx, 14); return err },
		"SetADCReference": func() error { return d.SetADCReference(ctx, ADCRefVCC) },
		"EEPROMRead":      func() error { _, err := d.EEPROMRead(ctx, 0x80, 4); return err },
		"EEPROMWrite":     func() error { return d.EEPROMWrite(ctx, 0x80, []byte{1, 2}) },
		"PWMWrite":        func() error { return d.PWMWrite(ctx, 3, 128) },
		"Tone":            func() error { return d.Tone(ctx, 11, 1000) },
		"NoTone":          func() error { return d.NoTone(ctx, 11) },
		"PulseIn":         func() error { _, err := d.PulseIn(ctx, 2, true, 10*time.Millisecond); return err },
		"PulseCapture": func() error {
			_, err := d.PulseCapture(ctx, 2, true, 8, 10*time.Millisecond)
			return err
		},
		"I2CTransfer":  func() error { _, err := d.I2CTransfer(ctx, 0x20, []byte{1}, 2); return err },
		"SPITransfer":  func() error { _, err := d.SPITransfer(ctx, 10, []byte{1, 2}); return err },
		"SetI2CClock":  func() error { return d.SetI2CClock(ctx, 100000) },
		"ConfigureSPI": func() error { return d.ConfigureSPI(ctx, SPIMode(0), SPIClockDiv(1), false) },
		"OneWireReset": func() error { _, err := d.OneWireReset(ctx, 4); return err },
		"OneWireWrite": func() error { return d.OneWireWrite(ctx, 4, []byte{0xcc}) },
		"OneWireRead":  func() error { _, err := d.OneWireRead(ctx, 4, 9); return err },
	} {
		t.Run(name, func(t *testing.T) {
			err := call()
			var le *LinkError
			if !errors.As(err, &le) || !errors.Is(err, errGone) {
				t.Fatalf("%s over a lost link: %v, want a LinkError wrapping %v", name, err, errGone)
			}
		})
	}
}
