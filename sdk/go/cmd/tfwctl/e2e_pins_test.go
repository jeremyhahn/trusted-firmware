// SPDX-License-Identifier: AGPL-3.0-or-later

package main

import (
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
	// heldCauses are what a board found running (a TCP wire, no reset) may
	// report: the cause of its last boot, which the run did not choose. Nil
	// means the board is always reset on connect and reports resetCause.
	heldCauses []tfw.ResetCause
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
	ledFlags   tfw.PinFlags   // ledPin's flags; zero means digital and LED
	txPin      int            // the UART's transmit pin; -1 where the UART is not in the pin table
	timerPin   uint8          // a digital output the timer scenario drives and reads back
}

// causes are the reset causes the SYS row accepts: resetCause on a board the
// run resets, heldCauses on one it finds running.
func (b peerBoard) causes(resets bool) []tfw.ResetCause {
	if resets || b.heldCauses == nil {
		return []tfw.ResetCause{b.resetCause}
	}
	return b.heldCauses
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
	// PORT=tcp:, so no reset: the cause is the last boot's, power-on after a
	// power cycle or the EN button, software after a RESET command
	// (src/hal/esp32/hal_sys.c). A watchdog or panic boot is not healthy.
	"esp32p4-poe": {
		resetCause: tfw.ResetPowerOn,
		heldCauses: []tfw.ResetCause{tfw.ResetPowerOn, tfw.ResetExternal, tfw.ResetSoftware},
		ledPort:    1, ledBit: 0, ledPWM: tfw.PWMNone, // GPIO32
		analog: 7, // GPIO20
		adcPin: 7, adcChannel: 4, adcPort: true,
		adcOnly: 0,
		// The tick is the system timer, not an LEDC channel: no PWM pin is
		// spent on it. Pin 9 here is GPIO22, an ADC input with no LEDC.
		tickPin: -1, noPWMPin: 11, // GPIO32
		pwmPin: 0,                           // GPIO2, LEDC0
		ledPin: 11, txPin: -1, timerPin: 12, // GPIO33
		// The table does not flag GPIO32 as an LED yet; #565 adds the flag
		// and takes PinLED back here.
		ledFlags: tfw.PinDigital,
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
