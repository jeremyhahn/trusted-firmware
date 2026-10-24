// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The device-capabilities row end to end: tfwctl's verbs against the real
// firmware compiled for the host, over serial. Each refusal is the status
// docs/device-capabilities/api.md and the firmware's unit tests name, carried
// over the wire unchanged. Serial only: the Ethernet allow-list refuses every
// peripheral verb over TCP (TestEthernetDeviceRefusesWhatIsNotOnTheAllowList).
//
// A refusal the SDK makes before any frame is sent -- a mode, level or ADC
// reference the CLI cannot spell, a timer or EEPROM length outside its bounds,
// a bus configuration out of range -- or one only a raw frame can carry, a
// short payload or a reserved id, is pinned by the firmware's C unit tests
// instead (test/unit/test_gpio.c, test_analog.c, test_sched.c, test_storage.c,
// test_bus.c, test_caps_wire.c).

func capsStart(t *testing.T) *tfw.Device {
	t.Helper()
	return tfw.New(peer.Start(t, simproc.Software), tfw.WithTimeout(5*time.Second))
}

// capsStatus runs one verb and returns the device status it was refused with.
func capsStatus(t *testing.T, dev *tfw.Device, verb string, args ...string) tfw.Status {
	t.Helper()
	err := run(context.Background(), dev, verb, args, false, io.Discard)
	var st tfw.Status
	require.True(t, errors.As(err, &st), "%s %v: %v, want a device status", verb, args, err)
	return st
}

// capsRun runs one verb that must succeed.
func capsRun(t *testing.T, dev *tfw.Device, verb string, args ...string) {
	t.Helper()
	require.NoError(t, run(context.Background(), dev, verb, args, false, io.Discard), "%s %v", verb, args)
}

// capsJSON runs one verb with --json and decodes what it printed into v.
func capsJSON(t *testing.T, dev *tfw.Device, v any, verb string, args ...string) {
	t.Helper()
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, verb, args, true, &out), "%s %v", verb, args)
	require.NoError(t, json.Unmarshal(out.Bytes(), v), "%s --json printed %q", verb, out.String())
}

func capsReadPin(t *testing.T, dev *tfw.Device, pin string) bool {
	t.Helper()
	var got struct {
		High bool `json:"high"`
	}
	capsJSON(t, dev, &got, "read", pin)
	return got.High
}

func TestCapsRefusalsReachTheCLIAsTheDocumentedStatus(t *testing.T) {
	dev := capsStart(t)
	for _, c := range []struct {
		name string
		args []string
		want tfw.Status
	}{
		{"PIN_MODE pin beyond the board", []string{"mode", "99", "out"}, tfw.StatusErrBadPin},
		{"PIN_MODE on a UART pin", []string{"mode", "0", "out"}, tfw.StatusErrReservedPin},
		{"DIGITAL_READ pin beyond the board", []string{"read", "99"}, tfw.StatusErrBadPin},
		{"DIGITAL_WRITE on a UART pin", []string{"write", "1", "1"}, tfw.StatusErrReservedPin},
		{"TIMER_SET on a UART pin", []string{"timer", "1", "1", "1s"}, tfw.StatusErrReservedPin},
		{"TIMER_CANCEL with no timer", []string{"timer-cancel", "13"}, tfw.StatusErrNoSlot},
		{"ANALOG_READ digital-only pin", []string{"analog", "13"}, tfw.StatusErrUnsupported},
		{"ANALOG_READ pin beyond the board", []string{"analog", "99"}, tfw.StatusErrBadPin},
		{"PWM_WRITE no compare unit", []string{"pwm", "13", "100"}, tfw.StatusErrUnsupported},
		{"PWM_WRITE tick timer pin", []string{"pwm", "5", "100"}, tfw.StatusErrUnsupported},
		{"PWM_WRITE on a UART pin", []string{"pwm", "1", "100"}, tfw.StatusErrReservedPin},
		{"PWM_WRITE pin beyond the board", []string{"pwm", "99", "100"}, tfw.StatusErrBadPin},
		{"TONE off the tone pin", []string{"tone", "3", "10000"}, tfw.StatusErrUnsupported},
		{"TONE below the reachable range", []string{"tone", "11", "100"}, tfw.StatusErrRange},
		{"PULSE_IN timeout past the cap", []string{"pulse-in", "2", "1", "5s"}, tfw.StatusErrRange},
		{"PULSE_IN zero timeout", []string{"pulse-in", "2", "1", "0s"}, tfw.StatusErrRange},
		{"PULSE_CAPTURE more edges than a reply carries", []string{"pulse-capture", "9", "1", "200", "50ms"}, tfw.StatusErrRange},
		{"PULSE_CAPTURE pin beyond the board", []string{"pulse-capture", "99", "1", "8", "50ms"}, tfw.StatusErrBadPin},
		{"EEPROM_WRITE first boot byte", []string{"eeprom-write", "0x000", "5a"}, tfw.StatusErrProtectedRegion},
		{"EEPROM_WRITE last boot byte", []string{"eeprom-write", "0x0bf", "5a"}, tfw.StatusErrProtectedRegion},
		{"EEPROM_WRITE straddling the boundary", []string{"eeprom-write", "0x0bf", "0102030405060708"}, tfw.StatusErrProtectedRegion},
		{"I2C_XFER reserved address", []string{"i2c", "0x60", "-", "1"}, tfw.StatusErrProtectedRegion},
		{"I2C_XFER address beyond seven bits", []string{"i2c", "0xff", "00", "1"}, tfw.StatusErrRange},
		{"SPI_XFER reserved chip select", []string{"spi", "1", "aa"}, tfw.StatusErrProtectedRegion},
		{"ONEWIRE_RESET on a UART pin", []string{"1wire-reset", "1"}, tfw.StatusErrReservedPin},
		{"ONEWIRE_READ zero bytes", []string{"1wire-read", "9", "0"}, tfw.StatusErrRange},
	} {
		if got := capsStatus(t, dev, c.args[0], c.args[1:]...); got != c.want {
			t.Errorf("%s: tfwctl %v = %v, want %v", c.name, c.args, got, c.want)
		}
	}
}

