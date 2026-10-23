//go:build tfwsim

package tfw

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The device-capabilities row end to end: the SDK against the real firmware
// compiled for the host. Each refusal is the status
// docs/device-capabilities/api.md and the firmware's unit tests name, carried
// over the wire unchanged.

func startCaps(t *testing.T) *Device {
	t.Helper()
	return New(simproc.Start(t, simproc.Software), WithTimeout(5*time.Second))
}

func TestCapabilityRefusalsReachTheHostAsTheDocumentedStatus(t *testing.T) {
	d := startCaps(t)
	ctx := context.Background()
	for _, c := range []struct {
		name    string
		cmd     Cmd
		payload []byte
		want    Status
	}{
		{"PIN_MODE pin beyond the board", 0x10, []byte{99, 1}, StatusErrBadPin},
		{"PIN_MODE on a UART pin", 0x10, []byte{0, 1}, StatusErrReservedPin},
		{"PIN_MODE mode 9", 0x10, []byte{2, 9}, StatusErrRange},
		{"PIN_MODE short", 0x10, []byte{2}, StatusErrBadArgc},
		{"DIGITAL_READ pin beyond the board", 0x11, []byte{99}, StatusErrBadPin},
		{"DIGITAL_WRITE level 7", 0x12, []byte{2, 7}, StatusErrRange},
		{"DIGITAL_WRITE on a UART pin", 0x12, []byte{1, 1}, StatusErrReservedPin},
		{"TIMER_SET zero ms", 0x14, []byte{13, 1, 0, 0, 0, 0}, StatusErrRange},
		{"TIMER_SET past 0x7FFFFFFF ms", 0x14, []byte{13, 1, 0, 0, 0, 0x80}, StatusErrRange},
		{"TIMER_SET on a UART pin", 0x14, []byte{1, 1, 0xe8, 0x03, 0, 0}, StatusErrReservedPin},
		{"TIMER_CANCEL with no timer", 0x15, []byte{13, 0}, StatusErrNoSlot},
		{"ANALOG_READ digital-only pin", 0x20, []byte{13}, StatusErrUnsupported},
		{"ANALOG_READ pin beyond the board", 0x20, []byte{99}, StatusErrBadPin},
		{"ADC_REF 9", 0x21, []byte{9}, StatusErrRange},
		{"PWM_WRITE no compare unit", 0x22, []byte{13, 100}, StatusErrUnsupported},
		{"PWM_WRITE tick timer pin", 0x22, []byte{5, 100}, StatusErrUnsupported},
		{"PWM_WRITE on a UART pin", 0x22, []byte{1, 100}, StatusErrReservedPin},
		{"PWM_WRITE pin beyond the board", 0x22, []byte{99, 100}, StatusErrBadPin},
		{"TONE off the tone pin", 0x23, []byte{3, 0x10, 0x27}, StatusErrUnsupported},
		{"TONE below the reachable range", 0x23, []byte{11, 100, 0}, StatusErrRange},
		{"PULSE_IN timeout past the cap", 0x30, []byte{2, 1, 0x40, 0x4b, 0x4c, 0}, StatusErrRange},
		{"PULSE_IN zero timeout", 0x30, []byte{2, 1, 0, 0, 0, 0}, StatusErrRange},
		{"PULSE_CAPTURE more edges than a reply carries", 0x34, []byte{9, 1, 200, 0x50, 0xc3, 0, 0}, StatusErrRange},
		{"PULSE_CAPTURE pin beyond the board", 0x34, []byte{99, 1, 8, 0x50, 0xc3, 0, 0}, StatusErrBadPin},
		{"PULSE_COUNT_START reserved", 0x31, nil, StatusErrUnknownCmd},
		{"PULSE_COUNT_READ reserved", 0x32, nil, StatusErrUnknownCmd},
		{"PULSE_COUNT_STOP reserved", 0x33, nil, StatusErrUnknownCmd},
		{"EEPROM_WRITE first boot byte", 0x41, []byte{0x00, 0x00, 0x5a}, StatusErrProtectedRegion},
		{"EEPROM_WRITE last boot byte", 0x41, []byte{0xbf, 0x00, 0x5a}, StatusErrProtectedRegion},
		{"EEPROM_WRITE straddling the boundary", 0x41, []byte{0xbf, 0x00, 1, 2, 3, 4, 5, 6, 7, 8}, StatusErrProtectedRegion},
		{"EEPROM_WRITE past the chunk", 0x41, append([]byte{0x00, 0x01}, make([]byte, 9)...), StatusErrRange},
		{"EEPROM_WRITE no data", 0x41, []byte{0x00, 0x01}, StatusErrBadArgc},
		{"I2C_XFER reserved address", 0x50, []byte{0x60, 1}, StatusErrProtectedRegion},
		{"I2C_XFER address beyond seven bits", 0x50, []byte{0xff, 1, 0}, StatusErrRange},
		{"SPI_XFER reserved chip select", 0x51, []byte{1, 0xaa}, StatusErrProtectedRegion},
		{"ONEWIRE_RESET on a UART pin", 0x52, []byte{1}, StatusErrReservedPin},
		{"ONEWIRE_READ zero bytes", 0x54, []byte{9, 0}, StatusErrRange},
		{"I2C_CONFIG below 1 kHz", 0x55, []byte{0xe7, 0x03, 0, 0}, StatusErrRange},
		{"I2C_CONFIG above 400 kHz", 0x55, []byte{0x81, 0x1a, 0x06, 0}, StatusErrRange},
		{"SPI_CONFIG mode 4", 0x56, []byte{4, 0, 0}, StatusErrRange},
		{"SPI_CONFIG clkdiv 4", 0x56, []byte{0, 4, 0}, StatusErrRange},
		{"SPI_CONFIG lsb_first 2", 0x56, []byte{0, 0, 2}, StatusErrRange},
	} {
		_, err := d.Do(ctx, c.cmd, c.payload)
		var st Status
		if !errors.As(err, &st) || st != c.want {
			t.Errorf("%s: err = %v, want %v", c.name, err, c.want)
		}
	}
}

