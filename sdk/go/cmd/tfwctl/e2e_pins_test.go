// SPDX-License-Identifier: AGPL-3.0-or-later

package main

import (
	"fmt"
	"math"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// peerBoard is what the scenarios have to know about the board they talk to:
// pin numbers and what the board's own profile says about them. tfwsim is
// compiled against the 328P's profile; `make hw-test` names the board on the
// port.
type peerBoard struct {
	// resetCause is what SYS_INFO reports after the connect. tfwsim starts
	// from power-on. A real board was just reset through DTR, and its stock
	// bootloader cleared MCUSR before the application could latch it, so the
	// honest answer is unknown (src/hal/avr/hal_sys.c).
	resetCause tfw.ResetCause
	ledPort    uint8          // the LED pin 13's port index
	ledBit     uint8          // and bit
	ledPWM     tfw.PWMSupport // what pin 13's compare unit, if any, is doing
	analog     uint8          // a pin with an ADC channel and a digital port
	adcPin     uint8          // the ADC pin the descriptor scenario reads
	adcChannel uint8          // adcPin's channel
	adcPort    bool           // whether adcPin has a digital port
	adcOnly    int            // how many pins are ADC inputs with no digital port
	tickPin    int            // a compare-unit pin whose timer is the millisecond tick; -1 where no PWM pin is spent on it
	noPWMPin   uint8          // a digital pin with no compare unit at all
	pwmPin     uint8          // a PWM pin with a free unit, which accepts a duty
	ledPin     uint8          // the pin the board's table flags as its LED
	txPin      int            // the UART's transmit pin; -1 where the UART is not in the pin table
	timerPin   uint8          // a digital output the timer scenario drives and reads back
	tonePin    uint8          // the board's TFW_TONE_PIN, the one pin TONE drives
}

var peerBoards = map[string]peerBoard{
	"atmega328p": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 5, ledPWM: tfw.PWMNone, // PB5
		analog: 14,                                // A0, PC0
		adcPin: 20, adcChannel: 6, adcPort: false, // A6
		adcOnly: 2,               // A6 and A7
		tickPin: 5, noPWMPin: 13, // OC0B
		pwmPin: 9, // OC1A
		ledPin: 13, txPin: 1, timerPin: 2,
		tonePin: 11, // OC2A, PB3
	},
	"atmega2560": {
		resetCause: tfw.ResetUnknown,
		ledPort:    1, ledBit: 7, ledPWM: tfw.PWMSystemTick, // PB7, OC0A
		analog: 54, // A0, PF0
		adcPin: 54, adcChannel: 0, adcPort: true,
		adcOnly: 0,               // every ADC input on the Mega is also a digital pin
		tickPin: 4, noPWMPin: 22, // OC0B; PA0
		pwmPin: 9, // OC2B
		ledPin: 13, txPin: 1, timerPin: 2,
		tonePin: 10, // OC2A, PB4: not the 328P's 11
	},
	// The MPS2 boards QEMU models (boards/mps2-an505/board.c and
	// boards/mps2-an500/board.c, the same table): three pins, user LEDs 0 and
	// 1 on the FPGA I/O block's port 0, outputs only, and pin 2 the shield 0
	// SPI select (MISC, port 1 bit 8), no flags; no ADC, no PWM, and the
	// UART outside the pin table. QEMU's system_reset leaves no reset mark
	// behind, so the HAL reads power-on (src/hal/cortex-m/hal_early.c). The
	// analog, PWM, EEPROM, bus and pulse rows find their families absent from
	// CAPS and fail (requireFeature), so the QEMU run leaves them out; their
	// pin fields here are never read.
	"mps2-an505": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 0, ledPWM: tfw.PWMNone, // LED0
		ledPin: 0, txPin: -1, timerPin: 1, // LED1
	},
	"mps2-an500": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 0, ledPWM: tfw.PWMNone, // LED0
		ledPin: 0, txPin: -1, timerPin: 1, // LED1
	},
	// boards/esp32p4-poe/board.c: no LED on the header, so the plain GPIO32
	// (pin 11, port 1 bit 0) stands in; GPIO20 is ADC1 channel 4 and a
	// digital pin; the console UART is outside the table. Reached over
	// PORT=tcp:, so no reset: the cause is the last boot's, power-on.
	"esp32p4-poe": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    1, ledBit: 0, ledPWM: tfw.PWMNone, // GPIO32
		analog: 7, // GPIO20
		adcPin: 7, adcChannel: 4, adcPort: true,
		adcOnly: 0,
		// The tick is the system timer, not an LEDC channel: no PWM pin is
		// spent on it. Pin 9 here is GPIO22, an ADC input with no LEDC.
		tickPin: -1, noPWMPin: 11, // GPIO32
		pwmPin: 0,                           // GPIO2, LEDC0
		ledPin: 11, txPin: -1, timerPin: 12, // GPIO33
		tonePin: 4, // GPIO6, the first pin with no LEDC channel
	},
}