func TestCapsRepliesFromTheFirmwareThroughTheCLI(t *testing.T) {
	dev := capsStart(t)

	t.Run("a written level reads back", func(t *testing.T) {
		capsRun(t, dev, "mode", "13", "out")
		capsRun(t, dev, "write", "13", "1")
		require.True(t, capsReadPin(t, dev, "13"))
		capsRun(t, dev, "write", "13", "0")
		require.False(t, capsReadPin(t, dev, "13"))
	})

	t.Run("an analog pin answers a ten-bit count", func(t *testing.T) {
		capsRun(t, dev, "adc-ref", "vcc")
		var got struct {
			Raw uint16 `json:"raw"`
		}
		capsJSON(t, dev, &got, "analog", "14")
		require.LessOrEqual(t, got.Raw, uint16(1023))
	})

	t.Run("pwm and tone accept their pins", func(t *testing.T) {
		capsRun(t, dev, "pwm", "9", "128")
		capsRun(t, dev, "pwm", "9", "0")
		capsRun(t, dev, "tone", "11", "1000")
		capsRun(t, dev, "notone", "11")
	})

	t.Run("eeprom above the boot region round-trips", func(t *testing.T) {
		addr := fmt.Sprintf("%#x", tfw.EEPROMReservedEnd)
		capsRun(t, dev, "eeprom-write", addr, "0102030405060708")
		var got struct {
			Hex string `json:"hex"`
		}
		capsJSON(t, dev, &got, "eeprom-read", addr, "8")
		require.Equal(t, "0102030405060708", got.Hex)
	})

	t.Run("spi answers as many bytes as it was given", func(t *testing.T) {
		var got struct {
			Hex string `json:"hex"`
		}
		capsJSON(t, dev, &got, "spi", "10", "9f0000")
		require.Len(t, got.Hex, 6)
	})

	t.Run("a silent pulse line times out", func(t *testing.T) {
		require.Equal(t, tfw.StatusErrTimeout, capsStatus(t, dev, "pulse-in", "2", "1", "1ms"))
	})

	t.Run("an empty 1-Wire bus reports no presence", func(t *testing.T) {
		var got struct {
			Present bool `json:"present"`
		}
		capsJSON(t, dev, &got, "1wire-reset", "4")
		require.False(t, got.Present)
	})

	t.Run("the bus config commands accept their documented range", func(t *testing.T) {
		capsRun(t, dev, "i2c-config", fmt.Sprint(tfw.I2CClockMin))
		capsRun(t, dev, "i2c-config", fmt.Sprint(tfw.I2CClockMax))
		capsRun(t, dev, "spi-config", fmt.Sprint(uint8(tfw.SPIMode3)), fmt.Sprint(uint8(tfw.SPIClockDiv128)), "lsb")
		capsRun(t, dev, "spi-config", fmt.Sprint(uint8(tfw.SPIMode0)), fmt.Sprint(uint8(tfw.SPIClockDiv4)), "msb")
	})
}

// A pin told to hold a level reverts on time. tfwsim's clock is simulated
// and moves one millisecond per read of traffic (tools/tfwsim/main.c), so
// "on time" is checked against the firmware's own countdown, not the wall:
// the pin stays held while timer-query counts down, and drops only once the
// countdown has run out. The host sends nothing that could move the pin.
// Reverting with no host present at all is test_a_timed_pin_reverts_without_
// the_host in test/unit/test_sched.c, where the clock needs no traffic.
func TestCapsATimedPinRevertsOnTheFirmwaresClock(t *testing.T) {
	dev := capsStart(t)
	const hold = 50 * time.Millisecond

	capsRun(t, dev, "mode", "13", "out")
	capsRun(t, dev, "write", "13", "0")
	capsRun(t, dev, "timer", "13", "1", hold.String())

	last := hold
	for i := 0; ; i++ {
		require.Less(t, i, 1000, "pin 13 never reverted")
		var ts tfw.TimerState
		capsJSON(t, dev, &ts, "timer-query", "13")
		high := capsReadPin(t, dev, "13")
		if !ts.Active {
			require.False(t, high, "the timer is gone but pin 13 is still high")
			break
		}
		require.True(t, ts.HeldLevel)
		require.LessOrEqual(t, ts.Remaining, last, "the countdown went backwards")
		last = ts.Remaining
		if !high {
			// Dropped while the timer still runs: early unless the
			// deadline has passed and only the sweep is pending.
			require.Zero(t, ts.Remaining, "pin 13 reverted %v before its deadline", ts.Remaining)
			break
		}
	}
	require.LessOrEqual(t, last, 2*time.Millisecond, "pin 13 reverted with %v still to run", last)
}