func TestCapabilityRepliesFromTheFirmware(t *testing.T) {
	d := startCaps(t)
	ctx := context.Background()

	t.Run("a written level reads back", func(t *testing.T) {
		require.NoError(t, d.SetPinMode(ctx, 13, PinOutput))
		require.NoError(t, d.DigitalWrite(ctx, 13, true))
		high, err := d.DigitalRead(ctx, 13)
		require.NoError(t, err)
		require.True(t, high)
		require.NoError(t, d.DigitalWrite(ctx, 13, false))
		high, err = d.DigitalRead(ctx, 13)
		require.NoError(t, err)
		require.False(t, high)
	})

	t.Run("an analog pin answers a ten-bit count", func(t *testing.T) {
		require.NoError(t, d.SetADCReference(ctx, ADCRefVCC))
		raw, err := d.AnalogRead(ctx, 14)
		require.NoError(t, err)
		require.LessOrEqual(t, raw, uint16(1023))
	})

	t.Run("pwm and tone accept their pins", func(t *testing.T) {
		require.NoError(t, d.PWMWrite(ctx, 9, 128))
		require.NoError(t, d.PWMWrite(ctx, 9, 0))
		require.NoError(t, d.Tone(ctx, 11, 1000))
		require.NoError(t, d.NoTone(ctx, 11))
	})

	t.Run("eeprom above the boot region round-trips", func(t *testing.T) {
		data := []byte{1, 2, 3, 4, 5, 6, 7, 8}
		require.NoError(t, d.EEPROMWrite(ctx, EEPROMReservedEnd, data))
		got, err := d.EEPROMRead(ctx, EEPROMReservedEnd, uint8(len(data)))
		require.NoError(t, err)
		require.Equal(t, data, got)
	})

	t.Run("spi answers as many bytes as it was given", func(t *testing.T) {
		got, err := d.SPITransfer(ctx, 10, []byte{0x9f, 0, 0})
		require.NoError(t, err)
		require.Len(t, got, 3)
	})

	t.Run("a silent pulse line times out", func(t *testing.T) {
		_, err := d.PulseIn(ctx, 2, true, time.Millisecond)
		var st Status
		require.True(t, errors.As(err, &st) && st == StatusErrTimeout, "PulseIn: %v", err)
	})

	t.Run("an empty 1-Wire bus reports no presence", func(t *testing.T) {
		present, err := d.OneWireReset(ctx, 4)
		require.NoError(t, err)
		require.False(t, present)
	})

	t.Run("the bus config commands accept their documented range", func(t *testing.T) {
		require.NoError(t, d.SetI2CClock(ctx, I2CClockMin))
		require.NoError(t, d.SetI2CClock(ctx, I2CClockMax))
		require.NoError(t, d.ConfigureSPI(ctx, SPIMode3, SPIClockDiv128, true))
		require.NoError(t, d.ConfigureSPI(ctx, SPIMode0, SPIClockDiv4, false))
	})
}

// A pin told to hold a level reverts on time. tfwsim's clock is simulated
// and moves one millisecond per read of traffic (tools/tfwsim/main.c), so
// "on time" is checked against the firmware's own countdown, not the wall:
// the pin stays held while TIMER_QUERY counts down, and drops only once the
// countdown has run out. The host sends nothing that could move the pin.
// Reverting with no host present at all is test_a_timed_pin_reverts_without_
// the_host in test/unit/test_sched.c, where the clock needs no traffic.
func TestATimedPinRevertsOnTheFirmwaresClock(t *testing.T) {
	d := startCaps(t)
	ctx := context.Background()
	const hold = 50 * time.Millisecond

	require.NoError(t, d.SetPinMode(ctx, 13, PinOutput))
	require.NoError(t, d.DigitalWrite(ctx, 13, false))
	_, err := d.TimerSet(ctx, 13, true, hold)
	require.NoError(t, err)

	last := hold
	for i := 0; ; i++ {
		require.Less(t, i, 1000, "pin 13 never reverted")
		ts, err := d.TimerQuery(ctx, 13)
		require.NoError(t, err)
		high, err := d.DigitalRead(ctx, 13)
		require.NoError(t, err)
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