// The Pin and PWM rows take their PWM pin from the board's facts (#547):
// every board whose row list runs either row has facts, and its PWM pin is
// none of the pins those rows expect to refuse a duty or to be an ADC input.
// The AVR boards keep pin 9; the ESP32-P4's pin 9 is an ADC input, so its
// row drives LEDC0.
func TestPinAndPWMRowsUseTheBoardsPWMPin(t *testing.T) {
	want := map[string]uint8{"atmega328p": 9, "atmega2560": 9, "esp32p4-poe": 0}
	for board := range boardImages {
		run := boardRun(t, board, false)
		if !run.MatchString("TestFirmwarePWM") && !run.MatchString("TestFirmwarePin") {
			continue
		}
		b, ok := peerBoards[board]
		if !ok {
			t.Errorf("%s runs the Pin or PWM row and has no pin facts in peerBoards", board)
			continue
		}
		if pin, ok := want[board]; !ok {
			t.Errorf("%s: no expected PWM pin in this test", board)
		} else if b.pwmPin != pin {
			t.Errorf("%s: PWM pin %d, want %d", board, b.pwmPin, pin)
		}
		if b.pwmPin == b.noPWMPin || int(b.pwmPin) == b.tickPin {
			t.Errorf("%s: PWM pin %d is a pin the PWM row expects to refuse a duty", board, b.pwmPin)
		}
		if b.pwmPin == b.adcPin || b.pwmPin == b.analog {
			t.Errorf("%s: PWM pin %d is the board's ADC pin", board, b.pwmPin)
		}
	}
}

// TestToneRowUsesTheBoardsTonePin: each board that runs the tone row has its
// TFW_TONE_PIN in its pin facts (boards/<board>/board_profile.h), and that pin
// is not the PWM pin the row expects TONE to refuse.
func TestToneRowUsesTheBoardsTonePin(t *testing.T) {
	want := map[string]uint8{"atmega328p": 11, "atmega2560": 10, "esp32p4-poe": 4}
	for board := range boardImages {
		if !boardRun(t, board, false).MatchString("TestFirmwareTone") {
			continue
		}
		b, ok := peerBoards[board]
		if !ok {
			t.Errorf("%s runs the tone row and has no pin facts in peerBoards", board)
			continue
		}
		if pin, ok := want[board]; !ok {
			t.Errorf("%s: no expected tone pin in this test", board)
		} else if b.tonePin != pin {
			t.Errorf("%s: tone pin %d, want %d", board, b.tonePin, pin)
		}
		if b.tonePin == b.pwmPin {
			t.Errorf("%s: tone pin %d is the PWM pin the tone row expects to be refused", board, b.tonePin)
		}
	}
}

// toneFrequency is the frequency a capture's half periods give. The first
// interval runs from the capture seeing its start level, not from an edge, so
// it is dropped, and so is the last when it would leave half a period over.
func toneFrequency(edgesUS []uint16) (float64, error) {
	if len(edgesUS) < 3 {
		return 0, fmt.Errorf("%d edges: no whole period left to time", len(edgesUS))
	}
	halves := edgesUS[1:]
	if len(halves)%2 != 0 {
		halves = halves[:len(halves)-1]
	}
	var sum uint64
	for _, h := range halves {
		sum += uint64(h)
	}
	if sum == 0 {
		return 0, fmt.Errorf("%d half periods summing to 0 us", len(halves))
	}
	return float64(len(halves)/2) * 1e6 / float64(sum), nil
}

// TestToneFrequencyTimesWholePeriods: the first interval, which starts at no
// edge, and an odd half period left over are not timed.
func TestToneFrequencyTimesWholePeriods(t *testing.T) {
	for _, c := range []struct {
		edges []uint16
		want  float64
	}{
		{[]uint16{123, 500, 500, 500, 500, 999}, 1000},
		{[]uint16{7, 250, 250, 250}, 2000},
		{[]uint16{0, 490, 510}, 1000},
	} {
		got, err := toneFrequency(c.edges)
		if err != nil || math.Abs(got-c.want) > 0.001 {
			t.Errorf("toneFrequency(%v) = %v, %v; want %v", c.edges, got, err, c.want)
		}
	}
	for _, edges := range [][]uint16{nil, {500}, {500, 500}, {1, 0, 0}} {
		if _, err := toneFrequency(edges); err == nil {
			t.Errorf("toneFrequency(%v): no error", edges)
		}
	}
}
